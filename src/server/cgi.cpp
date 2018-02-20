
#include <string>

#include "cgi.h"
#include "global.h"
#include "httpdUtils.h"
#include "utils/subprocess.h"
#include "utils/logging.h"
#include "utils/stringTools.h"

struct HttpHeader {
    std::string name;
    std::string value;
};

void launchCgi(const RequestContext *req, const std::string &exePath, Argv envp)
{
    Argv argv;
    argv.set(exePath.c_str(), 0);

    // Add all headers as HTTP_* variables
    std::string key, value;
    int i = 0;
    while (req->getHeader(i, key, value)) {
        // convert upper case, and change - to _
        std::string::iterator c;
        FOREACH(c, key) {
            if (*c == '-') *c = '_';
            else *c = ::toupper(*c);
        }

        std::string var = "HTTP_" + key + "=" + value;
        envp.append(var.c_str(), 0);

        i++;
    }

    std::string dir = getDirname(exePath); // CGI must be laucnhed in its directory

    Subprocess *subp = Subprocess::launch(argv.getv(), envp.getv(), dir.c_str());
    if (!subp) {
        LOG_ERROR("Cannot launch CGI: %s", exePath.c_str());
        sendHttpHeader500(req, "cannot launch git backend");
        return;
    }

    LOG_DIAG("launchCgi: CGI launched");

    // Send data from client to the CGI
    const int SIZ = 4096;
    char datachunk[SIZ];
    int n; // number of bytes read

    if (std::string("POST") == req->getMethod()) {
        while ( (n = req->read(datachunk, SIZ)) > 0) {
            subp->write(datachunk, n);
        }
    }

    LOG_DIAG("launchCgi: closeStdin");
    subp->closeStdin();

    // read the header from the CGI
    std::list<HttpHeader> headers;
    std::string statusText = "OK";
    int status = 200;
    // and process some HTTP headers
    while (1) {
        std::string line = subp->getline();
        LOG_DIAG("launchCgi: got line %s", line.c_str());

        if (line.empty() || line == "\r\n") break; // end of headers

        HttpHeader hh;
        hh.name = popToken(line, ':', TOK_TRIM_BOTH);
        trim(line); // remove surrounding blanks and ending CR or LF
        hh.value = line;

        if (hh.name == "Status") {
            // eg : Status:  404 Not Found
            // => we need to capture status=404 and statusText="Not Found"
            std::string statusStr = popToken(line, ' ', TOK_TRIM_BOTH);
            status = atoi(statusStr.c_str());
            statusText = line;
        }

        headers.push_back(hh);
    }

    LOG_DIAG("launchCgi: send status line");

    // send the status line
    req->printf("HTTP/1.1 %d %s\r\n", status, statusText.c_str());

    // send the headers
    // process the headers from the CGI before sending to the client
    std::list<HttpHeader>::iterator header;
    FOREACH(header, headers) {
        req->printf("%s: %s\r\n", header->name.c_str(), header->value.c_str());
    }

    // end of headers
    req->write("\r\n", 2);

    // read the remaining bytes and send back to the client
    while ( (n = subp->read(datachunk, SIZ)) > 0) {
        LOG_DIAG("launchCgi: send response to client: %ld bytes", L(n));
        req->write(datachunk, n);
    }

    std::string subpStderr = subp->getStderr();
    int err = subp->wait();
    delete subp;
    if (err || !subpStderr.empty()) {
        LOG_ERROR("launchCgi: err=%d, stderr=%s", err, subpStderr.c_str());
    } else {
        LOG_DIAG("launchCgi: err=%d", err);
    }
}
