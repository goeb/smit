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
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>

#include "httpClient.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"
#include "console.h"
#include "filesystem.h"

bool HttpRequest::Verbose = false;

#define LOGV(...) do { if (HttpRequest::Verbose) { printf(__VA_ARGS__); fflush(stdout);} } while (0)

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

int HttpRequest::getFileStdout()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, getStdoutCallback);
    performRequest();
}

int HttpRequest::downloadFile(const std::string localPath)
{
    LOGV("downloadFile: resourcePath=%s\n", resourcePath.c_str());
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, downloadCallback);
    filename = localPath;
    performRequest();
    if (fd) closeFile();
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
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeToFileOrDirCallback);

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
        handleReceiveFileOrDirectory(0, 0);
        if (!fd) { // defensive programming
            fprintf(stderr, "unexpected null file descriptor\n");
        } else closeFile();
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

size_t HttpRequest::writeToFileOrDirCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;
    hr->handleReceiveFileOrDirectory(contents, realsize);
    return realsize;
}

size_t HttpRequest::getStdoutCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    write(1, contents, realsize);
    return realsize;
}

size_t HttpRequest::downloadCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;
    hr->handleDownload(contents, realsize);
    return realsize;
}


void HttpRequest::handleReceivedRaw(void *data, size_t size)
{
    response.append((char*)data, size);
}

void HttpRequest::openFile()
{
    if (filename.empty()) {
        if (repository.size() > 0) filename = repository + resourcePath; // resourcePath has a starting '/'
        else if (downloadDir.size() > 0) filename = downloadDir + "/" + getBasename(resourcePath);
        else {
            fprintf(stderr, "Error: repository and downloadDir are both empty strings\n");
            exit(1);
        }
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

/** Handle bytes received
  *
  * The bytes may either be part of a directory listing or a file.
  */
void HttpRequest::handleReceiveFileOrDirectory(void *data, size_t size)
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

void HttpRequest::handleDownload(void *data, size_t size)
{
    if (!fd) openFile();

    if (size) {
        size_t n = fwrite(data, size, 1, fd);
        if (n != 1) {
            fprintf(stderr, "Cannot write to '%s': %s\n", filename.c_str(), strerror(errno));
            exit(1);
        }
    } // else the size is zero, an empty file has been created
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
