/*   Small Issue Tracker
 *   Copyright (C) 2015 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include <iostream>
#include <string>
#include <list>
#include <algorithm>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>
#include <fstream>
#include <sstream>
#include <iostream>

#include "clone.h"
#include "global.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "mg_win32.h"
#include "console.h"
#include "repository/db.h"
#include "httpClient.h"
#include "Args.h"
#include "restApi.h"
#include "project/Project.h"
#include "localClient.h"

#define LOG_CLI(...) { printf(__VA_ARGS__); fflush(stdout);}

/** smit distributed functions
  *     smit clone .... : download a local copy of a smit repository
  *     smit pull ....  : download latest changes from a smit server
  *     smit push ....  : upload local changes to a smit server
  *
  * 1. Conflicts on existing issues
  *
  * These conflicts occur typically when a user modified an issue on the server and
  * another user modified the same issue on his/her local clone.
  * These conflicts are detected during a pushing, but they can be resolved only
  * by a pulling.
  * These conflicts are resolved either automatically or interactively.
  *
  *
  * 2. Conflicts on new issues
  * These conflicts occur typically when a user creates a new issue on the server and
  * another user creates a new issue on his/her local clone, and both new issues get
  * the same issue id.
  *
  * These conflicts are resolved by pulling. The local issue of the clone is
  * renamed. For example issue 123 will be renamed issue 124.
  *
  * These conflicts are also resolved when pushing. New local issues may
  * be renamed by the server. There may be 2 cases where a conflicting new issue on the server
  * has already taken the id of the pushed issue :
  *   - a new issue on the same project
  *   - or a new issue on a different project (remember that with global numbering,
  *     the issue ids are shared between several projects)
  *
  *
  */

// resolve strategies for pulling conflicts
enum MergeStrategy { MERGE_KEEP_LOCAL, MERGE_DROP_LOCAL, MERGE_INTERACTIVE};
struct PullContext {
    std::string rooturl; // eg: http://example.com:8090/
    std::string localRepo; // path to local repository
    HttpClientContext httpCtx; // session identifier
    MergeStrategy mergeStrategy; // for pulling only, not cloning

    inline std::string getTmpDir() const { return localRepo + "/.tmp"; }
};

static void terminateSession()
{
    curl_global_cleanup();
}

int testSessid(const std::string url, const HttpClientContext &ctx)
{
    LOG_DEBUG("testSessid(%s, %s)", url.c_str(), ctx.cookieSessid.c_str());

    HttpRequest hr(ctx);
    hr.setUrl(url + "/");
    int status = hr.test();
    LOG_DEBUG("status = %d", status);

    if (status == 200) return 0;
    return -1;

}

int pullObjects(const PullContext &ctx, const Project &p)
{
    std::string urlFiles = ctx.rooturl + "/" + p.getUrlName() + "/" RESOURCE_FILES;

    // get the list of all remote objects
    std::string objectsList;
    int r = HttpRequest::downloadInMemory(ctx.httpCtx, urlFiles, objectsList);
    if (r < 0) {
        LOG_ERROR("Could not download %s: r=%d", urlFiles.c_str(), r);
        return -1;
    }

    // iterate over the list of all objects and download those that are not in local

    int count = 0;
    std::istringstream objectsStream(objectsList);
    std::string objectId;
    while (getline(objectsStream, objectId)) {
        trim(objectId);
        if (objectId.empty()) continue; // should not happen though

        std::string destLocal = p.getObjectsDir() + "/" + Object::getSubpath(objectId);
        if (fileExists(destLocal)) continue; // already in local repo

        // download

        count++;
        LOG_CLI("\r%s: pulling files: %d", p.getName().c_str(), count);

        std::string url = urlFiles + "/" + objectId;
        int r = HttpRequest::downloadFile(ctx.httpCtx, url, destLocal);
        if (r != 0) {
            LOG_ERROR("Could not download %s: r=%d", url.c_str(), r);
            return -1;
        }
    }

    if (count > 0) LOG_CLI("\n");

    return 0;
}

/** Recursively clone files and directories
  *
  * Files that already exist locally are by default not pulled.
  *
  * @param ctx
  * @param srcResource
  * @param destLocal
  * @param counter
  * @param force
  *     Download even if local file exists.
  *
  * @return
  *     0 success
  *    -1 an error occurred and the recursive pulling aborted
  */
int cloneFiles(const PullContext &ctx, const std::string &srcResource,
               const std::string &destLocal, int &counter, bool force)
{
    // Download the resource locally in a temporary location
    // and determine if it is a directory or a regular file

    LOG_DIAG("cloneFiles %s", srcResource.c_str());

    if (!force && fileExists(destLocal) && !isDir(destLocal)) {
        // File already there. Skip this download.
        LOG_DEBUG("File already there, skip this download");
        return 0;
    }

    std::string localTmp = ctx.getTmpDir() + "/.download";
    std::string url = ctx.rooturl + "/" + srcResource;

    int r = HttpRequest::downloadFile(ctx.httpCtx, url, localTmp);

    if (r == 0) {
        // Regular file downloaded successfully
        // move it to the right place
        r = rename(localTmp.c_str(), destLocal.c_str());
        if (r != 0) {
            LOG_ERROR("Could not rename %s -> %s: %s", localTmp.c_str(),
                      destLocal.c_str(), strerror(errno));
            return -1;
        }
        LOG_DIAG("pulled file: %s", getBasename(srcResource).c_str()); // indicate progress
        counter++;
        LOG_CLI("\rPulling files: %d", counter);
        unlink(localTmp.c_str());

    } else if (r == 1) {
        // Directory listing
        // Create the directory locally
        mkdirs(destLocal);
        // Load in memory
        std::string listing;
        r = loadFile(localTmp, listing);
        if (r != 0) {
            LOG_ERROR("Could not load file '%s': %s", localTmp.c_str(), strerror(errno));
            return -1;
        }
        unlink(localTmp.c_str());
        std::istringstream flisting(listing);
        std::string filename;
        while (getline(flisting, filename)) {
            trim(filename);
            if (filename.empty()) continue;

            // manage special characters in filename that must be escaped in URL
            // eg: spaces
            std::string urlFilename = urlEncode(filename);
            std::string rsrc = srcResource + "/" + urlFilename;
            std::string dlocal = destLocal + "/" + filename;
            r = cloneFiles(ctx, rsrc, dlocal, counter, force);
            if (r != 0) {
                LOG_ERROR("Abort pulling.");
                return r;
            }
        }
        if (flisting.bad()) {
            LOG_ERROR("Error loading listing of objects '%s': %s", localTmp.c_str(), strerror(errno));
            return -1;
        }

    } else {
        // error
        LOG_ERROR("Could not download %s: r=%d", url.c_str(), r);
        return -1;
    }

    return 0;
}


