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
#include "logging.h"

HttpClientContext::HttpClientContext()
{
    tlsCacert = 0;
    tlsInsecure = false;
}

void HttpRequest::setUrl(const std::string &_url)
{
    url = _url;
    curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str());
}

void HttpRequest::getFileStdout()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, getStdoutCallback);
    performRequest();

}

/** Dowload a file
  *
  * @return
  *     0, regular file downloaded successfully
  *     1, directory listing downloaded successfully
  *    -1, error, file not downloaded
  */
int HttpRequest::downloadFile(const HttpClientContext &ctx,
                              const std::string &url, const std::string &localPath)
{
    HttpRequest hr(ctx);
    hr.setUrl(url);
    int r = hr.downloadFile(localPath);
    return r;
}

/** Dowload a file
  *
  * @return
  *     0, regular file downloaded successfully
  *     1, directory listing downloaded successfully
  *    -1, error, file not downloaded
  */
int HttpRequest::downloadFile(const std::string &localPath)
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, downloadCallback);

    mkdirs(getDirname(localPath));

    // download to tmp file, then rename at the end.
    std::string tmp = getTmpPath(localPath);
    filename = tmp;
    performRequest();
    if (fd) closeFile();
    else {
        // if fd was not open, it is because:
        // - either there was an error (remote file does not exist)
        // - or the file is empty (the callback for storing received data was never called)
        if (httpStatusCode == 200) {
            // case of an empty file
            // create the empty file locally
            openFile();
            closeFile();
        }
    }
    if (httpStatusCode == 200) {
        int r = rename(tmp.c_str(), localPath.c_str());
        if (r != 0) {
            LOG_ERROR("Cannot rename after download '%s' -> '%s': %s",
                      tmp.c_str(), localPath.c_str(), strerror(errno));
            exit(1);
        }
        if (isDirectory) return 1;
        else return 0;

    } else {
        return -1;
    }
}

void HttpRequest::post(const std::string &params)
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, ignoreResponseCallback);
    curl_easy_setopt(curlHandle, CURLOPT_POSTFIELDS, params.c_str());
    performRequest();
}

int HttpRequest::head(const std::string &url)
{
    CURLcode res;
    /* upload to this place */
    curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curlHandle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curlHandle, CURLOPT_CUSTOMREQUEST, "HEAD");

    /* enable verbose for easier tracing */
    if (getLoggingLevel() > LL_INFO) curl_easy_setopt(curlHandle, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curlHandle);

    /* Check for errors */
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
        return -1;

    }

    return 0; // ok

}

/** Post a file (file upload)
  *
  * @param destUrl
  *     Must be a complete URI. Eg: http://example.com/x/y
  */

int HttpRequest::postFile(const std::string &srcFile, const std::string &destUrl)
{
    CURLcode res;
    struct stat file_info;
    FILE *fd;

    fd = fopen(srcFile.c_str(), "rb");

    if (!fd) {
        fprintf(stderr, "Cannot open file '%s': %s\n", srcFile.c_str(), strerror(errno));
        return -1;
    }
    /* to get the file size */
    if (fstat(fileno(fd), &file_info) != 0) {
        fprintf(stderr, "Cannot open file '%s': %s\n", srcFile.c_str(), strerror(errno));
        fclose(fd);
        return -1;
    }


    LOG_DEBUG("file '%s': size %ldo", srcFile.c_str(), file_info.st_size);

    /* upload to this place */
    curl_easy_setopt(curlHandle, CURLOPT_URL, destUrl.c_str());

    /* tell it to "upload" to the URL */
    curl_easy_setopt(curlHandle, CURLOPT_UPLOAD, 1L);

    curl_easy_setopt(curlHandle, CURLOPT_CUSTOMREQUEST, "POST");
    headerList = curl_slist_append(headerList, "Content-type: application/octet-stream");
    curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, headerList);

    /* set where to read from (on Windows you need to use READFUNCTION too) */ // TODO
    curl_easy_setopt(curlHandle, CURLOPT_READDATA, fd);

    /* and give the size of the upload (optional) */
    curl_easy_setopt(curlHandle, CURLOPT_INFILESIZE, (curl_off_t)file_info.st_size);

    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, receiveLinesCallback);

    /* enable verbose for easier tracing */
    if (getLoggingLevel() > LL_INFO) curl_easy_setopt(curlHandle, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curlHandle);
    fclose(fd);

    /* Check for errors */
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
        return -1;

    } else if (httpStatusCode < 200 || httpStatusCode >= 300) {
        LOG_ERROR("Got HTTP status %d", httpStatusCode);
        return -1;

    } else {
        // ok

        handleReceivedLines(0, 0); // finalize the last line of the response

        /* now extract transfer info */
        double speedUpload, totalTime;
        curl_easy_getinfo(curlHandle, CURLINFO_SPEED_UPLOAD, &speedUpload);
        curl_easy_getinfo(curlHandle, CURLINFO_TOTAL_TIME, &totalTime);

        LOG_DEBUG("Speed: %.3f bytes/sec during %.3f seconds", speedUpload, totalTime);

    }

    return 0; // ok
}

