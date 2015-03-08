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
#include "restApi.h"
#include "Project.h"


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
    LOG_DEBUG("testSessid(%s, %s)", url.c_str(), ctx.sessid.c_str());

    HttpRequest hr(ctx);
    hr.setUrl(url, "/");
    int status = hr.test();
    LOG_DEBUG("status = %d", status);

    if (status == 200) return 0;
    return -1;

}

/** Get all the projects that we have read-access to.
  */
int getProjects(const std::string &rooturl, const std::string &destdir, const HttpClientContext &ctx)
{
    LOG_DEBUG("getProjects(%s, %s, %s)", rooturl.c_str(), destdir.c_str(), ctx.sessid.c_str());

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
            LOG_DEBUG("Local message dropped.");
        } else {
            // no conflict on the properties. keep the message unchanged
            newProperties[K_MESSAGE].push_back(localEntry->getMessage());
        }
    }

    // check if this new entry must be kept
    if (newProperties.size() > 0) {
        // store the new entry
        Entry *e = Entry::createNewEntry(newProperties, localEntry->author, remoteIssue.latest);
        remoteIssue.addEntry(e);
        printf("New entry: %s\n", e->id.c_str());
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
    while (re && re->id != commonParent) re = re->next;
    if (!re) {
        LOG_ERROR("Cannot find remote common parent in locally downloaded issue: %s", commonParent.c_str());
        LOG_ERROR("Abort.");
        exit(1);
    }
    Issue remoteConflictingIssuePart;
    re = re->next; // start with the first conflicting entry
    while (re) {
        remoteConflictingIssuePart.consolidateWithSingleEntry(re, true);
        re = re->next;
    }
    // at this point, remoteConflictingIssuePart contains the conflicting remote part of the issue

    // for each local conflicting entry, do the merge
    std::list<Entry *> mergingEntries;
    while (conflictingLocalEntry) {
        Entry *e = mergeEntry(conflictingLocalEntry, remoteIssue, remoteConflictingIssuePart, pullCtx.mergeStrategy);
        if (e) mergingEntries.push_back(e);
        conflictingLocalEntry = conflictingLocalEntry->next;
    }
    // store to disk
    // TODO check if entry already exists in memory

    // The storage is done after all merging is complete
    // because if the user interrupts the inetractive merging, then we don't
    // want a mess.
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
    std::string resource = "/" + p.getUrlName() + "/" RESOURCE_ISSUES "/" + issueId;
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(pullCtx.rooturl, resource);
    hr.getRequestLines();
    hr.closeCurl(); // free the curl resource

    if (hr.lines.empty() || hr.lines.front().empty()) {
        // may happen if remote issue does not exist
        return -1;
    }
    entries = hr.lines;
    return 0;
}

/** Pull the attached files of an issue
  */