/** Get all the projects that we have read-access to
  */
int getProjects(const std::string &rooturl, const std::string &destdir, const HttpClientContext &ctx)
{
    LOG_DEBUG("getProjects(%s, %s, %s)", rooturl.c_str(), destdir.c_str(), ctx.cookieSessid.c_str());

    PullContext cloneCtx;
    cloneCtx.httpCtx = ctx;
    cloneCtx.rooturl = rooturl;
    cloneCtx.localRepo = destdir;
    cloneCtx.mergeStrategy = MERGE_KEEP_LOCAL; // not used here (brut cloning)
    int counter = 0;
    LOG_CLI("Cloning All Projects...\n");
    int r = cloneFiles(cloneCtx, "/", destdir, counter, false);
    LOG_CLI("\n");

    return r;
}

/** Merge a local entry into a remote branch (downloaded locally)
  *
  * @param remoteIssue
  *      in/out: The remote issue instance is updated by the merging
  * @return
  *      Pointer to the newly created merging entry
  *      NULL if no merging entry was created
  *
  */
Entry *mergeEntry(const Entry *localEntry, Issue &remoteIssue, const Issue &remoteConflictingIssuePart, MergeStrategy ms)
{
    PropertiesMap newProperties; // the resulting properties of the new entry

    bool isConflicting = false;
    // merge the properties
    // for each property in the local entry, look if it must be:
    // - ignored
    // - kept
    // - interactively merged
    std::map<std::string, std::list<std::string> >::const_iterator localProperty;
    FOREACH(localProperty, localEntry->properties) {
        std::string propertyName = localProperty->first;
        std::list<std::string> localValue = localProperty->second;

        if (propertyName == K_MESSAGE) continue; // handled below
        if (propertyName == K_FILE) {
            // keep local file
            newProperties[propertyName] = localValue;
            continue;
        }

        std::map<std::string, std::list<std::string> >::const_iterator remoteProperty;

        // look if the value in the local entry is the same as in the remote issue
        remoteProperty = remoteIssue.properties.find(propertyName);
        if (remoteProperty != remoteIssue.properties.end()) {
            if (remoteProperty->second == localValue) {
                // (case1) the local entry brings no change: ignore this property
                continue;
            }
        }

        // look if the property has been modified on the remote side
        remoteProperty = remoteConflictingIssuePart.properties.find(propertyName);
        if (remoteProperty == remoteConflictingIssuePart.properties.end()) {
            // This property has not been changed on the remote side
            // Keep it unchanged
            newProperties[propertyName] = localValue;

        } else {
            // This property has also been changed on the remote side.
            if (localProperty->second == remoteProperty->second) {
                // same value for this property
                // do not keep it for the new entry
                // should not happen as this case should be covered by (case1)
                continue;
            }

            LOG_DEBUG("Merge conflict: %s", propertyName.c_str());
            // there is a conflict: the remote side has changed this property,
            // but with a different value than the local entry
            isConflicting = true;

            if (ms == MERGE_INTERACTIVE) {
                std::list<std::string> remoteValue = remoteProperty->second;
                LOG_CLI("-- Conflict on issue %s: %s\n", remoteIssue.id.c_str(), remoteIssue.getSummary().c_str());
                LOG_CLI("Remote: %s => %s\n", propertyName.c_str(), toString(remoteValue).c_str());
                LOG_CLI("Local : %s => %s\n", propertyName.c_str(), toString(localValue).c_str());
                std::string response;
                while (response != "l" && response != "L" && response != "r" && response != "R") {
                    LOG_CLI("Select: (l)ocal, (r)emote: ");
                    std::cin >> response;
                }
                if (response == "l" || response == "L") {
                    // keep the local property
                    newProperties[propertyName] = localValue;
                } else {
                    // drop the local property
                    LOG_DEBUG("Local property dropped.");
                }
            } else if (ms == MERGE_KEEP_LOCAL) {
                // keep the local property
                newProperties[propertyName] = localValue;
            } else { // MERGE_DROP_LOCAL
                // drop the local property
            }
        }
    }

    // keep the message (ask for confirmation?)
    if (localEntry->getMessage().size() > 0) {
        // if a conflict was detected before, then ask the user to keep the message of not
        if (isConflicting && ms == MERGE_INTERACTIVE) {
            LOG_CLI("Local message:\n");
            LOG_CLI("--------------------------------------------------\n");
            LOG_CLI("%s\n", localEntry->getMessage().c_str());
            LOG_CLI("--------------------------------------------------\n");
            std::string response;
            while (response != "k" && response != "K" && response != "d" && response != "D") {
                LOG_CLI("Select: (k)eep message, (d)rop message: ");
                std::cin >> response;
            }
            if (response == "k" || response == "K") {
                // keep the message
                newProperties[K_MESSAGE].push_back(localEntry->getMessage());
            }
        } else if (isConflicting && ms == MERGE_DROP_LOCAL) {
            // drop the message
            LOG_DEBUG("Local message dropped.");
        } else {
            // no conflict on the properties. keep the message unchanged
            newProperties[K_MESSAGE].push_back(localEntry->getMessage());
        }
    }

    // check if this new entry must be kept
    if (newProperties.size() > 0) {
        // store the new entry
        std::list<AttachedFileRef> files;
        LOG_ERROR("attached files broken");
        Entry *e = Entry::createNewEntry(newProperties, files, localEntry->author, remoteIssue.latest);
        remoteIssue.addEntry(e);
        LOG_CLI("New entry: %s\n", e->id.c_str());
        return e;
    } else {
        return 0;
    }
}