/** TODO not used
  * TODO remove putFile
  */
int HttpRequest::putFile(const std::string &srcFile, const std::string &destUrl)
{
    CURLcode res;
    FILE *fd ;
    struct stat file_info;

    /* get the file size of the local file */
    stat(srcFile.c_str(), &file_info);

    /* get a FILE * of the same file, could also be made with
       fdopen() from the previous descriptor, but hey this is just
       an example! */
    fd = fopen(srcFile.c_str(), "rb");

    /* enable uploading */
    curl_easy_setopt(curlHandle, CURLOPT_UPLOAD, 1L);

    /* HTTP PUT please */
    curl_easy_setopt(curlHandle, CURLOPT_PUT, 1L);

    /* specify target URL, and note that this URL should include a file
         name, not only a directory */
    curl_easy_setopt(curlHandle, CURLOPT_URL, destUrl.c_str());

    /* now specify which file to upload */
    curl_easy_setopt(curlHandle, CURLOPT_READDATA, fd);

    /* provide the size of the upload, we specicially typecast the value
         to curl_off_t since we must be sure to use the correct data size */
    curl_easy_setopt(curlHandle, CURLOPT_INFILESIZE_LARGE,
                     (curl_off_t)file_info.st_size);

    /* Now run off and do what you've been told! */
    res = curl_easy_perform(curlHandle);
    /* Check for errors */
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));

    fclose(fd); /* close the local file */
    return 0;
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
    LOG_DEBUG("cookie: %s = %s", name.c_str(), value.c_str());
    return 0;
}

void HttpRequest::performRequest()
{
    CURLcode res;

    LOG_DEBUG("resource: %s", url.c_str());
    res = curl_easy_perform(curlHandle);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s (%s)\n", curl_easy_strerror(res),
                url.c_str());
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

    LOG_DEBUG("HDR: %s", (char*)contents);

    if (hr->httpStatusCode == -1) {
        // this header should be the HTTP response code
        std::string code = (char*)contents;
        popToken(code, ' ');
        std::string reponseCode = popToken(code, ' ');
        hr->httpStatusCode = atoi(reponseCode.c_str());

        if (hr->httpStatusCode < 200 && hr->httpStatusCode > 299) {
            fprintf(stderr, "%s: HTTP status code %d. Exiting.\n", hr->url.c_str(), hr->httpStatusCode);
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
        fprintf(stderr, "Error: filename empty\n");
        exit(1);
    }
    LOG_DEBUG("Opening file: %s", filename.c_str());
    fd = fopen(filename.c_str(), "wb");
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
    if (httpStatusCode != 200) return;

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
    // if error, then do not store anything in the file
    if (httpStatusCode != 200) return;

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
        // do not push empty lines if it is a diectory listing
        if (!isDirectory || !currentLine.empty()) lines.push_back(currentLine);
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
            // do not push empty lines if it is a diectory listing
            if (!isDirectory || !currentLine.empty()) lines.push_back(currentLine);
            currentLine.clear();
            notConsumedOffset = i+1;
        }
        i++;
    }
    if (notConsumedOffset < size) currentLine.append(data, notConsumedOffset, size-notConsumedOffset);
}

HttpRequest::HttpRequest(const HttpClientContext &ctx)
{
    httpCtx = ctx;
    isDirectory = false;
    fd = 0;
    httpStatusCode = -1; // not set
    curlHandle = curl_easy_init();
    curl_easy_setopt(curlHandle, CURLOPT_USERAGENT, "smit-agent/1.0");
    curl_easy_setopt(curlHandle, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curlHandle, CURLOPT_HEADERDATA, (void *)this);

    if (httpCtx.tlsCacert) curl_easy_setopt(curlHandle, CURLOPT_CAINFO, httpCtx.tlsCacert);
    else if (httpCtx.tlsInsecure) curl_easy_setopt(curlHandle, CURLOPT_SSL_VERIFYPEER, 0);

    headerList = 0;
    headerList = curl_slist_append(headerList, "Accept: " APP_X_SMIT);
    if (!httpCtx.sessid.empty()) {
        std::string cookie = "Cookie: " SESSID "=" + httpCtx.sessid;
        headerList = curl_slist_append(headerList, cookie.c_str());
    }

    headerList = curl_slist_append(headerList, "Expect:"); // mongoose does not support Expect

    curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, headerList);

    curl_easy_setopt(curlHandle, CURLOPT_COOKIEFILE, ""); /* just to start the cookie engine */
}

HttpRequest::~HttpRequest()
{
    if (headerList) curl_slist_free_all(headerList);
    if (curlHandle) curl_easy_cleanup(curlHandle);
}

void HttpRequest::closeCurl()
{
    curl_slist_free_all(headerList);
    headerList = 0;
    curl_easy_cleanup(curlHandle);
    curlHandle = 0;
}