void pullAttachedFiles(const PullContext &pullCtx, const Project &p, const Issue &i)
{
    LOG_FUNC();
    LOG_DEBUG("Pulling attached files of %s/issues/%s", p.getName().c_str(), i.id.c_str());
    // get the remote files attached to a issue
    Entry *e = i.first;
    while (e) {
        PropertiesIt files = e->properties.find(K_FILE);
        if (files != e->properties.end()) {
            LOG_DEBUG("Entry %s has files: %s", e->id.c_str(), toString(files->second).c_str());
            std::list<std::string>::const_iterator f;
            FOREACH(f, files->second) {
                // file is like: <object-id>/<basename>
                std::string fid = *f;
                std::string id = popToken(fid, '/');
                std::string localPath = p.getObjectsDir() + "/" + Object::getSubpath(id);
                if (!fileExists(localPath)) {
                    // download the file
                    printf("Pulling file %s...\n", f->c_str());
                    HttpRequest hr(pullCtx.httpCtx);
                    std::string resource = "/" + p.getUrlName() + "/" RESOURCE_FILES "/" + *f;
                    hr.setUrl(pullCtx.rooturl, resource);
                    int r = hr.downloadFile(localPath);
                    if (r!=0) LOG_ERROR("Cannot get file: %s", resource.c_str());
                } else {
                    LOG_DEBUG("Remote file already exists locally: %s", f->c_str());

                }
            }
        }
        e = e->next;
    }
}
Issue *cloneIssue(const PullContext &pullCtx, Project &p, const std::string &issueId)
{
    LOG_DEBUG("Cloning %s/issues/%s", p.getName().c_str(), issueId.c_str());

    // get the entries of the remote issue
    std::list<std::string> remoteEntries;
    int r = getEntriesOfRemoteIssue(pullCtx, p, issueId, remoteEntries);
    if (r != 0) {
        fprintf(stderr, "Cannot get entries of remote issue %s\n", issueId.c_str());
        exit(1);
    }
    if (remoteEntries.empty()) {
        fprintf(stderr, "Cannot clone remote issue that has no entry: %s\n", issueId.c_str());
        exit(1);
    }

    // download all remote entries
    std::list<std::string>::const_iterator remoteEntry;
    std::string latest;
    FOREACH(remoteEntry, remoteEntries) {
        std::string localfile = p.getObjectsDir() + "/" + Entry::getSubpath(*remoteEntry);
        if (!fileExists(localfile)) {
            // file not existing locally: do download
            printf("Pulling entry: %s\n", remoteEntry->c_str());

            HttpRequest hr(pullCtx.httpCtx);
            std::string resource = "/" + p.getUrlName() + "/" RESOURCE_FILES "/" + (*remoteEntry);
            hr.setUrl(pullCtx.rooturl, resource);
            int r = hr.downloadFile(localfile);
            if (r!=0) LOG_ERROR("Cannot get file: %s", resource.c_str());

        }
        latest = *remoteEntry;
    }

    // Load the remote issue from its latest entry
    Issue *remoteIssue = Issue::load(p.getObjectsDir(), latest);
    if (!remoteIssue) {
        fprintf(stderr, "Cannot load remote issue from latest %s", latest.c_str());
        exit(1);
    }
    // set the id of the remote, as it is not fulfilled by "load()"
    remoteIssue->id = issueId;
    return remoteIssue;
}

/** Pull an issue
  *
  * If the remote issue conflicts with a local issue (ie: they have not the same first entry):
  * - rename the local issue (thus changing its identifier)
  * - clone the remote issue
  */
int pullIssue(const PullContext &pullCtx, Project &p, const Issue &localIssue)
{
    LOG_FUNC();
    LOG_DEBUG("Pulling %s/issues/%s", p.getName().c_str(), localIssue.id.c_str());

    // compare the remote and local issue
    // get the first entry of the issue
    // check conflict on issue id

    Issue *remoteIssue = cloneIssue(pullCtx, p, localIssue.id);

    if (localIssue.first->id != remoteIssue->first->id) {

        // TODO check if the remote is not the same as one of the locals (under a different id)

        // The remote issue and the local issue have not the same first entry
        // and therefore are not the same.
        // The local issue must be renamed.

        // propose to the user a new id for the issue?

        printf("Issue conflicting with remote: %s %s\n", localIssue.id.c_str(), localIssue.getSummary().c_str());
        printf("Issue %s: local and remote diverge: %s <> %s", localIssue.id.c_str(),
                  localIssue.first->id.c_str(), remoteIssue->first->id.c_str());

        std::string newId = p.renameIssue(localIssue.id);
        if (newId.empty()) {
            fprintf(stderr, "Cannot rename issue %s. Aborting\n", localIssue.id.c_str());
            exit(1);
        }

        // inform the user
        printf("Local issue %s renamed %s (%s)\n", localIssue.id.c_str(), newId.c_str(), localIssue.getSummary().c_str());

        // store new issue id
        int r = p.storeRefIssue(remoteIssue->id, remoteIssue->latest->id);
        if (r != 0) exit(1);


    } else {
        // same issue. Walk through the entries...

        Entry *localEntry = localIssue.first;
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

                // TODO check if the issue must be inserted in memory or not
                int r = p.storeRefIssue(remoteIssue->id, remoteIssue->latest->id);
                if (r != 0) exit(1);

                break; // leave the loop.

            }

            // move the local entry pointer forward, except if already at the end
            localEntry = localEntry->next;
            remoteEntry = remoteEntry->next;
        }
    }
    pullAttachedFiles(pullCtx, p, *remoteIssue);
    return 0; // ok
}



