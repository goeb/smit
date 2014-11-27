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




class Cookie {
public:
    std::string domain;
    std::string flag;
    std::string path;
    std::string secure;
    std::string expiration;
    std::string name;
    std::string value;
    int parse(std::string cookieLine);
};

class HttpRequest {
public:
    HttpRequest(const std::string &sessid);
    ~HttpRequest();
    void closeCurl(); // close libcurl resources
    void handleReceivedLines(const char *contents, size_t size);
    void handleReceivedRaw(void *data, size_t size);
    void setUrl(const std::string &root, const std::string &path);
    void getRequestLines();
    void getRequestRaw();
    void post(const std::string &params);
    std::map<std::string, Cookie> cookies;
    std::list<std::string> lines; // fulfilled after calling getRequestLines()
    void doCloning(bool recursive, int recursionLevel);
    int test();
    void handleWriteToFile(void *data, size_t size);
    void openFile();
    void closeFile();
    /** In order to download files, the caller must set either set
      * - the repository
      *            the downloaded file will be located in a path depending
      *            on the repo and the resource path.
      * - or the download dir
      *            the downloaded file will be located directly in this dir.
      */
    inline void setRepository(const std::string &r) { repository = r; }
    inline void setDownloadDir(const std::string &r) { downloadDir = r; }

    static size_t receiveLinesCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t writeToFileCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t headerCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t ignoreResponseCallback(void *contents, size_t size, size_t nmemb, void *userp);

    int httpStatusCode;

private:
    void performRequest();
    std::string rooturl;
    std::string resourcePath;
    std::string response;
    std::string sessionId;
    std::string currentLine;
    CURL *curlHandle;
    struct curl_slist *slist;
    std::string filename; // name of the file to write into
    FILE *fd; // file descriptor of the file
    bool isDirectory;
    std::string repository; // base path for storage of files
    std::string downloadDir; // alternative to repository for storage
};


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
  */
void downloadEntries(const PullContext &pullCtx, const Project &p, const Issue &i,
                     const std::list<std::string> &remoteEntries, std::list<std::string>::iterator reid, const std::string &localDir)
{
    LOGV("downloadEntries: %s ...", reid->c_str());
    while (reid != remoteEntries.end()) {
        std::string remoteEid = *reid;
        std::string resource = "/" + p.getUrlName() + "/issues/" + i.id + "/" + remoteEid;
        HttpRequest hr(pullCtx.sessid);
        hr.setUrl(pullCtx.rooturl, resource);
        std::string downloadDir = localDir;

        if (downloadDir.empty()) downloadDir = pullCtx.localRepo + resource;

        hr.setDownloadDir(downloadDir);
        hr.doCloning(false, 0);
        reid++;
    }
}

/** Merge a local entry into a remote branch (downloaded locally)
  *
  */
void mergeEntry(const Entry *localEntry, const std::string &dir, const Issue &remoteIssue, const Issue &remoteConflictingIssuePart)
{
    Entry newEntry; // the resulting entry after the merging

    // merge the properties
    std::map<std::string, std::list<std::string> >::const_iterator localProperty;
    FOREACH(localProperty, localEntry->properties) {
        // look if the property has been modified on the remote side
        std::string propertyName = localProperty->first;
        std::list<std::string> localValue = localProperty->second;
        std::map<std::string, std::list<std::string> >::const_iterator remoteProperty;
        remoteProperty = remoteConflictingIssuePart.properties.find(propertyName);
        if (remoteProperty == remoteConflictingIssuePart.properties.end()) {
            // This property has not been changed on the remote side
            // Keep it unchanged
            newEntry.properties[propertyName] = localValue;
        } else {
            // This property has also been changed on the remote side.
            if (localProperty->second == remoteProperty->second) {
                // same value for this property
                // do not keep it for the new entry
            } else {
                // This is a conflict.
                std::list<std::string> remoteValue = remoteProperty->second;
                printf("-- Conflict on issue %s: %s\n", remoteIssue.id.c_str(), remoteIssue.getSummary().c_str());
                printf("Remote property: %s => %s\n", propertyName.c_str(), toString(remoteValue).c_str());
                printf("Local property : %s => %s\n", propertyName.c_str(), toString(localValue).c_str());
                printf("Select: (l)ocal, (r)emote: ");
                std::string response;
                while (response != "l" && response != "L" && response != "r" && response != "R") {
                    std::cin >> response;
                }
                if (response == "l" && response == "L") {
                    // keep the local property
                    newEntry.properties[propertyName] = localValue;
                } else {
                    // keep the remote property
                    newEntry.properties[propertyName] = remoteValue;
                }
            }
        }
    }

    // merge the attached files
    // TODO

    // keep the message (ask for confirmation?)
    if (localEntry->getMessage().size() > 0) {
        // TODO
    }

    // check if this new entry must be kept
    if (newEntry.getMessage().size() || newEntry.properties.size() > 0 /* TODO files */) {
        // TODO
        // store the new entry
    }


//    Entry *addEntry(std::map<std::string, std::list<std::string> > properties,
  //                  const std::string &username, const std::string &issueDir);


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
                int r = removeDir(tmpPath);
                if (r<0) exit(1);

                // create the tmp dir
                mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
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
            LOGV("Copying local entries to tmp dir");
            const Entry *e = firstEntry;
            while (e != 0 && e != conflictingLocalEntry) {
                std::string path = tmpPath + "/" + e->id;
                int r = writeToFile(tmpPath.c_str(), e->serialize());
                if (r != 0) {
                    fprintf(stderr, "Cannot store entry in tmp directory: %s\n", e->id.c_str());
                    fprintf(stderr, "Abort.\n");
                    exit(1);
                }
            }

            // load this tmp issue in memory (it is the same as the remote issue)
            Issue remoteIssue;
            int r = remoteIssue.load(tmpPath);
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
            // - either rewrite automatically the local entry:
            //     + rewrite the parent,
            //     + remove redundant properties changes
            //     + keep the message and attached files, if any
            // - or ask the user to merge some conflicting properties
            // and add the +merge indication
            // TODO merge conflictingLocalEntry and followers into this tmp issue
            while (conflictingLocalEntry) {
                mergeEntry(conflictingLocalEntry, tmpPath, remoteIssue, remoteConflictingIssuePart);
                conflictingLocalEntry = conflictingLocalEntry->next;
            }

            // TODO move the merge issue from tmp to official storage
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
        if (projectName->empty()) continue;

        printf("Pulling entries of project: %s...\n", projectName->c_str());
        Project *p = Database::Db.getProject(*projectName);
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

void HttpRequest::setUrl(const std::string &root, const std::string &path)
{
    if (path.empty() || path[0] != '/') {
        fprintf(stderr, "setUrl: Malformed internal path '%s'\n", path.c_str());
        exit(1);
    }
    rooturl = root;
    trimRight(rooturl, "/");
    resourcePath = path;
    std::string url = rooturl + urlEncode(resourcePath, '%', "/"); // do not url-encode /
    curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str());
}


