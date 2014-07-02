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

#include <string>
#include <list>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#include "clone.h"
#include "global.h"


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



size_t writeMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
size_t receiveLinesCallback(void *contents, size_t size, size_t nmemb, void *userp);

class HttpRequest {
public:
    HttpRequest();
    ~HttpRequest();
    void handleReceivedLines(const char *contents, size_t size);
    void handleReceivedRaw(void *data, size_t size);
    void setUrl(const char *u);
    void getRequestLines();
    void getRequestRaw();

private:
    void performRequest();
    std::string url;
    std::string response;
    std::list<std::string> lines;
    std::string currentLine;
    CURL *curlHandle;
    struct curl_slist *slist;
};

int getProjects(const char *rooturl)
{
    HttpRequest hr;

    hr.setUrl(rooturl);
    hr.getRequestLines();

    return 0;
}

void signin(const char *user, const char *passwd)
{
    printf("signin not implented. exiting...\n");
    exit(1);
}

int cmdClone(int argc, const char **argv)
{
    const char *user = 0;
    const char *passwd = 0;
    const char *url = 0;
    const char *dir = 0;

    int i = 0;
    while (i < argc) {
        const char *arg = argv[i];
        i++;
        if (0 == strcmp(arg, "--user")) {
            if (argc <= 0) return helpClone();
            user = argv[i];
            i++;
        } else if (0 == strcmp(arg, "--passwd")) {
            if (argc <= 0) return helpClone();
            passwd = argv[i];
            i++;
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

    if (!user || !passwd) {
        printf("You must specify user name and password.\n\n");
        return helpClone();
    }

    curl_global_init(CURL_GLOBAL_ALL);

    signin(user, passwd);
    // TODO

    getProjects(url);


    curl_global_cleanup();

    return 1;
}


void HttpRequest::setUrl(const char *u)
{
    url = u;
    curl_easy_setopt(curlHandle, CURLOPT_URL, u);
}


void HttpRequest::getRequestRaw()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    performRequest();
}


void HttpRequest::getRequestLines()
{
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, receiveLinesCallback);
    performRequest();
    handleReceivedLines(0, 0);

    // debug
    std::list<std::string>::iterator line;
    FOREACH(line, lines) {
        printf("%s\n", line->c_str());
    }


}
void HttpRequest::performRequest()
{
    CURLcode res;
    res = curl_easy_perform(curlHandle);

    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s (url %s)\n", curl_easy_strerror(res), url.c_str());
        exit(1);
    }
}

size_t writeMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HttpRequest *hr = (HttpRequest*)userp;
    size_t realsize = size * nmemb;
    hr->handleReceivedRaw(contents, realsize);
    return realsize;
}

void HttpRequest::handleReceivedRaw(void *data, size_t size)
{
    response.append((char*)data, size);
}


size_t receiveLinesCallback(void *contents, size_t size, size_t nmemb, void *userp)
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


HttpRequest::HttpRequest()
{
    curlHandle = curl_easy_init();
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)this);
    curl_easy_setopt(curlHandle, CURLOPT_USERAGENT, "smit-agent/1.0");

    slist = 0;
    slist = curl_slist_append(slist, "Accept: text/plain");
    curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, slist);

}

HttpRequest::~HttpRequest()
{
    curl_slist_free_all(slist);
    curl_easy_cleanup(curlHandle);
}