int pullProject(const PullContext &pullCtx, Project &p)
{
    printf("Pulling project %s\n", p.getName().c_str());
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
        LOG_DEBUG("Remote: %s/issues/%s", p.getName().c_str(), id.c_str());

        // get the issue with same id in local repository
        Issue i;
        int r = p.get(id, i);

        // check if the pulled remote issue is not already
        // locally under another issue id (this may happen
        // if a previous push was interrupted)

        // TODO


        if (r < 0) {
            // simply clone the remote issue
            Issue *i = cloneIssue(pullCtx, p, id);

            // insert the issue in the project tables
            int r = p.insertIssue(i);
            if (r!=0) {
                LOG_ERROR("Cannot add cloned issue: %s", id.c_str());
                exit(1);
            }
            // store issue ref on disk
            r = p.storeRefIssue(id, i->latest->id);
            if (r != 0) exit(1);

            pullAttachedFiles(pullCtx, p, *i);

        } else {
            pullIssue(pullCtx, p, i);
        }
    }

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
    LOG_DEBUG("createSmitDir(%s)...", dir.c_str());

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
    LOG_DEBUG("storeSessid(%s, %s)...", dir.c_str(), sessid.c_str());
    std::string path = dir + "/" PATH_SESSID;
    int r = writeToFile(path.c_str(), sessid + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

void storeUsername(const std::string &dir, const std::string &username)
{
    LOG_DEBUG("storeUsername(%s, %s)...", dir.c_str(), username.c_str());
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
    LOG_DEBUG("storeUrl(%s, %s)...", dir.c_str(), url.c_str());
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
            setLoggingLevel(LL_DEBUG);
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
    LOG_DEBUG("%s=%s", SESSID, sessid.c_str());

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
            setLoggingLevel(LL_DEBUG);
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

    LOG_DEBUG("%s=%s", SESSID, ctx.sessid.c_str());

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
            setLoggingLevel(LL_DEBUG);
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

    LOG_DEBUG("%s=%s", SESSID, pullCtx.httpCtx.sessid.c_str());

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
    // TODO
    // response = hr.lines;
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
int pushFirstEntry(const PullContext &pushCtx, const Project &project, const Issue &i, const Entry &e)
{
    // post the entry (which must be the first entry of an issue)
    // the result of the POST indicates the issue number that has been allocated
    std::string localFile = i.path + '/' + e.id;
    std::string url = pushCtx.rooturl + '/' + project.getUrlName() + "/issues/" + i.id + '/' + e.id;
    std::string response;
    int r = pushFile(pushCtx, localFile, url, response);
    if (r != 0) {
        // error
        LOG_ERROR("%s: Could not push entry %s", project.getName().c_str(), e.id.c_str());
        exit(1);
    }
    // first line of response gives the new allocated issue id
    // format is:
    //     issue: 010203045666
    popToken(response, ':');
    trim(response);
    if (i.id != response) {
        printf("%s: Renaming local issue %s -> %s\n", project.getName().c_str(),
               i.id.c_str(), response.c_str());
        // TODO rename locally
    } // else: no change

    return -1; // TODO
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
    int r = getEntriesOfRemoteIssue(pushCtx, project, i.id, entries);
    if (r != 0) {
        // no such remote issue
        // push first entry
        pushFirstEntry(pushCtx, project, i, firstEntry);

        // TODO push remaining entries

    } else if (entries.empty()) {
        // internal error, should not happen
        LOG_ERROR("%s: error, empty entries for remote issue %s", project.getName().c_str(),
                i.id.c_str());
        exit(1);

    } else {
        // check if first entries match
        if (entries.front() != firstEntry.id) {
            printf("%s: mismatch of first entries for issue %s\n", project.getName().c_str(),
                   i.id.c_str());
            printf("Try pulling first to resolve\n");
            exit(1);
        }

        // walk through entries and push missing ones
        // TODO
    }

    return -1; // TODO
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

    LOG_DEBUG("%s=%s", SESSID, ctx.httpCtx.sessid.c_str());

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
        setLoggingLevel(LL_DEBUG);
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
