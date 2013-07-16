// Copyright (c) 2004-2012 Sergey Lyubka
// This file is a part of mongoose project, http://github.com/valenok/mongoose

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define strtoll strtol
typedef __int64 int64_t;
#else
#include <inttypes.h>
#include <unistd.h>
#endif // !_WIN32

#include <string>
#include <sstream>
#include "mongoose.h"

#include "db.h"
#include "logging.h"
#include "identifiers.h"

std::string request2string(struct mg_connection *conn)
{
    const struct mg_request_info *request_info = mg_get_request_info(conn);

    const int SIZEX = 1000;
    char content[SIZEX+1];
    int content_length = snprintf(content, sizeof(content),
                                  "<pre>Hello from mongoose! Remote port: %d",
                                  request_info->remote_port);

    int L = content_length;
    L += snprintf(content+L, SIZEX-L, "request_method=%s\n", request_info->request_method);
    L += snprintf(content+L, SIZEX-L, "uri=%s\n", request_info->uri);
    L += snprintf(content+L, SIZEX-L, "http_version=%s\n", request_info->http_version);
    L += snprintf(content+L, SIZEX-L, "query_string=%s\n", request_info->query_string);
    L += snprintf(content+L, SIZEX-L, "remote_user=%s\n", request_info->remote_user);

    for (int i=0; i<request_info->num_headers; i++) {
        struct mg_request_info::mg_header H = request_info->http_headers[i];
        L += snprintf(content+L, SIZEX-L, "header: %30s = %s\n", H.name, H.value);
    }
    L += snprintf(content+L, SIZEX-L, "</pre>");

    return std::string(content);

}

static int begin_request_handler(struct mg_connection *conn) {

    const char *uri = mg_get_request_info(conn)->uri;
    LOG_DEBUG("uri=%s", uri);
    if (0 == strcmp(uri, "/admin")) {
        mg_printf(conn, "%s", "HTTP/1.0 200 OK\r\n\r\n");
        //mg_upload(conn, "/admin");
        mg_printf(conn, "%s", "admin TODO");
    } else  if (0 == strcmp(uri, "/signin")) {
        mg_printf(conn, "%s", "HTTP/1.0 200 OK\r\n\r\n");
        mg_printf(conn, "%s", "signin TODO");
    } else  if (0 == strcmp(uri, "/users")) {
        mg_printf(conn, "%s", "HTTP/1.0 200 OK\r\n\r\n");
        mg_printf(conn, "%s", "users TODO xxx");
        LOG_DEBUG("users TODO");
    } else {
        // project related URI
        // Show HTML form. Make sure it has enctype="multipart/form-data" attr.
        static const char *html_form =
                "<html><body>Upload example."
                "<form method=\"POST\" action=\"/handle_post_request\" "
                "  enctype=\"multipart/form-data\">"
                "<input type=\"file\" name=\"file\" /> <br/>"
                "<input type=\"submit\" value=\"Upload\" />"
                "</form></body></html>";

        std::string req = request2string(conn);

        mg_printf(conn, "HTTP/1.0 200 OK\r\n"
                  "Content-Length: %d\r\n"
                  "Content-Type: text/html\r\n\r\n%s%s",
                  (int) strlen(html_form)+req.size(), html_form, req.c_str());
    }

    // Mark request as processed
    return 1;
}

static void upload_handler(struct mg_connection *conn, const char *path) {
    mg_printf(conn, "Saved [%s]", path);
    std::string req = request2string(conn);
    mg_printf(conn, "%s", req.c_str());

}

int main(void) {

    computeIdBase34((uint8_t*)"toto", 4);


    init("src/repositories");
    struct mg_context *ctx;
    const char *options[] = {"listening_ports", "8080", NULL};
    struct mg_callbacks callbacks;

    ustring x = bin2hex(ustring((unsigned char*)"toto"));
    printf("bin2hex(toto)=%s\n", x.c_str());

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_request = begin_request_handler;
    callbacks.upload = upload_handler;
    ctx = mg_start(&callbacks, NULL, options);
    getchar();  // Wait until user hits "enter"
    mg_stop(ctx);

    return 0;
}