/** Get files recursively through sub-directories
  *
  * @param recursionLevel
  *    used to track the depth in the sub-directories
  */
void HttpRequest::doCloning(bool recursive, int recursionLevel)
{
    LOGV("Entering doCloning: resourcePath=%s\n", resourcePath.c_str());
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeToFileCallback);

    if (recursionLevel < 2) printf("%s\n", resourcePath.c_str());
    performRequest();

    if (isDirectory && recursive) {
        // finalize received lines
        handleReceivedLines(0, 0);

        // free some resource before going recursive
        // force this cleanup before destructor of this HttpRequest instance
        closeCurl();

        std::string localPath = repository + resourcePath;
        trimRight(localPath, "/");

        // make current directory locally
        mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
        LOGV("mkdir %s...\n", localPath.c_str());
        int r = mg_mkdir(localPath.c_str(), mode);
        if (r != 0) {
            fprintf(stderr, "Cannot create directory '%s': %s\n", localPath.c_str(), strerror(errno));
            fprintf(stderr, "Abort.\n");
            exit(1);
        }

        std::list<std::string>::iterator file;
        FOREACH(file, lines) {

            if (file->empty()) continue;
			if ((*file)[0] == '.') continue; // do not clone hidden files

            std::string subpath = resourcePath;
            if (resourcePath != "/")  subpath += '/';
            subpath += (*file);

            HttpRequest hr(sessionId);
            hr.setUrl(rooturl, subpath);
            hr.setRepository(repository);
            hr.doCloning(true, recursionLevel+1);
        }
    }
    if (!isDirectory) {
        // make sure that even an empty file gets created as well
        handleWriteToFile(0, 0);
        if (fd) closeFile(); // it may happen that the file was not open in MODE_PULL
    }
}

void HttpRequest::post(const std::string &params)
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, ignoreResponseCallback);
    curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, params.c_str());
    performRequest();
}



int HttpRequest::test()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, receiveLinesCallback);
    performRequest();
    return httpStatusCode;
}

void HttpRequest::getRequestLines()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, receiveLinesCallback);
    performRequest();
    handleReceivedLines(0, 0); // finalize the last line
}


/** Parse a cookie line in libcurl format (same as netscape format)
  * Example:
  * .example.com TRUE / FALSE 946684799 key value
  * (fields separated by tabs)
  */
int Cookie::parse(std::string cookieLine)
{
    domain = popToken(cookieLine, '\t');
    flag = popToken(cookieLine, '\t');
    path = popToken(cookieLine, '\t');
    secure = popToken(cookieLine, '\t');
    expiration = popToken(cookieLine, '\t');
    name = popToken(cookieLine, '\t');
    value = cookieLine;
    LOGV("cookie: %s = %s\n", name.c_str(), value.c_str());
    return 0;
}