/** Manage the merging of conflicting entries of an issue
  *
  */
void handleConflictOnEntries(const PullContext &pullCtx, Project &p,
                             const Entry *conflictingLocalEntry,
                             Issue &remoteIssue)
{
    // remote: a--b--c--d
    // local:  a--b--e
    // remote issue has conflicting entries. download them locally in a separate directory

    // compute the remote part of the issue that is conflicting with local entries
    std::string commonParent = conflictingLocalEntry->parent;
    // look for this parent in the remote issue
    Entry *re = remoteIssue.first;
    while (re && re->id != commonParent) re = re->getNext();
    if (!re) {
        LOG_ERROR("Cannot find remote common parent in locally downloaded issue: %s", commonParent.c_str());
        LOG_ERROR("Abort.");
        exit(1);
    }
    Issue remoteConflictingIssuePart;
    re = re->getNext(); // start with the first conflicting entry
    while (re) {
        remoteConflictingIssuePart.consolidateWithSingleEntry(re);
        re = re->getNext();
    }
    // at this point, remoteConflictingIssuePart contains the conflicting remote part of the issue

    // for each local conflicting entry, do the merge
    std::list<Entry *> mergingEntries;
    while (conflictingLocalEntry) {
        Entry *e = mergeEntry(conflictingLocalEntry, remoteIssue, remoteConflictingIssuePart, pullCtx.mergeStrategy);
        if (e) mergingEntries.push_back(e);
        conflictingLocalEntry = conflictingLocalEntry->getNext();
    }
    // store to disk
    // The storage is done after all merging is complete
    // but it could perhaps be done in the loop above?
    std::list<Entry *>::const_iterator e;
    FOREACH(e, mergingEntries) {
        int r = p.storeEntry(*e);
        if (r<0) exit(1);
    }

}

/** Get a list of the entries of a remote issue
  */
int getEntriesOfRemoteIssue(const PullContext &pullCtx, const Project &p, const std::string &issueId,
                      std::list<std::string> &entries)
{
    std::string url = pullCtx.rooturl + "/" + p.getName() + "/" RESOURCE_ISSUES "/" + issueId;
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(url);
    hr.getRequestLines();
    hr.closeCurl(); // free the curl resource

    if (hr.lines.empty() || hr.lines.front().empty()) {
        return -1;
    } else if (hr.httpStatusCode != 200) {
        // may happen if remote issue does not exist
        return -2;
    }
    entries = hr.lines;
    return 0;
}

/** Clone a remote issue
  *
  * This makes the assumption that all remote entries have been
  * previously downloaded.
  */
Issue *loadRemoteIssue(const PullContext &pullCtx, Project &p, const std::string &issueId, const std::string &latestEntry)
{
    LOG_DEBUG("loadRemoteIssue of %s: %s", p.getName().c_str(), issueId.c_str());

    LOG_ERROR("loadRemoteIssue not implemented");
    exit(1);

    Issue *remoteIssue = 0; //Issue::load(p.getObjectsDir(), latestEntry);
    if (!remoteIssue) {
        fprintf(stderr, "Cannot load remote issue from latest %s\n", latestEntry.c_str());
        exit(1);
    }
    // set the id of the remote, as it is not fulfilled by "load()"
    remoteIssue->id = issueId;
    return remoteIssue;
}

/** Rename local issue if existing
  */
void renameIssueStandingInTheWay(Project &p, const std::string &issueId)
{
    IssueCopy i;
    int r = p.get(issueId, i);
    if (r == 0) {
        // Yes, another issue is in the way. Rename it.
        LOG_CLI("Local issue %s is in the way: needs renaming\n", issueId.c_str());
        std::string newId = p.renameIssue(issueId);
        if (newId.empty()) {
            fprintf(stderr, "Cannot rename issue %s\n", issueId.c_str());
            exit(1);
        }
        LOG_CLI("Local issue renamed: %s -> %s\n", issueId.c_str(), newId.c_str());
    }
}

/** Pull an issue
  *
  * This is not really a "pull" as no download occurs here.
  * All must have been downloaded previously.
  */
