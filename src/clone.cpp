/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
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

#include "clone.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"
#include "console.h"
#include "filesystem.h"
#include "db.h"
#include "logging.h"
#include "httpClient.h"
#include "Args.h"


/** smit distributed functions
  *     smit clone .... : download a local copy of a smit repository
  *     smit pull ....  : download latest changes from a smit server
  *     smit push ....  : upload local changes to a smit server
  *
  * 1. Conflicts on existing issues
  *
  * These conflicts occur typically when a user modifies an issue on the server and
  * another user modifies the same issue on his/her local clone.
  * These conflicts are detected during a pushing, but they can be resolved only
  * during a pulling.
  * These conflicts are resolved either automatically or interactively.
  *
  *
  * 2. Conflicts on new issues
  * These conflicts occur typically when a user creates a new issue on the server and
  * another user creates a new issue on his/her local clone, and both new issues get
  * the same issue id.
  *
  * These conflicts are resolved during a pulling. The local issue of the clone is
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

bool Verbose = false;

#define LOGV(...) do { if (Verbose) { printf(__VA_ARGS__); fflush(stdout);} } while (0)

int helpClone()
{
    printf("Usage: smit clone [options] <url> <directory>\n"
           "\n"
           "  Clone a smit repository into a new directory.\n"
           "\n"
           "Options:\n"
           "  <url>        The (possibly remote) repository to clone from.\n"
           "  <directory>  The name of a new directory to clone into. Cloning into an\n"
           "               existing directory is only allowed if the directory is empty.\n"
           "\n"
           "  --user <user> --passwd <password> \n"
           "      Give the user name and the password.\n"
           "\n"
           "TLS Options:\n"
           "  --cacert <CA-certificate>\n"
           "      CA certificate, in PEM format, relevant only when the server runs TLS.\n"
           "\n"
           "  --insecure\n"
           "      Do not verify the server certificate against the local CA certificates."
           "\n"
           "Example:\n"
           "  smit clone http://example.com:8090 localDir\n"
           "\n"
           );
    return 1;
}



int testSessid(const std::string url, const HttpClientContext &ctx)
{
    LOGV("testSessid(%s, %s)\n", url.c_str(), ctx.sessid.c_str());

    HttpRequest hr(ctx);
    hr.setUrl(url, "/");
    int status = hr.test();
    LOGV("status = %d\n", status);

    if (status == 200) return 0;
    return -1;

}

/** Get all the projects that we have read-access to.
  */
int getProjects(const std::string &rooturl, const std::string &destdir, const HttpClientContext &ctx)
{
    LOGV("getProjects(%s, %s, %s)\n", rooturl.c_str(), destdir.c_str(), ctx.sessid.c_str());

    HttpRequest hr(ctx);
    hr.setUrl(rooturl, "/");
    hr.setRepository(destdir);
    hr.doCloning(true, 0);

    return 0;
}

// resolve strategies for pulling conflicts
enum MergeStrategy { MERGE_KEEP_LOCAL, MERGE_DROP_LOCAL, MERGE_INTERACTIVE};
struct PullContext {
    std::string rooturl; // eg: http://example.com:8090/
    std::string localRepo; // path to local repository
    HttpClientContext httpCtx; // session identifier
    MergeStrategy mergeStrategy; // for pulling only, not cloning
};

/** Download entries, starting at the given iterator
  *
  * @param localDir
  *    Specifies the location where the entries should be downloaded.
  *    If empty, then the default location is used inside the local repository.
  *
  */
void downloadEntries(const PullContext &pullCtx, const Project &p, const std::string &issueId,
                     const std::list<std::string> &remoteEntries, std::list<std::string>::const_iterator reid,
                     const std::string &localDir)
{
    LOGV("downloadEntries: %s ...\n", reid->c_str());
    for ( ; reid != remoteEntries.end(); reid++) {
        if (reid->empty()) continue; // directory listing may end by an empty line
        std::string remoteEid = *reid;
        std::string resourceDir = "/" + p.getUrlName() + "/issues/" + issueId;
        std::string resource = resourceDir + "/" + remoteEid;

        HttpRequest hr(pullCtx.httpCtx);
        hr.setUrl(pullCtx.rooturl, resource);

        std::string downloadDir = localDir;
        if (downloadDir.empty()) downloadDir = pullCtx.localRepo + resourceDir;

        // TODO download to tmp storage, then move (in order to have an atomic dowload)
        std::string localEntryFile = downloadDir + "/" + remoteEid;
        hr.downloadFile(localEntryFile);
    }
}

/** Merge a local entry into a remote branch (downloaded locally)
  *
  * @param remoteIssue
  *      in/out: The remote issue instance is updated by the merging
  */
