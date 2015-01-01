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

bool Verbose = false;

#define LOGV(...) do { if (Verbose) { printf(__VA_ARGS__); fflush(stdout);} } while (0)

#define SMIT_DIR ".smit"
#define PATH_SESSID SMIT_DIR "/sessid"
#define PATH_URL SMIT_DIR "/remote"


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
           "               Give the user name and the password.\n"
           "\n"
           "Example:\n"
           "  smit clone http://example.com:8090 localDir\n"
           "\n"
           );
    return 1;
}



int testSessid(const std::string url, const std::string &sessid)
{
    LOGV("testSessid(%s, %s)\n", url.c_str(), sessid.c_str());

    HttpRequest hr(sessid);
    hr.setUrl(url, "/");
    int status = hr.test();
    LOGV("status = %d\n", status);

    if (status == 200) return 0;
    return -1;

}

/** Get all the projects that we have read-access to.
  */
int getProjects(const std::string &rooturl, const std::string &destdir, const std::string &sessid)
{
    LOGV("getProjects(%s, %s, %s)\n", rooturl.c_str(), destdir.c_str(), sessid.c_str());

    HttpRequest hr(sessid);
    hr.setUrl(rooturl, "/");
    hr.setRepository(destdir);
    hr.doCloning(true, 0);

    return 0;
}

struct PullContext {
    std::string rooturl; // eg: http://example.com:8090/
    std::string localRepo; // path to local repository
    std::string sessid; // session identifier
};

/** Download entries, starting at the given iterator
  *
  * @param localDir
  *    Specifies the location where the entries should be downloaded.
  *    If empty, then the default location is used inside the local repository.
  *
  */
void downloadEntries(const PullContext &pullCtx, const Project &p, const Issue &i,
                     const std::list<std::string> &remoteEntries, std::list<std::string>::iterator reid, const std::string &localDir)
{
    LOGV("downloadEntries: %s ...\n", reid->c_str());
    for ( ; reid != remoteEntries.end(); reid++) {
        if (reid->empty()) continue; // directory listing may end by an empty line
        std::string remoteEid = *reid;
        std::string resourceDir = "/" + p.getUrlName() + "/issues/" + i.id;
        std::string resource = resourceDir + "/" + remoteEid;

        HttpRequest hr(pullCtx.sessid);
        hr.setUrl(pullCtx.rooturl, resource);

        std::string downloadDir = localDir;
        if (downloadDir.empty()) downloadDir = pullCtx.localRepo + resourceDir;

        // TODO download to tmp storage, then move (in order to have an atomic dowload)
        std::string localEntryFile = downloadDir + "/" + remoteEid;
        hr.downloadFile(resource, localEntryFile);

        // download attached files as well
        Entry *e = Entry::loadEntry(downloadDir, remoteEid.c_str());
        if (!e) {
            // should not happen
            fprintf(stderr, "Cannot load locally downloaded entry: %s/%s", downloadDir.c_str(), remoteEid.c_str());
            fprintf(stderr, "Abort.");
            exit(1);
        }
        PropertiesIt files = e->properties.find(K_FILE);
        if (files != e->properties.end()) {
            std::list<std::string>::const_iterator f;
            // download each file
            FOREACH(f, files->second) {
                std::string fResource = "/" + p.getUrlName() + "/" K_UPLOADED_FILES_DIR "/" + *f;
                HttpRequest hr(pullCtx.sessid);
                hr.setUrl(pullCtx.rooturl, fResource);
                std::string localFile = p.getTmpDir() + "/" + *f;
                hr.downloadFile(fResource, localFile);

                // move the tmp file to official 'files'
                std::string officialPath = p.getPathUploadedFiles() + "/" + *f;
                int r = rename(localFile.c_str(), officialPath.c_str());
                if (r != 0) {
                    fprintf(stderr, "Canno move '%s' -> '%s': %s\n", localFile.c_str(), officialPath.c_str(), strerror(errno));
                    fprintf(stderr, "Abort.\n");
                    exit(1);
                }
            }
        }

    }
}

/** Merge a local entry into a remote branch (downloaded locally)
  *
  * @param remoteIssue
  *      in/out: The remote issue instance is updated by the merging
  */