int pullIssue(const PullContext &pullCtx, Project &p, const std::string &remoteIssueId, const std::string &latestEntry)
{
    LOG_FUNC();
    LOG_DEBUG("Pulling %s/issues/%s", p.getName().c_str(), remoteIssueId.c_str());

    // download the remote issue
    Issue *remoteIssue = loadRemoteIssue(pullCtx, p, remoteIssueId, latestEntry);
    if (!remoteIssue) return -1;

    // Get the related local issue. 3 possible cases:
    // - no related local issue
    // - a related local issue with same identifier
    // - a related local issue with a different id

    Issue *localIssue = 0; // the local issue related to the remote issue being pulled

    std::string firstEntry = remoteIssue->first->id;
    // look if the first entry of the remote issue is known locally
    Entry *e = p.getEntry(firstEntry);
    if (e) {
        // yes, we have it locally
        localIssue = e->issue;
        if (!localIssue) {
            fprintf(stderr, "Entry %s related to null issue\n", firstEntry.c_str());
            exit(1);
        }
        if (localIssue->id != remoteIssueId) {
            // The local issue needs to be renamed and assigned the same id as the remote.
            // But first check (and rename) if another local issue occupies the id.
            LOG_CLI("%s/issues/%s needs renaming to %s (remote does not match)\n",
                   p.getName().c_str(), localIssue->id.c_str(), remoteIssueId.c_str());

            // check if another local issue is in the way
            renameIssueStandingInTheWay(p, remoteIssueId);

            // rename the local issue to the same id as the remote issue
            int r = p.renameIssue(*localIssue, remoteIssueId);
            if (r != 0) {
                fprintf(stderr, "Cannot rename issue %s -> %s\n", localIssue->id.c_str(),
                        remoteIssueId.c_str());
                exit(1);
            }
        }
    } else {
        // this entry does not exist locally
        localIssue = 0;
    }

    if (!localIssue) {
        // Local issue not existing

        // Check if a local issue with same id exists.
        renameIssueStandingInTheWay(p, remoteIssueId);

        // add the issue to the project
        int r = p.addNewIssue(*remoteIssue);
        if (r!=0) {
            LOG_ERROR("Cannot insert cloned issue: %s", remoteIssue->id.c_str());
            exit(1);
        }

    } else {

        // Local and remote issues are now aligned.
        // Walk through the entries...

        Entry *localEntry = localIssue->first;
        Entry *remoteEntry = remoteIssue->first;
        while (1) {

            if (!remoteEntry) {
                // local issue has the same entries as the remote, or more.
                // entry-pulling complete
                break;

            } else if (!localEntry) {
                // remote issue has more entries. they are already downloaded...
                // entry-pulling completed.
                int r = p.storeRefIssue(remoteIssue->id, remoteIssue->latest->id);
                if (r != 0) exit(1);

                break;

            } else if (localEntry->id != remoteEntry->id) {

                // remote: a--b--c--d
                // local:  a--b--e

                handleConflictOnEntries(pullCtx, p, localEntry, *remoteIssue);

                int r = p.storeRefIssue(remoteIssue->id, remoteIssue->latest->id);
                if (r != 0) exit(1);

                // the updated issue need not be inserted in memory
                // as the pulling of subsequent issues does need it

                break; // leave the loop.

            }

            // move the local entry pointer forward, except if already at the end
            localEntry = localEntry->getNext();
            remoteEntry = remoteEntry->getNext();
        }
    }

    delete remoteIssue;
    return 0; // ok
}

static int pullProjectViews(const PullContext &ctx, Project &p)
{
    // The object of the config itself is supposed already pulled.
    // Now pull the refs/views.

    // First, download the ref
    std::string localTmp = ctx.getTmpDir() + "/.download";
    std::string url = ctx.rooturl + "/" + p.getUrlName() + "/" + RSRC_REF_VIEWS;
    int r = HttpRequest::downloadFile(ctx.httpCtx, url, localTmp);
    if (r != 0) {
        LOG_ERROR("Cannot download project config: r=%d", r);
        return -1;
    }

    // Load the ref of the new 'views'
    std::string newid;
    r = loadFile(localTmp, newid);
    if (r != 0) {
        LOG_ERROR("Cannot load downloaded views '%s': %s", localTmp.c_str(), strerror(errno));
        return -1;
    }

    unlink(localTmp.c_str());
    trim(newid);

    // get the ref of the old 'views'
    std::string oldid;
    std::string localViews = p.getPath() + "/" PATH_VIEWS;
    r = loadFile(localViews , oldid);
    if (r != 0) {
        LOG_ERROR("Cannot load views '%s': %s", localViews.c_str(), strerror(errno));
        return -1;
    }
    trim(oldid);

    if (oldid == newid) return 0; // no change

    LOG_CLI("  new views: %s\n", newid.c_str());

    // Officialize the new 'views'
    r = writeToFile(localViews, newid);
    if (r != 0) {
        LOG_ERROR("Cannot write views ref '%s': %s", localViews.c_str(), strerror(errno));
    }

    return r;
}

static int pullPublic(const PullContext &ctx)
{
    std::string resource = "/public";
    std::string localPublicDir = ctx.localRepo + "/public";

    int n = 0;
    int r = cloneFiles(ctx, resource, localPublicDir, n, true); // overwrite local files
    if (n > 0) LOG_CLI("\n");
    return r;
}

static int pullTemplates(const PullContext &ctx, const std::string &resource, const std::string &destDir)
{
    LOG_DIAG("pullTemplates: %s", resource.c_str());

    // check first if the remote resource exists and is a directory
    std::string url = ctx.rooturl + "/" + resource;
    std::string listing;
    int r = HttpRequest::downloadInMemory(ctx.httpCtx, url, listing);

    if (1 == r) {
        // directory listing found
        // we can clone the remote template dir

        int n = 0;
        int r = cloneFiles(ctx, resource, destDir, n, true); // overwrite local files
        if (n > 0) LOG_CLI("\n");
        return r;
    }

    return -1;
}


static int pullProject(const PullContext &pullCtx, Project &p, bool doPullTemplates)
{
    LOG_CLI("Pulling project: %s\n", p.getName().c_str());

    // TODO: move the pullObjects after the pulling of remote issues
    // as it may happen that a remote file is created between the
    // moment of the pullObjects and the moment of the download of RESOURCE_ISSUES
    LOG_DEBUG("Pulling objects of %s", p.getName().c_str());
    int r = pullObjects(pullCtx, p);
    if (r < 0) return r;

    // get the remote issues
    HttpRequest hr(pullCtx.httpCtx);
    std::string url = pullCtx.rooturl + "/" + p.getUrlName() + "/" RESOURCE_ISSUES "/";
    hr.setUrl(url);
    hr.getRequestLines();
    hr.closeCurl(); // free the resource

    std::list<std::string>::iterator issueIt;
    FOREACH(issueIt, hr.lines) {
        // each line is: <issue-id> <first-entry-id> <latest-entry-id>
        std::string remoteIssueId = popToken(*issueIt, ' ');
        if (remoteIssueId.empty()) continue;
        std::string firstEntry = popToken(*issueIt, ' ');
        std::string latestEntry = *issueIt;

        pullIssue(pullCtx, p, remoteIssueId, latestEntry);
    }

    // pull project configuration
    //pullProjectConfig(pullCtx, p);

    // pull predefined views
    pullProjectViews(pullCtx, p);

    if (doPullTemplates) {
        // pull templates
        std::string resource = "/" + p.getUrlName() + "/" RSRC_SMIP "/" P_TEMPLATES;
        std::string localTemplatesDir = p.getPath() + "/" RSRC_SMIP "/" P_TEMPLATES;
        pullTemplates(pullCtx, resource, localTemplatesDir);
    }

    // pull tags
    //LOG_ERROR("Pull project tags not implemented yet");

    return 0; // ok
}