void mergeEntry(const Entry *localEntry, Issue &remoteIssue, const Issue &remoteConflictingIssuePart, MergeStrategy ms)
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

            LOGV("Merge conflict: %s", propertyName.c_str());
            // there is a conflict: the remote side has changed this property,
            // but with a different value than the local entry
            isConflicting = true;

            if (ms == MERGE_INTERACTIVE) {
                std::list<std::string> remoteValue = remoteProperty->second;
                printf("-- Conflict on issue %s: %s\n", remoteIssue.id.c_str(), remoteIssue.getSummary().c_str());
                printf("Remote: %s => %s\n", propertyName.c_str(), toString(remoteValue).c_str());
                printf("Local : %s => %s\n", propertyName.c_str(), toString(localValue).c_str());
                std::string response;
                while (response != "l" && response != "L" && response != "r" && response != "R") {
                    printf("Select: (l)ocal, (r)emote: ");
                    std::cin >> response;
                }
                if (response == "l" || response == "L") {
                    // keep the local property
                    newProperties[propertyName] = localValue;
                } else {
                    // drop the local property
                    LOGV("Local property dropped.");
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
            printf("Local message:\n");
            printf("--------------------------------------------------\n");
            printf("%s\n", localEntry->getMessage().c_str());
            printf("--------------------------------------------------\n");
            std::string response;
            while (response != "k" && response != "K" && response != "d" && response != "D") {
                printf("Select: (k)eep message, (d)rop message: ");
                std::cin >> response;
            }
            if (response == "k" || response == "K") {
                // keep the message
                newProperties[K_MESSAGE].push_back(localEntry->getMessage());
            }
        } else if (isConflicting && ms == MERGE_DROP_LOCAL) {
            // drop the message
            LOGV("Local message dropped.");
        } else {
            // no conflict on the properties. keep the message unchanged
            newProperties[K_MESSAGE].push_back(localEntry->getMessage());
        }
    }

    // check if this new entry must be kept
    if (newProperties.size() > 0) {
        // store the new entry
        Entry *e = remoteIssue.addEntry(newProperties, localEntry->author);
        if (!e) {
            fprintf(stderr, "Abort.");
            exit(1);
        }
        printf("New entry: %s\n", e->id.c_str());
    }
}

/** Download the entries of remote issue
  */
std::string downloadRemoteIssue(const PullContext &pullCtx, Project &p, const std::string &issueId,
                                const std::list<std::string> &remoteEntries)
{
    // clone the issue to a temporary directory and download the remote entries

    // clean up if previous aborted merging left a such temporary dir
    std::string tmpPath = p.getTmpDir() + "/" + issueId;
    LOGV("Removing directory: %s\n", tmpPath.c_str());
    int r = removeDir(tmpPath);
    if (r<0) exit(1);

    // create the tmp dir
    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
    r = mg_mkdir(p.getTmpDir().c_str(), mode);
    // if it could not be created because it already exists, ok, not an error
    // if it could not be created for another reason, the following mkdir will fail
    // if no error, ok, continue.

    r = mg_mkdir(tmpPath.c_str(), mode);
    if (r != 0) {
        fprintf(stderr, "Cannot create tmp directory '%s': %s\n", tmpPath.c_str(), strerror(errno));
        fprintf(stderr, "Abort.\n");
        exit(1);
    }

    // download all the remaining remote entries
    downloadEntries(pullCtx, p, issueId, remoteEntries, remoteEntries.begin(), tmpPath);
    return tmpPath;
}

/** Manage the merging of conflicting entries of an issue
  *
  */
