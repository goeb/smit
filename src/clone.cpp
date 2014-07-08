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

#include "clone.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"

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
    void setUrl(const std::string u);
    void getRequestLines();
    void getRequestRaw();
    void post(const std::string &params);
    std::map<std::string, Cookie> cookies;
    std::list<std::string> lines; // fulfilled after calling getRequestLines()
    void getAndStore(const std::string &destdir);
    void handleWriteToFile(void *data, size_t size);
    void openFile(const std::string &file);
    void closeFile();

    static size_t writeMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t receiveLinesCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t writeToFileCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t headerCallback(void *contents, size_t size, size_t nmemb, void *userp);

private:
    void performRequest();
    std::string url;
    std::string response;
    std::string currentLine;
    CURL *curlHandle;
    struct curl_slist *slist;
    std::string filename; // name of the file to write into
    FILE *fd; // file descriptor of the file
};



/** Get a project
  */
int getProject(const char *rooturl, const std::string &projectDir, const std::string &destdir, const std::string &sessid)
{
    printf("%s...\n", projectDir.c_str());

    std::string path = destdir + '/' + projectDir;
    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
    int r = mg_mkdir(path.c_str(), mode);
    if (r != 0) {
        fprintf(stderr, "Cannot create directory '%s': %s\n", path.c_str(), strerror(errno));
        fprintf(stderr, "Abort.\n");
        exit(1);
    }

    // get file 'project'
    HttpRequest hr(sessid);

    std::string url = rooturl;
    url += "/" + projectDir + "/project";
    hr.setUrl(url);

    std::string destFile = path + "/project";
    hr.openFile(destFile);
    hr.getAndStore(destdir);
    hr.closeFile();
    return 0;
}

/** Get all the projects that we have read-access to.
  */
int getProjects(const char *rooturl, const std::string &destdir, const std::string &sessid)
{
    HttpRequest hr(sessid);

    hr.setUrl(rooturl);
    hr.getRequestLines();

    if (hr.lines.empty()) {
        printf("no project.");
        exit(1);
    }

    // make root destination directory
    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
    int r = mg_mkdir(destdir.c_str(), mode);
    if (r != 0) {
        fprintf(stderr, "Cannot create directory '%s': %s\n", destdir.c_str(), strerror(errno));
        exit(1);
    }

    hr.closeCurl();

    std::list<std::string>::iterator line;
    FOREACH(line, hr.lines) {
        std::string projectDirname = popToken(*line, ' ');
        if (projectDirname.empty()) continue;

        getProject(rooturl, projectDirname, destdir, sessid);
    }
    return 0;
}

std::string signin(const char *url, const std::string &user, const std::string &passwd)
{
    HttpRequest hr("");

    std::string signinUrl = url;
    signinUrl += "/signin";
    hr.setUrl(signinUrl);

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

int cmdClone(int argc, const char **argv)
{
    std::string username;
    const char *passwd = 0;
    const char *url = 0;
    const char *dir = 0;

    int i = 0;
    while (i < argc) {
        const char *arg = argv[i];
        i++;
        if (0 == strcmp(arg, "--user")) {
            if (argc <= 0) return helpClone();
            username = argv[i];
            i++;
        } else if (0 == strcmp(arg, "--passwd")) {
            if (argc <= 0) return helpClone();
            passwd = argv[i];
            i++;
        } else if (0 == strcmp(arg, "-v")) {
            Verbose = true;
        } else {
            if (!url) url = arg;
            else if (!dir) dir = arg;
            else {
                printf("Too many arguments.\n\n");
                return helpClone();
            }
        }
    }
    if (!url || !dir) {
        printf("You must specify a repository to clone and a directory.\n\n");
        return helpClone();
    }

    if (username.empty()) {
        printf("Username: ");
        std::cin >> username;

    }
    std::string password;

    if (passwd) password = passwd;
    else {
        printf("Password: ");
        std::cin >> password;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::string sessid = signin(url, username, password);
    if (Verbose) printf("sessid=%s\n", sessid.c_str());

    getProjects(url, dir, sessid);


    curl_global_cleanup();

    return 1;
}


void HttpRequest::setUrl(const std::string u)
{
    url = u;
    curl_easy_setopt(curlHandle, CURLOPT_URL, u.c_str());
}



void HttpRequest::getAndStore(const std::string &destdir)
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeToFileCallback);
    performRequest();
}

void HttpRequest::getRequestRaw()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    performRequest();
}

void HttpRequest::post(const std::string &params)
{
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

    if (Verbose) printf("uri: %s\n", url.c_str());
    res = curl_easy_perform(curlHandle);

    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s (url %s)\n", curl_easy_strerror(res), url.c_str());
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

size_t HttpRequest::writeMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;
    hr->handleReceivedRaw(contents, realsize);
    return realsize;
}
size_t HttpRequest::headerCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    if (Verbose) printf("%s", (char*)contents);
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


void HttpRequest::openFile(const std::string &file)
{
    fd = fopen(file.c_str(), "wbx");
    if (!fd) {
        fprintf(stderr, "Cannot create file '%s': %s\n", file.c_str(), strerror(errno));
        exit(1);
    }
    filename = file;
}

void HttpRequest::closeFile()
{
    fclose(fd);
}

void HttpRequest::handleWriteToFile(void *data, size_t size)
{
    size_t n = fwrite(data, size, 1, fd);
    if (n != 1) {
        fprintf(stderr, "Cannot write to '%s': %s", filename.c_str(), strerror(errno));
        exit(1);
    }
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
    curlHandle = curl_easy_init();
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_USERAGENT, "smit-agent/1.0");
    curl_easy_setopt(curlHandle, CURLOPT_HEADERFUNCTION, headerCallback);

    slist = 0;
    slist = curl_slist_append(slist, "Accept: " APP_X_SMIT);
    if (!sessid.empty()) {
        std::string cookie = "Cookie: sessid=" + sessid;
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
