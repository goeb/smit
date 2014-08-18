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

bool Verbose = false;

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
    void getAndStore(bool recursive, int recursionLevel);
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
};




/** Get all the projects that we have read-access to.
  */
int getProjects(const char *rooturl, const std::string &destdir, const std::string &sessid)
{
    if (Verbose) printf("getProjects(%s, %s, %s)\n", rooturl, destdir.c_str(), sessid.c_str());

    HttpRequest hr(sessid);
    hr.setUrl(rooturl, "/");
    hr.setRepository(destdir);
    hr.getAndStore(true, 0);

    return 0;
}

std::string signin(const char *rooturl, const std::string &user, const std::string &passwd)
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
    c = hr.cookies.find("sessid");
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
    static struct option longOptions[] = {
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
        case 'v':
            Verbose = true;
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            exit(1);
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            exit(1);
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
    if (Verbose) printf("sessid=%s\n", sessid.c_str());

    if (sessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return 1; // authentication failed
    }

    getProjects(url, dir, sessid);


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


void HttpRequest::getAndStore(bool recursive, int recursionLevel)
{
    if (Verbose) printf("Entering getAndStore: resourcePath=%s\n", resourcePath.c_str());
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeToFileCallback);
    performRequest();
    if (isDirectory && recursive) {
        // finalize received lines
        handleReceivedLines(0, 0);

        closeCurl();

        std::string localPath = repository + resourcePath;
        trimRight(localPath, "/");
        if (recursionLevel < 2) printf("%s...\n", localPath.c_str());

        mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
        if (Verbose) printf("mkdir %s...\n", localPath.c_str());
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
            hr.getAndStore(true, recursionLevel+1);
        }
    }
	if (!isDirectory) {
        // make sure that even an empty file gets created as well
        handleWriteToFile(0, 0);
		closeFile();
    }
}

void HttpRequest::post(const std::string &params)
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, ignoreResponseCallback);
    curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, params.c_str());
    performRequest();
}


void HttpRequest::getRequestLines()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, receiveLinesCallback);
    performRequest();
    handleReceivedLines(0, 0);
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
    if (Verbose) printf("cookie: %s = %s\n", name.c_str(), value.c_str());
    return 0;
}

void HttpRequest::performRequest()
{
    CURLcode res;

    if (Verbose) printf("resource: %s%s\n", rooturl.c_str(), resourcePath.c_str());
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

    if (Verbose) printf("HDR: %s", (char*)contents);

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
    if (Verbose) printf("Opening file: %s\n", filename.c_str());
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
        std::string cookie = "Cookie: sessid=" + sessionId;
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