/** Pull issues of all local projects
  * - pull entries
  * - pull files
  *
  * @param all
  *     Also pull: tags, public, templates
  */
int pullProjects(const PullContext &pullCtx, bool all)
{
    // Load all local projects
    int r = dbLoad(pullCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pullCtx.localRepo.c_str());
        exit(1);
    }

    // get the list of remote projects
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(pullCtx.rooturl + "/");
    hr.getRequestLines();

    std::list<std::string>::iterator projectName;
    FOREACH(projectName, hr.lines) {
        if ((*projectName) == "public") continue;
        if ((*projectName) == PATH_REPO) continue;
        if (projectName->empty()) continue;

        // project name is mangled
        Project *p = Database::Db.getProject(*projectName);
        if (!p) {
            // the remote project was not locally cloned
            // do cloning now
            std::string resource = "/" + Project::urlNameEncode(*projectName);
            std::string destLocal = pullCtx.localRepo + "/" + *projectName;
            LOG_CLI("Pulling new project: %s\n", projectName->c_str());
            int counter = 0;
            int r = cloneFiles(pullCtx, resource, destLocal, counter, false);
            if (counter > 0) LOG_CLI("\n");
            if (r < 0) return r;
        } else {
            pullProject(pullCtx, *p, all);
        }
    }
    if (all) {

        LOG_CLI("Pulling repo templates\n");
        std::string resource = "/" PATH_REPO "/" P_TEMPLATES;
        std::string localTemplatesDir = pullCtx.localRepo + "/" PATH_REPO "/" P_TEMPLATES;
        r = pullTemplates(pullCtx, resource, localTemplatesDir);
        if (r < 0) return r;


        LOG_CLI("Pulling 'public'\n");
        r = pullPublic(pullCtx);
        if (r < 0) return r;
    }
    return 0; //ok
}

std::string getSmitDir(const std::string &dir)
{
    return dir + "/" SMIT_DIR;
}