void handleConflictOnEntries(const PullContext &pullCtx, Project &p,
                             const Issue &localIssue, const Entry *conflictingLocalEntry,
                             Issue &remoteIssue)
{
    // remote: a--b--c--d
    // local:  a--b--e
    // remote issue has conflicting entries. download them locally in a separate directory

    // compute the remote part of the issue that is conflicting with local entries
    std::string commonParent = conflictingLocalEntry->parent;
    // look for this parent in the remote issue
    Entry *re = remoteIssue.latest;
    while (re && re->prev) re = re->prev; // rewind
    while (re && re->id != commonParent) re = re->next;
    if (!re) {
        fprintf(stderr, "Cannot find remote common parent in locally downloaded issue: %s\n", commonParent.c_str());
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
    Issue remoteConflictingIssuePart;
    re = re->next; // start with the first conflicting entry
    while (re) {
        remoteConflictingIssuePart.consolidateIssueWithSingleEntry(re, true);
        re = re->next;
    }
    // at this point, remoteConflictingIssuePart contains the conflicting remote part of the issue

    // for each local conflicting entry, do the merge
    while (conflictingLocalEntry) {
        mergeEntry(conflictingLocalEntry, remoteIssue, remoteConflictingIssuePart, pullCtx.mergeStrategy);
        conflictingLocalEntry = conflictingLocalEntry->next;
    }

    // move the merge issue from tmp to official storage
    p.officializeMerging(remoteIssue);
    // no need to update the issue in memory, as it will not be re-accessed during the smit pulling
}

/** Get a list of the entries of a remote issue
  */
int getEntriesOfIssue(const PullContext &pullCtx, const Project &p, const Issue &i,
                      std::list<std::string> &entries)
{
    std::string resource = "/" + p.getUrlName() + "/issues/" + i.id;
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(pullCtx.rooturl, resource);
    hr.getRequestLines();
    hr.closeCurl(); // free the curl resource

    if (hr.lines.empty() || hr.lines.front().empty()) {
        // may happen if remote issue does not exist
        return -1;
    }
    return 0;
}

/** Pull an issue
  *
  * If the remote issue conflicts with a local issue:
  * - rename the local issue (thus changing its id)
  * - clone the remote issue
  */
int pullIssue(const PullContext &pullCtx, Project &p, const Issue &i)
{
    // compare the remote and local issue
    // get the first entry of the issue
    // check conflict on issue id

    // get the entries of the remote issue
    std::list<std::string> remoteEntries;
    int r = getEntriesOfIssue(pullCtx, p, i, remoteEntries);
    if (r != 0) {
        fprintf(stderr, "Cannot get entries of remote issue %s", i.id.c_str());
        exit(1);
    }

    // get first entry of local issue
    const Entry *e = i.latest;
    while (e && e->prev) e = e->prev; // rewind to the first entry
    if (!e) {
        // should not happen
        fprintf(stderr, "Got local issue with no first entry: %s", i.id.c_str());
        exit(1);
    }

    const Entry *localEntry = e;

    if (localEntry->id != remoteEntries.front())  {
        // the remote issue and the local issue are not the same
        // the local issue has to be renamed
        LOGV("Issue %s: local (%s) and remote (%s) diverge", i.id.c_str(),
             localEntry->id.c_str(), remoteEntries.front().c_str());
        // propose to the user a new id for the issue

        printf("Issue conflicting with remote: %s %s\n", i.id.c_str(), i.getSummary().c_str());
        std::string newId = p.renameIssue(i.id);
        if (newId.empty()) {
            fprintf(stderr, "Cannot rename issue %s. Aborting", i.id.c_str());
            exit(1);
        }

        // inform the user
        printf("Local issue %s renamed %s (%s)\n", i.id.c_str(), newId.c_str(), i.getSummary().c_str());

        // clone the remote issue
        std::string resource = "/" + p.getUrlName() + "/issues/" + i.id;
        HttpRequest hr(pullCtx.httpCtx);
        hr.setUrl(pullCtx.rooturl, resource);
        hr.setRepository(pullCtx.localRepo);
        hr.doCloning(true, 0);

    } else {
        // same issue. Walk through the entries and pull...

        std::list<std::string>::const_iterator reid;
        FOREACH(reid, remoteEntries) {
            std::string remoteEid = *reid;
            if (remoteEid.empty()) continue; // ignore (usually last item in the directory listing)

            if (!localEntry) {
                // remote issue has more entries. download them locally

                downloadEntries(pullCtx, p, i.id, remoteEntries, reid, "");

                break; // leave the loop as all the remaining remotes have been managed by downloadEntries

            } else if (localEntry->id != remoteEid) {

                // remote: a--b--c--d
                // local:  a--b--e

                std::string tmpPath = downloadRemoteIssue(pullCtx, p, i.id, remoteEntries);
                // load this remote issue in memory
                Issue remoteIssue;
                int r = remoteIssue.load(i.id, tmpPath);
                if (r != 0) {
                    fprintf(stderr, "Cannot load downloaded issue: %s\n", tmpPath.c_str());
                    fprintf(stderr, "Abort.\n");
                    exit(1);

                }

                handleConflictOnEntries(pullCtx, p, i, localEntry, remoteIssue);

                break; // leave the loop.


            } // else nothing to do: local and remote still aligned

            // move the local entry pointer forward, except if already at the end
            if (localEntry) localEntry = localEntry->next;
        }
    }

    return 0; // ok
}

void pullAttachedFiles(const PullContext &pullCtx, Project &p)
{
    // get the remote files
    HttpRequest hr(pullCtx.httpCtx);
    std::string resourceFilesDir = "/" + p.getUrlName() + "/" K_UPLOADED_FILES_DIR "/";
    hr.setUrl(pullCtx.rooturl, resourceFilesDir);
    hr.getRequestLines();
    hr.closeCurl(); // free the resource

    std::string localFilesDir = pullCtx.localRepo + "/" + p.getUrlName() + "/" K_UPLOADED_FILES_DIR "/";

    std::list<std::string>::iterator fileIt;
    FOREACH(fileIt, hr.lines) {
        std::string filename = *fileIt;
        if (filename.empty()) continue;
        LOGV("Remote: %s/%s\n", resourceFilesDir.c_str(), filename.c_str());

        std::string localPath = localFilesDir + filename;
        if (fileExists(localPath)) {
            LOGV("File already exists locally: %s", localPath.c_str());
            continue;
        }

        // download the file
        printf("%s\n", filename.c_str());
        std::string resource = resourceFilesDir + filename;
        HttpRequest hr(pullCtx.httpCtx);
        hr.setUrl(pullCtx.rooturl, resource);
        std::string localTmpFile = p.getTmpDir() + "/" + filename;
        mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
        mg_mkdir(p.getTmpDir().c_str(), mode);
        hr.downloadFile(localTmpFile);
        hr.closeCurl(); // free the resource

        // move the file at the right place
        int r = rename(localTmpFile.c_str(), localPath.c_str());
        if (r != 0) {
            LOG_ERROR("Cannot mv file '%s' -> '%s': %s", localTmpFile.c_str(), localPath.c_str(), strerror(errno));
            exit(1);
        }
    }
}


int pullProject(const PullContext &pullCtx, Project &p)
{
    // get the remote issues
    HttpRequest hr(pullCtx.httpCtx);
    std::string resource = "/" + p.getUrlName() + "/issues/";
    hr.setUrl(pullCtx.rooturl, resource);
    hr.getRequestLines();
    hr.closeCurl(); // free the resource

    std::list<std::string>::iterator issueId;
    FOREACH(issueId, hr.lines) {
        std::string id = *issueId;
        if (id.empty()) continue;
        LOGV("Remote: %s/issues/%s\n", p.getName().c_str(), id.c_str());

        // get the issue with same id in local repository
        Issue i;
        int r = p.get(id, i);

        // check if the pulled remote issue is not already
        // locally under another issue id (this may happen
        // if a previous push was interrupted)

        // TODO


        if (r < 0) {
            // simply clone the remote issue
            LOGV("Cloning issue: %s/issues/%s\n", p.getName().c_str(), id.c_str());
            resource = "/" + p.getUrlName() + "/issues/" + id;
            HttpRequest hr(pullCtx.httpCtx);
            hr.setUrl(pullCtx.rooturl, resource);
            hr.setRepository(pullCtx.localRepo);
            hr.doCloning(true, 0);
        } else {
            LOGV("Pulling issue: %s/issues/%s\n", p.getName().c_str(), id.c_str());
            pullIssue(pullCtx, p, i);
        }
    }

    // pull attached files
    pullAttachedFiles(pullCtx, p);

    return 0; // ok
}


/** Pull issues of all local projects
  * - pull entries
  * - pull files
  *
  * Things not pulled: tags, views, project config, files in html, public, etc.
  */
int pullProjects(const PullContext &pullCtx)
{
    // set log level to hide INFO logs
    setLoggingLevel(LL_ERROR);

    // Load all local projects
    int r = dbLoad(pullCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pullCtx.localRepo.c_str());
        exit(1);
    }

    // get the list of remote projects
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(pullCtx.rooturl, "/");
    hr.getRequestLines();

    std::list<std::string>::iterator projectName;
    FOREACH(projectName, hr.lines) {
        if ((*projectName) == "public") continue;
        if ((*projectName) == "users") continue;
        if (projectName->empty()) continue;

        printf("Pulling entries of project: %s...\n", projectName->c_str());
        // project name is mangled
        std::string unmangledName = Project::urlNameDecode(*projectName);
        Project *p = Database::Db.getProject(unmangledName);
        if (!p) {
            // the remote project was not locally cloned
            // do cloning now
            printf("Cloning project: %s\n", unmangledName.c_str());
            HttpRequest hr(pullCtx.httpCtx);
            std::string resource = *projectName;
            resource = "/" + resource;
            hr.setUrl(pullCtx.rooturl, resource);
            hr.setRepository(pullCtx.localRepo);
            hr.doCloning(true, 0);
        } else {
            pullProject(pullCtx, *p);
        }
    }
    return 0; //ok
}

