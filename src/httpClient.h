#ifndef _httpClient_h
#define _httpClient_h

#include <string>
#include <map>
#include <list>

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
    int downloadFile(const std::string &resource, const std::string localPath);

    int test();
    void handleReceiveFileOrDirectory(void *data, size_t size);
    void handleDownload(void *data, size_t size);
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
    static size_t downloadCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t writeToFileOrDirCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t headerCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static size_t ignoreResponseCallback(void *contents, size_t size, size_t nmemb, void *userp);

    int httpStatusCode;
    static inline void setVerbose(bool v) {Verbose = v;}
    static bool Verbose;

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


#endif