void createSmitDir(const std::string &dir)
{
    LOG_DEBUG("createSmitDir(%s)...", dir.c_str());

    std::string smitDir = getSmitDir(dir);

    if (isDir(smitDir)) return; // Already exists

    int r = mkdir(smitDir);
    if (r != 0) {
        fprintf(stderr, "Cannot create directory '%s': %s\n", getSmitDir(dir).c_str(), strerror(errno));
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Sign-in and return the sessid cookie
  *
  * @return
  *    The format is the cookie format: key=value
  */
std::string signin(const std::string &rooturl, const std::string &user,
                   const std::string &passwd, const HttpClientContext &ctx)
{
    HttpRequest hr(ctx);

    hr.setUrl(rooturl + "/signin");

    // specify the POST data
    std::string params;
    params += "username=";
    params += urlEncode(user);
    params += "&password=";
    params += urlEncode(passwd);
    hr.post(params);

    // Get the sessiond id
    // Look for the cookie smit-sessid-*
    std::map<std::string, Cookie>::iterator c;
    FOREACH(c, hr.cookies) {
        std::string cookie = c->first; // name of the cookie
        if (0 == cookie.compare(0, strlen(COOKIE_SESSID_PREFIX), COOKIE_SESSID_PREFIX)) {
            // Got it
            cookie += "=" + c->second.value;
            return cookie;
        }
    }

    return "";
}

void parseUrl(std::string url, std::string &scheme, std::string &host, std::string &port, std::string &resource)
{
    scheme = popToken(url, ':');
    std::string hostAndPort = popToken(url, '/');
    host = popToken(hostAndPort, ':');
    port = hostAndPort;
    resource = url;
}

/** Store the cookie of the sessid in .smit/sessid
  */
void storeSessid(const std::string &dir, const std::string &sessid)
{
    LOG_DEBUG("storeSessid(%s, %s)...", dir.c_str(), sessid.c_str());
    std::string path = dir + "/" PATH_SESSID;
    int r = writeToFile(path, sessid + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Load the cookie of the sessid
  */
std::string loadSessid(const std::string &dir)
{
    std::string path = dir + "/" PATH_SESSID;
    std::string sessid;
    loadFile(path.c_str(), sessid);
    trim(sessid);

    return sessid;
}

// store url in .smit/remote
void storeUrl(const std::string &dir, const std::string &url)
{
    LOG_DEBUG("storeUrl(%s, %s)...", dir.c_str(), url.c_str());
    std::string path = dir + "/" PATH_URL;

    int r = writeToFile(path, url + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

std::string loadUrl(const std::string &dir)
{
    std::string path = dir + "/" PATH_URL;
    std::string url;
    loadFile(path.c_str(), url);
    trim(url);

    return url;
}

Args *setupCloneOptions()
{
    Args *args = new Args();
    args->setDescription("Clones a repository into a newly created directory.\n"
                         "\n"
                         "Args:\n"
                         "  <url>        The repository to clone from.\n"
                         "  <directory>  The name of a new directory to clone into.\n"
                         "\n"
                         "Example:\n"
                         "  smit clone http://example.com:8090 localDir\n"
                         );
    args->setUsage("smit clone [options] <url> <directory>");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(2);
    return args;
}

int helpClone(const Args *args)
{
    if (!args) args = setupCloneOptions();
    args->usage(true);
    return 1;
}


int cmdClone(int argc, char **argv)
{
    HttpClientContext httpCtx;
    Args *args = setupCloneOptions();
    args->parse(argc-1, argv+1);
    const char *pUsername = args->get("user");
    const char *pPasswd = args->get("passwd");
    if (args->get("insecure")) httpCtx.tlsInsecure = true;
    httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *url = args->pop();
    const char *localdir = args->pop();
    if (!url || !localdir) {
        LOG_CLI("Missing argument.\n");
        exit(1);
    }

    setLoggingOption(LO_CLI);

    if (fileExists(localdir)) {
        LOG_CLI("Cannot clone: '%s' already exists\n", localdir);
        exit(1);
    }

    std::string username;
    std::string passwd;

    if (!pUsername) username = getString("Username: ", false);
    else username = pUsername;
    if (!pPasswd) passwd = getString("Password: ", true);
    else passwd = pPasswd;

    curl_global_init(CURL_GLOBAL_ALL);

    httpCtx.cookieSessid = signin(url, username, passwd, httpCtx);

    if (httpCtx.cookieSessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        exit(1);
    }

    int r = mkdir(localdir);
    if (r != 0) {
        LOG_CLI("Cannot create directory '%s': %s\n", localdir, strerror(errno));
        exit(1);
    }

    // create persistent configuration of the local clone
    createSmitDir(localdir);
    storeSessid(localdir, httpCtx.cookieSessid);
    storeUsername(localdir, username);
    storeUrl(localdir, url);

    r = getProjects(url, localdir, httpCtx);
    if (r < 0) {
        fprintf(stderr, "Clone failed. Abort.\n");
        exit(1);
    }

    terminateSession();

    return 0;
}

/** Sign-in or reuse existing cookie
  *
  * @param username
  *    Optional. If not given, try to use the cookie.
  *
  *
  * @param ctx IN/OUT
  */
static int establishSession(const char *dir, const char *username, const char *password, PullContext &ctx, bool store)
{
    curl_global_init(CURL_GLOBAL_ALL);

    if (ctx.rooturl.empty()) {
        // get the remote root url from local configuration file
        ctx.rooturl = loadUrl(dir);
        if (ctx.rooturl.empty()) {
            LOG_CLI("Cannot get remote url.\n");
            return -1;
        }
    }

    bool redoSigin = false;

    std::string user;
    if (!username) {
        // check if the sessid is valid
        ctx.httpCtx.cookieSessid = loadSessid(dir);
        int r = testSessid(ctx.rooturl, ctx.httpCtx);

        if (r < 0) {
            // failed (session may have expired), then prompt for user name/password
            LOG_CLI("Session not valid. You must sign-in again.\n");
            user = getString("Username: ", false);

            // and redo the signing-in
            redoSigin = true;
        }

    } else {
        // do the signing-in with the given username / password
        redoSigin = true;
        user = username;
    }

    if (redoSigin) {

        std::string pass;
        if (!password) pass = getString("Password: ", true);
        else pass = password;

        ctx.httpCtx.cookieSessid = signin(ctx.rooturl, user, pass, ctx.httpCtx);
        if (!ctx.httpCtx.cookieSessid.empty() && store) {
            storeSessid(dir, ctx.httpCtx.cookieSessid);
            storeUrl(dir, ctx.rooturl);
        }
    }

    LOG_DEBUG("cookieSessid: %s", ctx.httpCtx.cookieSessid.c_str());

    if (ctx.httpCtx.cookieSessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return -1; // authentication failed
    }
    return 0;
}

Args *setupGetOptions()
{
    Args *args = new Args();
    args->setDescription("Get with format x-smit.\n");
    args->setUsage("smit get [options] <root-url> <resource>");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setOpt("use-signin-cookie", 0, "Use the cookie of the cloned repository for signin in", 0);
    args->setNonOptionLimit(2);
    return args;
}

int helpGet(const Args *args)
{
    if (!args) args = setupGetOptions();
    args->usage(true);
    return 1;
}

int cmdGet(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupGetOptions();
    args->parse(argc-1, argv+1);
    const char *username = args->get("user");
    const char *password = args->get("passwd");
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *rooturl = args->pop();
    const char *resource = args->pop();
    if (!rooturl || !resource) {
        LOG_CLI("Missing argument.\n");
        exit(1);
    }

    setLoggingOption(LO_CLI);

    pullCtx.rooturl = rooturl;
    int r = establishSession(".", username, password, pullCtx, false);
    if (r != 0) return 1;

    // do the GET
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(std::string(rooturl) + std::string(resource));
    hr.getFileStdout();

    terminateSession();

    return 0;
}

Args *setupPullOptions()
{
    Args *args = new Args();
    args->setDescription("Incorporates changes from a remote repository into a local copy.");
    args->setUsage("smit pull [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("resolve-conflict", 0,
                 "Resolve conflictual pulling automatically (no interactive choice).\n"
                 "Possible values for <policy>:\n"
                 "  keep-local : local modifications shall be kept as-is\n"
                 "  drop-local : local modifications shall be deleted"
                 ,
                 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setOpt("all", 'a', "also pull 'public' and templates", 0);
    args->setNonOptionLimit(1);
    return args;
}

int helpPull(const Args *args)
{
    if (!args) args = setupPullOptions();
    args->usage(true);
    return 1;
}

int cmdPull(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupPullOptions();
    args->parse(argc-1, argv+1);
    const char *username = args->get("user");
    const char *password = args->get("passwd");
    bool pullAll = false;
    if (args->get("all")) pullAll = true;
    const char *resolve = args->get("resolve-conflict");
    if (!resolve) pullCtx.mergeStrategy = MERGE_INTERACTIVE;
    else if (0 == strcmp(resolve, "keep-local")) pullCtx.mergeStrategy = MERGE_KEEP_LOCAL;
    else if (0 == strcmp(resolve, "drop-local")) pullCtx.mergeStrategy = MERGE_DROP_LOCAL;
    else {
        LOG_CLI("invalid value for --resolve-conflict\n");
        return helpPull(args);
    }
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    setLoggingOption(LO_CLI);

    // get the remote url from local configuration file
    std::string url = loadUrl(dir);
    if (url.empty()) {
        LOG_CLI("Cannot get remote url.\n");
        exit(1);
    }

    int r = establishSession(dir, username, password, pullCtx, true);
    if (r != 0) return 1;

    // pull new entries and new attached files of all projects
    pullCtx.rooturl = url;
    pullCtx.localRepo = dir;
    r = pullProjects(pullCtx, pullAll);

    terminateSession();

    if (r < 0) return 1;
    else return 0;
}

int getHead(const PullContext &pushCtx, const std::string &url)
{
    HttpRequest hr(pushCtx.httpCtx);
    int r = hr.head(url);
    if (r !=0 ) {
        LOG_ERROR("Could not get HEAD for: %s", url.c_str());
        exit(1);
    } else if (hr.httpStatusCode >= 200 && hr.httpStatusCode < 300) {
        return 0;
    } else {
        return -1;
    }
}

int pushFile(const PullContext &pushCtx, const std::string &localFile, const std::string &url,
             int &httpStatusCode, std::string &response)
{
    HttpRequest hr(pushCtx.httpCtx);
    int r = hr.postFile(localFile, url);
    if (r == 0 && hr.lines.size()) response = hr.lines.front();
    httpStatusCode = hr.httpStatusCode;
    return r;
}

/** Push an entry
  *
  * @param issue
  *     IN/OUT
  *     The issue id may be changed by the server when the entry is the first entry.
  *
  * @return
  *   -3   : conflict error, the entry could not be pushed
  *   <0   : other error, the entry could not be pushed
  *    0   : ok, no change for issue id
  *    1   : issue id has changed
  */
int pushEntry(const PullContext &pushCtx, const Project &p, std::string &issue,
              const std::string &entry)
{
    LOG_CLI("Pushing entry: %s / %s / %s\n", p.getName().c_str(), issue.c_str(), entry.c_str());
    // post the entry (which must be the first entry of an issue)
    // the result of the POST indicates the issue number that has been allocated
    std::string localFile = p.getObjectsDir() + '/' + Object::getSubpath(entry);
    std::string url = pushCtx.rooturl + '/' + p.getUrlName() + "/" RESOURCE_ISSUES "/" + issue + '/' + entry;
    std::string response;
    int httpStatusCode = 0;
    int r = pushFile(pushCtx, localFile, url, httpStatusCode, response);
    if (httpStatusCode == 409) {
        // conflict
        LOG_ERROR("Conflict for pushing entry of project %s: %s/%s",
                  p.getName().c_str(), issue.c_str(), entry.c_str());
        printf("Try pulling first.\n");
        exit(1);
    } else if (r != 0) {
        // error
        LOG_ERROR("Could not push entry of project %s: %s/%s", p.getName().c_str(), issue.c_str(), entry.c_str());
        exit(1);
    }

    // first line of response gives the new allocated issue id
    // format is:
    //     issue: 010203045666
    popToken(response, ':');
    trim(response);
    if (issue != response) {
        issue = response;
        return 1;
    }

    return 0;
}

/** Push the files attached to an entry
  */
void pushAttachedFiles(const PullContext &pushCtx, const Project &p, const Entry &e, bool dryRun)
{
    LOG_FUNC();
    LOG_DEBUG("Pushing files attached to entry %s", e.id.c_str());

    PropertiesIt files = e.properties.find(K_FILE);
    if (files != e.properties.end()) {
        LOG_DEBUG("Entry %s has files: %s", e.id.c_str(), toString(files->second).c_str());
        std::list<std::string>::const_iterator f;
        FOREACH(f, files->second) {
            // file is like: <object-id>/<basename>
            std::string fid = *f;
            std::string id = popToken(fid, '/');
            std::string url = pushCtx.rooturl + '/' + p.getUrlName() + "/" RESOURCE_FILES "/" + id;
            // test if the file exists on the server
            int r = getHead(pushCtx, url);
            if (r != 0) {
                // file does not exist on the server
                // push the file

                if (dryRun) {
                    LOG_CLI("[DRY-RUN] Pushing file %s...\n", f->c_str());
                    continue;
                }

                LOG_CLI("Pushing file %s...\n", f->c_str());
                std::string localPath = p.getObjectsDir() + "/" + Object::getSubpath(id);
                std::string url = pushCtx.rooturl + '/' + p.getUrlName() + "/" RESOURCE_FILES "/" + id;
                std::string response;
                int httpStatusCode = 0;
                int r = pushFile(pushCtx, localPath, url, httpStatusCode, response);

                if (r != 0) {
                    LOG_ERROR("Cannot push file: %s (HTTP %d)", url.c_str(), httpStatusCode);
                    exit(1);
                }
            }
        }
    } // else no files attached to this entry
}

/** Push an issue
  *
  * @param dryRun
  *     If true, do not really push but show what should be pushed.
  */
int pushIssue(const PullContext &pushCtx, Project &project, Issue localIssue, bool dryRun)
{
    LOG_DEBUG("pushIssue %s: %s", project.getName().c_str(), localIssue.id.c_str());

    // - get list of entries of the same remote issue
    //    + if no such remote issue, push
    //    + if discrepancy (first entries do not match), abort and ask the user to pull first
    //    + else, push the local entries missing on the remote side
    Entry *localEntry = localIssue.first;
    if (!localEntry) {
        LOG_ERROR("%s: null first entry for issue %s", project.getName().c_str(),
                  localIssue.id.c_str());
        exit(1);
    }
    const Entry &firstEntry = *localEntry;
    std::list<std::string> remoteEntries;
    int r = getEntriesOfRemoteIssue(pushCtx, project, localIssue.id, remoteEntries);
    if (r != 0) {
        // no such remote issue

        if (dryRun) {
            LOG_CLI("[DRY-RUN] Pushing issue: %s / %s / %s\n", project.getName().c_str(),
                    localIssue.id.c_str(), firstEntry.id.c_str());

            return 0;
        }

        // push first entry
        std::string issueId = localIssue.id;
        r = pushEntry(pushCtx, project, issueId, firstEntry.id);

        if (r > 0) {
            // issue was renamed, update local project
            LOG_CLI("%s: Issue renamed: %s -> %s\n", project.getName().c_str(),
                    localIssue.id.c_str(), issueId.c_str());
            renameIssueStandingInTheWay(project, issueId);
            r = project.renameIssue(localIssue, issueId);
            if (r != 0) {
                fprintf(stderr, "Cannot rename issue %s -> %s\n", localIssue.id.c_str(),
                        issueId.c_str());
                exit(1);

            }
        } else if (r < 0) {
            LOG_ERROR("Failed to push entry %s/issues/%s/%s", project.getName().c_str(),
                      localIssue.id.c_str(), firstEntry.id.c_str());
            exit(1);
        }

        // recurse
        return pushIssue(pushCtx, project, localIssue, dryRun);

    } else if (remoteEntries.empty()) {
        // internal error, should not happen
        LOG_ERROR("%s: error, empty entries for remote issue %s", project.getName().c_str(),
                localIssue.id.c_str());
        exit(1);

    } else {
        // the remote issue does exist

        // check if first entries match
        if (remoteEntries.front() != firstEntry.id) {
            LOG_CLI("%s: mismatch of first entries for issue %s: %s <> %s\n", project.getName().c_str(),
                   localIssue.id.c_str(), remoteEntries.front().c_str(), firstEntry.id.c_str());
            LOG_CLI("Try pulling first to resolve\n");
            exit(1);
        }

        // the remote and local issues are the same (same first entry)

        std::list<std::string>::iterator remoteEntryIt = remoteEntries.begin();
        // walk through entries and push missing ones
        while (localEntry) {

            if (remoteEntryIt == remoteEntries.end()) {
                // the remote issue has less entries than the local side

                if (dryRun) {
                    LOG_CLI("[DRY-RUN] Pushing entry: %s / %s / %s\n", project.getName().c_str(),
                            localIssue.id.c_str(), localEntry->id.c_str());
                    r = 0;

                } else {
                    // push the local entry to the remote side
                    r = pushEntry(pushCtx, project, localIssue.id, localEntry->id);
                }

                if (r > 0) {
                    // the issue was renamed. this should not happen.
                    LOG_ERROR("pushEntry returned %d: localEntry=%s", r, localEntry->id.c_str());
                    exit(1);
                } else if (r < 0) {
                    LOG_ERROR("Could not push local entry %s", localEntry->id.c_str());
                    exit(1);
                } else {
                    // ok
                }
            } else {
                // check that local and remote entries are aligned
                std::string remoteEntryId = *remoteEntryIt;

                if (localEntry->id == remoteEntryId) {
                    // ok, still aligned
                } else {
                    // error, not aligned
                    LOG_ERROR("Remote issue not pulled. Try pulling before pushing.");
                    exit(1);
                }
                remoteEntryIt++;
            }
            localEntry = localEntry->getNext();
        }
    }

    // push attached files of this issue
    const Entry *e = localIssue.first;
    while (e) {
        pushAttachedFiles(pushCtx, project, *e, dryRun);
        e = e->getNext();
    }

    return 0;
}

int pushProject(const PullContext &pushCtx, Project &project, bool dryRun)
{
    LOG_CLI("Pushing project %s...\n", project.getName().c_str());

    // First, get a linear list of all issues
    // (we need a fixed list of the issues, and not a map, because some issues may be renamed)
    std::vector<Issue*> issues;
    project.getAllIssues(issues);
    std::vector<Issue*>::iterator i;
    FOREACH(i, issues) {
        int r = pushIssue(pushCtx, project, **i, dryRun);
        if (r != 0) return r;
    }
    return 0;
}

int pushProjects(const PullContext &pushCtx, bool dryRun)
{
    // Load all local projects
    int r = dbLoad(pushCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pushCtx.localRepo.c_str());
        exit(1);
    }

    // for each local project, push it

    Project *p = Database::Db.getNextProject(0);
    while (p) {
        pushProject(pushCtx, *p, dryRun);
        p = Database::Db.getNextProject(p);
    }
    return 0;
}

Args *setupPushOptions()
{
    Args *args = new Args();
    args->setDescription("Push local changes to a remote repository.");
    args->setUsage("smit push [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("dry-run", 'n', "do not push, but show what would have been pushed", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(1);
    return args;
}

int helpPush(const Args *args)
{
    if (!args) args = setupPushOptions();
    args->usage(true);
    return 1;
}

int cmdPush(int argc, char **argv)
{
    PullContext pushCtx;

    Args *args = setupPushOptions();
    args->parse(argc-1, argv+1);

    const char *username = args->get("user");
    const char *password = args->get("passwd");
    if (args->get("insecure")) pushCtx.httpCtx.tlsInsecure = true;
    pushCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }
    bool dryRun = false;
    if (args->get("dry-run")) dryRun = true;

    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    const char *unexpected = args->pop();
    if (unexpected) {
        // normally already managed by the Args class
        LOG_CLI("Too many arguments.\n\n");
        return helpPush(args);
    }

    setLoggingOption(LO_CLI);
    // set log level to hide INFO logs

    int r = establishSession(dir, username, password, pushCtx, true);
    if (r != 0) return 1;

    // push new entries and new attached files of all projects
    pushCtx.localRepo = dir;
    r = pushProjects(pushCtx, dryRun);

    terminateSession();

    if (r < 0) return 1;
    else return 0;


}