void HttpRequest::performRequest()
{
    CURLcode res;

    LOGV("resource: %s%s\n", rooturl.c_str(), resourcePath.c_str());
    res = curl_easy_perform(curlHandle);

    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s (%s%s)\n", curl_easy_strerror(res), rooturl.c_str(), resourcePath.c_str());
        exit(1);
    }

    // get the cookies
    struct curl_slist *curlCookies;

    res = curl_easy_getinfo(curlHandle, CURLINFO_COOKIELIST, &curlCookies);
    if (res != CURLE_OK) {
        fprintf(stderr, "Curl curl_easy_getinfo failed: %s\n", curl_easy_strerror(res));
        exit(1);
    }

    struct curl_slist *nc = curlCookies;
    int i = 1;
    while (nc) {
        // parse the cookie
        Cookie c;
        c.parse(nc->data);
        cookies[c.name] = c;
        nc = nc->next;
        i++;
    }
    curl_slist_free_all(curlCookies);
}

size_t HttpRequest::headerCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;

    LOGV("HDR: %s", (char*)contents);

    if (hr->httpStatusCode == -1) {
        // this header should be the HTTP response code
        std::string code = (char*)contents;
        popToken(code, ' ');
        std::string reponseCode = popToken(code, ' ');
        hr->httpStatusCode = atoi(reponseCode.c_str());

        if (hr->httpStatusCode < 200 && hr->httpStatusCode > 299) {
            fprintf(stderr, "%s: HTTP status code %d. Exiting.\n", hr->resourcePath.c_str(), hr->httpStatusCode);
            exit(1);
        }
    }

    // check content-type
    if (strstr((char*)contents, "Content-Type: text/directory")) hr->isDirectory = true;

    return realsize;
}

size_t HttpRequest::writeToFileCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;
    hr->handleWriteToFile(contents, realsize);
    return realsize;
}


void HttpRequest::handleReceivedRaw(void *data, size_t size)
{
    response.append((char*)data, size);
}

void HttpRequest::openFile()
{
    if (repository.size() > 0) filename = repository + resourcePath; // resourcePath has a starting '/'
    else if (downloadDir.size() > 0) filename = downloadDir + "/" + getBasename(resourcePath);
    else {
        fprintf(stderr, "repository and downloadDir are both empty strings\n");
        exit(1);
    }
    LOGV("Opening file: %s\n", filename.c_str());
    fd = fopen(filename.c_str(), "wbx");
    if (!fd) {
        fprintf(stderr, "Cannot create file '%s': %s\n", filename.c_str(), strerror(errno));
        exit(1);
    }
}

void HttpRequest::closeFile()
{
    fclose(fd);
    fd = 0;
    filename = "";
}


void HttpRequest::handleWriteToFile(void *data, size_t size)
{
    if (isDirectory) {
        // store the lines temporarily in memory
        handleReceivedLines((char*)data, size);

    } else {
        if (!fd) openFile();

        if (size) {
            size_t n = fwrite(data, size, 1, fd);
            if (n != 1) {
                fprintf(stderr, "Cannot write to '%s': %s\n", filename.c_str(), strerror(errno));
                exit(1);
            }
        } // else the size is zero, an empty file has been created
    }
}

/** Ignore the response
  * (do not save it into a file, as the default lib curl behaviour)
  */
size_t HttpRequest::ignoreResponseCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    return size * nmemb;
}

size_t HttpRequest::receiveLinesCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;
    hr->handleReceivedLines((char*)contents, realsize);
    return realsize;
}

void HttpRequest::handleReceivedLines(const char *data, size_t size)
{
    if (data == 0) {
        // finalize the currentLine and return
        lines.push_back(currentLine);
        currentLine.clear();
    }
    size_t i = 0;
    size_t notConsumedOffset = 0;
    while (i < size) {
        if (data[i] == '\n') {
            // clean up character \r before the \n (if any)
            size_t endl = i;
            if (i > 0 && data[i-1] == '\r') endl--;

            currentLine.append(data + notConsumedOffset, endl-notConsumedOffset);
            lines.push_back(currentLine);
            currentLine.clear();
            notConsumedOffset = i+1;
        }
        i++;
    }
    if (notConsumedOffset < size) currentLine.append(data, notConsumedOffset, size-notConsumedOffset);
}


HttpRequest::HttpRequest(const std::string &sessid)
{
    sessionId = sessid;
    isDirectory = false;
    fd = 0;
    httpStatusCode = -1; // not set
    curlHandle = curl_easy_init();
    curl_easy_setopt(curlHandle, CURLOPT_USERAGENT, "smit-agent/1.0");
    curl_easy_setopt(curlHandle, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curlHandle, CURLOPT_HEADERDATA, (void *)this);


    slist = 0;
    slist = curl_slist_append(slist, "Accept: " APP_X_SMIT);
    if (!sessionId.empty()) {
        std::string cookie = "Cookie: " SESSID "=" + sessionId;
        slist = curl_slist_append(slist, cookie.c_str());
    }

    curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, slist);

    curl_easy_setopt(curlHandle, CURLOPT_COOKIEFILE, ""); /* just to start the cookie engine */
}

HttpRequest::~HttpRequest()
{
    if (slist) curl_slist_free_all(slist);
    if (curlHandle) curl_easy_cleanup(curlHandle);
}

void HttpRequest::closeCurl()
{
    curl_slist_free_all(slist);
    slist = 0;
    curl_easy_cleanup(curlHandle);
    curlHandle = 0;
}
