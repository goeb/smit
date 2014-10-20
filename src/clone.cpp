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


enum Mode { MODE_PULL, // fetch only missing files
       MODE_CLONE // fetch all files
     };


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
    void getAndStore(bool recursive, int recursionLevel, Mode cmode);
    int test();
    void handleWriteToFile(void *data, size_t size);
    void openFile();
    void closeFile();
    inline void setRepository(const std::string &r) { repository = r; }

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
    enum Mode cloneMode;
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
int getProjects(const std::string &rooturl, const std::string &destdir, const std::string &sessid, enum Mode mode)
{
    LOGV("getProjects(%s, %s, %s)\n", rooturl.c_str(), destdir.c_str(), sessid.c_str());

    HttpRequest hr(sessid);
    hr.setUrl(rooturl, "/");
    hr.setRepository(destdir);
    hr.getAndStore(true, 0, mode);

    return 0;
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

    getProjects(url, dir, sessid, MODE_CLONE);

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

    // TODO add a flag that says to fetch only new remote files
    getProjects(url, dir, sessid, MODE_PULL);

    curl_global_cleanup();

    return 0;


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
void HttpRequest::getAndStore(bool recursive, int recursionLevel, enum Mode cmode)
{
    LOGV("Entering getAndStore: resourcePath=%s\n", resourcePath.c_str());
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeToFileCallback);
    cloneMode = cmode;

    performRequest();

    if (isDirectory && recursive) {
        // finalize received lines
        handleReceivedLines(0, 0);

        closeCurl();

        std::string localPath = repository + resourcePath;
        trimRight(localPath, "/");
        if (recursionLevel < 2) printf("%s...\n", localPath.c_str());

        // make current directory locally
        mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
        LOGV("mkdir %s...\n", localPath.c_str());
        int r = mg_mkdir(localPath.c_str(), mode);
        if (r != 0 && cloneMode == MODE_CLONE) {
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
            hr.getAndStore(true, recursionLevel+1, cmode);
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
    filename = repository + resourcePath;
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
        std::string path = repository + resourcePath;
        if (cloneMode == MODE_PULL) {
            // do not overwrite files
            // TODO force resolution of conflicting history
            //  A---B---C---E (remote entries)
            //           \
            //            D (local entry)
            // 2 alternatives:
            // - D relocated after E (modification of parent and of entry id)
            // - D removed (if user decides that D is made obsolete by E)

            if (fileExists(path)) return;
        }

        if (!fd) {
            if (cloneMode == MODE_PULL) printf("%s\n", path.c_str());
            openFile();
        }
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
            // clean up possible \r before the \n
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