std::string getSmitDir(const std::string &dir)
{
    return dir + "/" SMIT_DIR;
}

void createSmitDir(const std::string &dir)
{
    LOGV("createSmitDir(%s)...\n", dir.c_str());

    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
    int r = mg_mkdir(getSmitDir(dir).c_str(), mode);
    if (r != 0) {
        fprintf(stderr, "Cannot create directory '%s': %s\n", getSmitDir(dir).c_str(), strerror(errno));
        fprintf(stderr, "Abort.\n");
        exit(1);
    }

}
// store sessid in .smit/sessid
void storeSessid(const std::string &dir, const std::string &sessid)
{
    LOGV("storeSessid(%s, %s)...\n", dir.c_str(), sessid.c_str());
    std::string path = dir + "/" PATH_SESSID;
    int r = writeToFile(path.c_str(), sessid + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

void storeUsername(const std::string &dir, const std::string &username)
{
    LOGV("storeUsername(%s, %s)...\n", dir.c_str(), username.c_str());
    std::string path = dir + "/" PATH_USERNAME;
    int r = writeToFile(path.c_str(), username + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}
std::string loadUsername(const std::string &clonedRepo)
{
    std::string path = clonedRepo + "/" PATH_USERNAME;
    std::string username;
    int r = loadFile(path.c_str(), username);
    if (r < 0) {
        username = "Anonymous";
        fprintf(stderr, "Cannot load username. Set '%s'\n", username.c_str());
    }
    return username;
}

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
    LOGV("storeUrl(%s, %s)...\n", dir.c_str(), url.c_str());
    std::string path = dir + "/" PATH_URL;

    int r = writeToFile(path.c_str(), url + "\n");
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


std::string signin(const std::string &rooturl, const std::string &user,
                   const std::string &passwd, const HttpClientContext &ctx)
{
    HttpRequest hr(ctx);

    hr.setUrl(rooturl, "/signin");

    // specify the POST data
    std::string params;
    params += "username=";
    params += urlEncode(user);
    params += "&password=";
    params += urlEncode(passwd);
    hr.post(params);

    // get the sessiond id
    std::string sessid;
    std::map<std::string, Cookie>::iterator c;
    c = hr.cookies.find(SESSID);
    if (c != hr.cookies.end()) sessid = c->second.value;

    return sessid;
}

int cmdClone(int argc, char * const *argv)
{
    std::string username;
    const char *passwd = 0;
    const char *url = 0;
    const char *dir = 0;
    HttpClientContext ctx;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"user", 1, 0, 0},
        {"passwd", 1, 0, 0},
        {"insecure", 0, 0, 0},
        {"cacert", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "v", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            if (0 == strcmp(longOptions[optionIndex].name, "user")) {
                username = optarg;
            } else if (0 == strcmp(longOptions[optionIndex].name, "passwd")) {
                passwd = optarg;
            } else if (0 == strcmp(longOptions[optionIndex].name, "insecure")) {
                ctx.tlsInsecure = true;
            } else if (0 == strcmp(longOptions[optionIndex].name, "cacert")) {
                ctx.tlsCacert = optarg;
            }
            break;
        case 'v':
            Verbose = true;
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpClone();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpClone();
        }
    }
    // manage non-option ARGV elements
    if (optind < argc) {
        url = argv[optind];
        optind++;
    }
    if (optind < argc) {
        dir = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpClone();
    }

    if (!url || !dir) {
        printf("You must specify a repository to clone and a directory.\n\n");
        return helpClone();
    }

    if (username.empty()) username = getString("Username: ", false);

    std::string password;

    if (passwd) password = passwd;
    else password = getString("Password: ", true);

    setLoggingOption(LO_CLI);

    curl_global_init(CURL_GLOBAL_ALL);

    std::string sessid = signin(url, username, password, ctx);
    LOGV("%s=%s\n", SESSID, sessid.c_str());

    if (sessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return 1; // authentication failed
    }
    ctx.sessid = sessid;

    getProjects(url, dir, ctx);

    // ceate persistent configuration of the local clone
    createSmitDir(dir);
    storeSessid(dir, ctx.sessid);
    storeUsername(dir, username);
    storeUrl(dir, url);

    curl_global_cleanup();

    return 0;
}

int helpGet()
{
    printf("Usage: smit get <root-url> <resource>\n"
           "\n"
           "  Get with format x-smit.\n"
           "\n"
           "Options:\n"
           "  --user <user> --passwd <password> \n"
           "               Give the user name and the password.\n"
           "  --use-signin-cookie\n"
           "               Use the cookie of the cloned repository for signin in.\n"
           "\n"
           );
    return 1;
}

int cmdGet(int argc, char * const *argv)
{
    std::string rooturl;
    std::string resource;
    std::string username;
    const char *passwd = 0;
    bool useSigninCookie = false;
    HttpClientContext ctx;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"use-signin-cookie", 0, 0, 0},
        {"user", 1, 0, 0},
        {"passwd", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "v", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            if (0 == strcmp(longOptions[optionIndex].name, "user")) {
                username = optarg;
            } else if (0 == strcmp(longOptions[optionIndex].name, "passwd")) {
                passwd = optarg;
            } else if (0 == strcmp(longOptions[optionIndex].name, "use-signin-cookie")) {
                useSigninCookie = true;
            }
            break;
        case 'v':
            Verbose = true;
            HttpRequest::setVerbose(true);
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpGet();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpGet();
        }
    }

    // manage non-option ARGV elements
    if (optind < argc) {
        rooturl = argv[optind];
        optind++;
    }
    if (optind < argc) {
        resource = argv[optind];
        optind++;
    }

    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpGet();
    }
    if (rooturl.empty()) {
        printf("Missing url.\n");
        return helpGet();
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::string sessid;
    std::string password;
    bool redoSigin = false;

    if (useSigninCookie) {
        // check if the sessid is valid
        ctx.sessid = loadSessid(".");
        int r = testSessid(rooturl, ctx);

        if (r < 0) {
            // failed (session may have expired), then prompt for user name/password
            username = getString("Username: ", false);
            password = getString("Password: ", true);

            // and redo the signing-in
            redoSigin = true;
        }

    } else if (username.size() && passwd){
        // do the signing-in with the given username / password
        password = passwd;
        redoSigin = true;
    }

    if (redoSigin) {
        ctx.sessid = signin(rooturl, username, password, ctx);
        if (sessid.empty()) {
            fprintf(stderr, "Authentication failed\n");
            return 1; // authentication failed
        }
    }

    LOGV("%s=%s\n", SESSID, ctx.sessid.c_str());

    // pull new entries and new attached files of all projects
    HttpRequest hr(ctx);
    hr.setUrl(rooturl, resource);
    hr.getFileStdout();

    curl_global_cleanup();

    return 0;
}