void mergeEntry(const Entry *localEntry, const std::string &dir, Issue &remoteIssue, const Issue &remoteConflictingIssuePart)
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
        if (propertyName == K_MESSAGE) continue; // handled below
        if (propertyName == K_FILE) continue; // handled below

        std::list<std::string> localValue = localProperty->second;
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

            // there is a conflict: the remote side has changed this property,
            // but with a different value than the local entry
            isConflicting = true;

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
                // keep the remote property
                newProperties[propertyName] = remoteValue;
            }
        }
    }

    // merge the attached files
    // TODO newProperties[K_FILE]

    // keep the message (ask for confirmation?)
    if (localEntry->getMessage().size() > 0) {
        // if a conflict was detected before, then ask the user to keep the message of not
        if (isConflicting) {
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
        } else {
            // no conflict on the properties. keep the message unchanged
            newProperties[K_MESSAGE].push_back(localEntry->getMessage());
        }
    }

    // check if this new entry must be kept
    if (newProperties.size() > 0) {
        // store the new entry
        Entry *e = remoteIssue.addEntry(newProperties, localEntry->author, dir);
        if (!e) {
            fprintf(stderr, "Abort.");
            exit(1);
        }
        printf("New entry: %s\n", e->id.c_str());
    }
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
    // check conflict on issue id*
    std::string resource = "/" + p.getUrlName() + "/issues/" + i.id;
    HttpRequest hr(pullCtx.sessid);
    hr.setUrl(pullCtx.rooturl, resource);
    hr.getRequestLines();
    hr.closeCurl(); // free the curl resource

    if (hr.lines.empty() || hr.lines.front().empty()) {
        // should not happen
        fprintf(stderr, "Got remote issue with no entry: %s", i.id.c_str());
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
    const Entry *firstEntry = e;

    if (localEntry->id != hr.lines.front())  {
        // the remote issue and the local issue are not the same
        // the local issue has to be renamed
        LOGV("Issue %s: local (%s) and remote (%s) diverge", i.id.c_str(),
             localEntry->id.c_str(), hr.lines.front().c_str());
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
        resource = "/" + p.getUrlName() + "/issues/" + i.id;
        HttpRequest hr(pullCtx.sessid);
        hr.setUrl(pullCtx.rooturl, resource);
        hr.setRepository(pullCtx.localRepo);
        hr.doCloning(true, 0);
        // TODO pull attached files

    } else {
        // same issue. Walk through the entries and pull...

        // TODO manage deleted remote entries
        const Entry *conflictingLocalEntry = 0;// used in case of conflicting local entry
        std::string tmpPath;

        std::list<std::string>::iterator reid;
        FOREACH(reid, hr.lines) {
            std::string remoteEid = *reid;
            if (remoteEid.empty()) continue; // ignore (usually last item in the directory listing)

            if (!localEntry) {
                // remote issue has more entries. download them locally

                downloadEntries(pullCtx, p, i, hr.lines, reid, "");

                break; // leave the loop as all the remaining remotes have been managed by downloadEntries

            } else if (localEntry->id != remoteEid) {
                // remote issue has conflicting entries. download them locally in a separate directory

                // remote: a--b--c--d
                // local:  a--b--e

                conflictingLocalEntry = localEntry;

                // clone the current issue to a temporary directory and download the remote entries

                // clean up if previous aborted merging left a such temporary dir
                tmpPath = p.getTmpDir() + "/" + i.id;
                LOGV("Removing directory: %s\n", tmpPath.c_str());
                int r = removeDir(tmpPath);
                if (r<0) exit(1);

                // create the tmp dir
                mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
                r = mg_mkdir(p.getTmpDir().c_str(), mode);
                // if it could not be created because it already exists, ok, not an error
                // if it could not be created fior another reason, the following mkdir will fail
                // if no error, ok, continue.

                r = mg_mkdir(tmpPath.c_str(), mode);
                if (r != 0) {
                    fprintf(stderr, "Cannot create tmp directory '%s': %s\n", tmpPath.c_str(), strerror(errno));
                    fprintf(stderr, "Abort.\n");
                    exit(1);
                }

                // download all the remaining remote entries
                downloadEntries(pullCtx, p, i, hr.lines, reid, tmpPath);

                break; // leave the loop. Solving the conflict is handled below


            } // else nothing to do: local and remote still aligned

            // move the local entry pointer forward, except if already at the end
            if (localEntry) localEntry = localEntry->next;
        }

        if (conflictingLocalEntry)   { // TODO move this block to a separate function
            // handle the conflict on the issue

            // copy local entries to the tmp dir
            LOGV("Copying local entries to tmp dir\n");
            const Entry *e = firstEntry;
            while (e != 0 && e != conflictingLocalEntry) {
                std::string srcPath = i.path + "/" + e->id;
                std::string destPath = tmpPath + "/" + e->id;
                int r = copyFile(srcPath, destPath);
                if (r != 0) {
                    fprintf(stderr, "Cannot copy entry to tmp location: %s\n", destPath.c_str());
                    fprintf(stderr, "Abort.\n");
                    exit(1);
                }
                e = e->next;
            }

            // load this tmp issue in memory (it is the same as the remote issue)
            Issue remoteIssue;
            int r = remoteIssue.load(i.id, tmpPath);
            if (r != 0) {
                fprintf(stderr, "Cannot load downloaded issue: %s\n", tmpPath.c_str());
                fprintf(stderr, "Abort.\n");
                exit(1);

            }

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
                mergeEntry(conflictingLocalEntry, tmpPath, remoteIssue, remoteConflictingIssuePart);
                conflictingLocalEntry = conflictingLocalEntry->next;
            }

            // move the merge issue from tmp to official storage
            p.officializeMerging(remoteIssue);
            // no need to update the issue in memory, as it will not be re-accessed during the smit pulling
        }

    }

    return 0; // ok
}