int helpPull()
{
    printf("Usage: smit pull [<local-repository>]\n"
           "\n"
           "  Incorporates changes from a remote repository into the local copy.\n"
           "\n"
           "Options:\n"
           "  --user <user> --passwd <password> \n"
           "      Specify the user name and the password.\n"
           "\n"
           "  --resolve-conflict <policy>\n"
           "      Resolve conflictual pulling automatically (no interactive choice).\n"
           "      Possible values for <policy>:\n"
           "        keep-local : local modifications shall be kept as-is\n"
           "        drop-local : local modifications shall be deleted\n"
           "\n"
           "TLS Options:\n"
           "  --cacert <CA-certificate>\n"
           "      CA certificate, in PEM format, relevant only when the server runs TLS.\n"
           "\n"
           "  --insecure\n"
           "      Do not verify the server certificate against the local CA certificates."
           "\n"
           );
    return 1;
}


int cmdPull(int argc, char * const *argv)
{
    std::string username;
    const char *passwd = 0;
    const char *dir = "."; // default value is current directory

    int c;
    int optionIndex = 0;
    PullContext pullCtx;
    pullCtx.mergeStrategy = MERGE_INTERACTIVE;

    struct option longOptions[] = {
        {"user", 1, 0, 0},
        {"passwd", 1, 0, 0},
        {"resolve-conflict", 1, 0, 0},
        {"insecure", 0, 0, 0},
        {"cacert", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "v", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            if (0 == strcmp(longOptions[optionIndex].name, "user")) {
                username = optarg;
            } else if (0 == strcmp(longOptions[optionIndex].name, "passwd")) {
                passwd = optarg;
            } else if (0 == strcmp(longOptions[optionIndex].name, "resolve-conflict")) {
                if (0 == strcmp(optarg, "keep-local")) pullCtx.mergeStrategy = MERGE_KEEP_LOCAL;
                else if (0 == strcmp(optarg, "drop-local")) pullCtx.mergeStrategy = MERGE_DROP_LOCAL;
                else {
                    printf("invalid value for --resolve-conflict\n");
                    return helpPull();
                }
            } else if (0 == strcmp(longOptions[optionIndex].name, "insecure")) {
                pullCtx.httpCtx.tlsInsecure = true;
            } else if (0 == strcmp(longOptions[optionIndex].name, "cacert")) {
                pullCtx.httpCtx.tlsCacert = optarg;
            }
            break;
        case 'v':
            Verbose = true;
            HttpRequest::setVerbose(true);
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpPull();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpPull();
        }
    }
    // manage non-option ARGV elements
    if (optind < argc) {
        dir = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpPull();
    }

    setLoggingOption(LO_CLI);

    // get the remote url from local configuration file
    std::string url = loadUrl(dir);
    if (url.empty()) {
        printf("Cannot get remote url.\n");
        exit(1);
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::string password;
    bool redoSigin = false;

    if (username.empty()) {
        // check if the sessid is valid
        pullCtx.httpCtx.sessid = loadSessid(dir);
        int r = testSessid(url, pullCtx.httpCtx);

        if (r < 0) {
            // failed (session may have expired), then prompt for user name/password
            username = getString("Username: ", false);
            password = getString("Password: ", true);

            // and redo the signing-in
            redoSigin = true;
        }

    } else {
        // do the signing-in with the given username / password
        password = passwd;
        redoSigin = true;
    }

    if (redoSigin) {
        pullCtx.httpCtx.sessid = signin(url, username, password, pullCtx.httpCtx);
        if (!pullCtx.httpCtx.sessid.empty()) {
            storeSessid(dir, pullCtx.httpCtx.sessid);
            storeUrl(dir, url);
        }
    }

    LOGV("%s=%s\n", SESSID, pullCtx.httpCtx.sessid.c_str());

    if (pullCtx.httpCtx.sessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return 1; // authentication failed
    }

    // pull new entries and new attached files of all projects
    pullCtx.rooturl = url;
    pullCtx.localRepo = dir;
    int r = pullProjects(pullCtx);

    curl_global_cleanup();

    if (r < 0) return 1;
    else return 0;


}

int pushFile(const PullContext &pushCtx, const std::string localFile, const std::string url, std::string &response)
{
    HttpRequest hr(pushCtx.httpCtx);
    int r = hr.postFile(localFile, url);
    response = hr.lines;
    return r;
}

int pushAttachedFiles(const PullContext &pushCtx, const Project &p)
{
    // get the remote files
    HttpRequest hr(pushCtx.httpCtx);
    std::string resourceFilesDir = "/" + p.getUrlName() + "/" K_UPLOADED_FILES_DIR "/";
    hr.setUrl(pushCtx.rooturl, resourceFilesDir);
    hr.getRequestLines();
    hr.closeCurl(); // free the resource

    // for each local file, if not existing remotely, the upload it
    std::string localFilesDir = pushCtx.localRepo + "/" + p.getUrlName() + "/" K_UPLOADED_FILES_DIR;

    DIR *d = openDir(localFilesDir.c_str());
    if (!d) {
        LOG_ERROR("Cannot open directory %s: %s", localFilesDir.c_str(), strerror(errno));
        return -1;
    }

    while (1) {
        std::string filename = getNextFile(d);
        if (filename.empty()) break;
        std::list<std::string>::const_iterator i;
        i = std::find(hr.lines.begin(), hr.lines.end(), filename);
        if (i == hr.lines.end()) {
            printf("pushing attached file: %s\n", filename.c_str());
            std::string localFile = localFilesDir + '/' + filename;
            std::string url = pushCtx.rooturl + resourceFilesDir + '/' + filename;
            std::string response; // not used in this context
            int r = pushFile(pushCtx, localFile, url, response);
            if (r != 0) exit(1);
        }
    }

    closeDir(d);
    return 0;
}

/** Push the first entry of an issue.
  *
  * @param i
  *     The issue id may be modified, as the server allocates the issue identifiers.
  */
int pushFirstEntry(const PullContext &pushCtx, const Project &project, Issue &i, const Entry &e)
{
    // post the entry (which must be the first entry of an issue)
    // the result of the POST indicates the issue number that has been allocated
    std::string localFile = i.path + '/' + e.id;
    std::string url = pushCtx.rooturl + '/' + project.getUrlName() + "/issues/" + i.id + '/' + e.id;
    std::string response;
    int r = pushFile(pushCtx, localFile, url, response);
    if (r != 0) {
        // error
        LOG_ERROR("%s: Could not push entry %s\n", project.getName().c_str(), e.id.c_str());
        exit(1);
    }
    // first line of response gives the new allocated issue id
    // format is:
    //     issue: 010203045666
    popToken(response, ":");
    trim(response);
    if (i.id != response) {
        printf("%s: Renaming local issue %s -> %s\n", project.getName().c_str(),
               i.id.c_str(), response.c_str());
        // TODO rename locally
    } // else: no change
}


int pushIssue(const PullContext &pushCtx, const Project &project, const Issue &i)
{
    // - get list of entries of the same remote issue
    //    + if no such remote issue, push
    //    + if discrepancy (first entries do not match), abort and ask the user to pull first
    //    + else, push the local entries missing on the remote side
    Entry *e = i.latest;
    while (e && e->prev) e = e->prev; // rewind to first entry
    if (!e) {
        LOG_ERROR("%s: null first entry for issue %s", project.getName().c_str(),
                  i.id.c_str());
        exit(1);
    }
    const Entry &firstEntry = *e;

    // TODO check if
    std::list<std::string> entries;
    int r = getEntriesOfIssue(pushCtx, project, i, entries);
    if (r != 0) {
        // no such remote issue
        // push first entry
        pushFirstEntry(pushCtx, project, i, firstEntry);

        // TODO push remaining entries

    } else if (entries.empty()) {
        // internal error, should not happen
        LOG_ERROR("%s: error, empty entries for remote issue %s\n", project.getName().c_str(),
                i.id.c_str());
        exit(1);

    } else {
        // check if first entries match
        if (entries.front() != firstEntry) {
            printf("%s: mismatch of first entries for issue %s\n", project.getName().c_str(),
                   i.id.c_str());
            printf("Try pulling first to resolve\n");
            exit(1);
        }

        // walk through entries and push missing ones
        // TODO
    }


}

int pushProject(const PullContext &pushCtx, const Project &project)
{
    printf("pushing project %s...\n", project.getName().c_str());

    // pushFiles:
    // - get list of remote files
    // - upload local files missing on the remote side
    int r = pushAttachedFiles(pushCtx, project);
    if (r != 0) return r;

    // push issues
    std::map<std::string, std::list<std::string> > filterIn;
    std::map<std::string, std::list<std::string> > filterOut;

    std::vector<const Issue*> issues = project.search(0, filterIn, filterOut, "id");

    std::vector<const Issue*>::const_iterator it;
    FOREACH(it, issues) {
        const Issue* i = *it;
        r = pushIssue(pushCtx, project, *i);
        if (r != 0) return r;
    }
    return 0;
}

int pushProjects(const PullContext &pushCtx)
{
    // Load all local projects
    int r = dbLoad(pushCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pushCtx.localRepo.c_str());
        exit(1);
    }

    // for each local project, push it

    const Project *p = Database::Db.getNext(0);
    while (p) {
        pushProject(pushCtx, *p);
        p = Database::Db.getNext(p);
    }
    return 0;
}