int pullProject(const PullContext &pullCtx, Project &p)
{
    // get the remote issues
    HttpRequest hr(pullCtx.sessid);
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

        if (r < 0) {
            // simply clone the remote issue
            LOGV("Cloning issue: %s/issues/%s\n", p.getName().c_str(), id.c_str());
            resource = "/" + p.getUrlName() + "/issues/" + id;
            HttpRequest hr(pullCtx.sessid);
            hr.setUrl(pullCtx.rooturl, resource);
            hr.setRepository(pullCtx.localRepo);
            hr.doCloning(true, 0);
            // TODO pull attached files
        } else {
            LOGV("Pulling issue: %s/issues/%s\n", p.getName().c_str(), id.c_str());
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
    // set log level to hide INFO logs
    setLoggingLevel(LL_ERROR);

    // Load all local projects
    int r = dbLoad(pullCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pullCtx.localRepo.c_str());
        exit(1);
    }

    // get the list of remote projects
    HttpRequest hr(pullCtx.sessid);
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
            fprintf(stderr, "remote project not existing locally. TODO. Exiting...\n");
            exit(1);
        }
        pullProject(pullCtx, *p);
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



std::string signin(const std::string &rooturl, const std::string &user, const std::string &passwd)
{
    HttpRequest hr("");

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

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
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

    curl_global_init(CURL_GLOBAL_ALL);

    std::string sessid = signin(url, username, password);
    LOGV("%s=%s\n", SESSID, sessid.c_str());

    if (sessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return 1; // authentication failed
    }

    getProjects(url, dir, sessid);

    // ceate persistent configuration of the local clone
    createSmitDir(dir);
    storeSessid(dir, sessid);
    storeUrl(dir, url);

    curl_global_cleanup();

    return 0;
}



int helpPull()
{
    printf("Usage: smit pull\n"
           "\n"
           "  Incorporates changes from a remote repository into the local copy.\n"
           "\n"
           "Options:\n"
           "  --user <user> --passwd <password> \n"
           "               Give the user name and the password.\n"
           "\n"
           );
    return 1;
}


int cmdPull(int argc, char * const *argv)
{
    std::string username;
    const char *passwd = 0;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
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
            }
            break;
        case 'v':
            Verbose = true;
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
        printf("Too many arguments.\n\n");
        return helpPull();
    }

    const char *dir = "."; // local dir

    // get the remote url from local configuration file
    std::string url = loadUrl(dir);
    if (url.empty()) {
        printf("Cannot get remote url.\n");
        exit(1);
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::string sessid;
    std::string password;
    bool redoSigin = false;

    if (username.empty()) {
        // check if the sessid is valid
        sessid = loadSessid(dir);
        int r = testSessid(url, sessid);

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
        sessid = signin(url, username, password);
        if (!sessid.empty()) {
            storeSessid(dir, sessid);
            storeUrl(dir, url);
        }
    }

    LOGV("%s=%s\n", SESSID, sessid.c_str());

    if (sessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return 1; // authentication failed
    }

    // pull new entries and new attached files of all projects
    PullContext pullCtx;
    pullCtx.rooturl = url;
    pullCtx.localRepo = dir;
    pullCtx.sessid = sessid;
    int r = pullProjects(pullCtx);

    curl_global_cleanup();

    if (r < 0) return 1;
    else return 0;


}