Args *setupPushOptions()
{
    Args *args = new Args();
    args->setDescription("Push local changes to a remote repository.");
    args->setUsage("smit push [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
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

/**
  */
int establishSession(const char *dir, const char *username, const char *password, PullContext &ctx)
{
    curl_global_init(CURL_GLOBAL_ALL);

    // get the remote url from local configuration file
    std::string url = loadUrl(dir);
    if (url.empty()) {
        printf("Cannot get remote url.\n");
        return -1;
    }

    ctx.rooturl = url;

    bool redoSigin = false;

    std::string user;
    std::string pass;
    if (!username) {
        // check if the sessid is valid
        ctx.httpCtx.sessid = loadSessid(dir);
        int r = testSessid(url, ctx.httpCtx);

        if (r < 0) {
            // failed (session may have expired), then prompt for user name/password
            printf("Session not valid. You must sign-in again.\n");
            user = getString("Username: ", false);

            // and redo the signing-in
            redoSigin = true;
        }

    } else {
        // do the signing-in with the given username / password
        redoSigin = true;
        user = username;
    }

    if (redoSigin && !password) pass = getString("Password: ", true);

    if (redoSigin) {
        ctx.httpCtx.sessid = signin(url, user, pass, ctx.httpCtx);
        if (!ctx.httpCtx.sessid.empty()) {
            storeSessid(dir, ctx.httpCtx.sessid);
            storeUrl(dir, url);
        }
    }

    LOGV("%s=%s\n", SESSID, ctx.httpCtx.sessid.c_str());

    if (ctx.httpCtx.sessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return -1; // authentication failed
    }
    return 0;
}

void terminateSession()
{
    curl_global_cleanup();
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
        Verbose = true;
        HttpRequest::setVerbose(true);
    }
    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    const char *unexpected = args->pop();
    if (unexpected) {
        // normally already managed by the Args class
        printf("Too many arguments.\n\n");
        return helpPush(args);
    }

    setLoggingOption(LO_CLI);
    // set log level to hide INFO logs
    setLoggingLevel(LL_ERROR);

    int r = establishSession(dir, username, password, pushCtx);
    if (r != 0) return 1;

    // pull new entries and new attached files of all projects
    pushCtx.localRepo = dir;
    r = pushProjects(pushCtx);

    terminateSession();

    if (r < 0) return 1;
    else return 0;


}
