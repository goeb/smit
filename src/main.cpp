
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "mongoose.h"
#include "httpdHandlers.h"
#include "logging.h"
#include "db.h"
#include "cpio.h"
#include "session.h"


void usage()
{
    printf("Usage: smit <command> [<args>]\n"
           "\n"
           "Commands:\n"
           "    extract (debug only)\n"
           "The repository is a directory where the projects are stored.\n");
    exit(1);
}

void testEmbeddedString(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    fseek(f, -4, SEEK_END);
    uint8_t sizeString[4];
    size_t n = fread(sizeString, sizeof(sizeString), 1, f);
    uint32_t size;
    memcpy(&size, sizeString, sizeof(uint32_t));
    size = ntohl(size);
    printf("size=%u\n", size);

    fseek(f, -4-size, SEEK_END);
    char data[100+1];
    n = fread(data, size, 1, f);
    data[size] = 0;
    printf("data=%s\n", data);
    fclose(f);

}

int main(int argc, const char **argv)
{
    //testEmbeddedString(argv[0]);

    if (argc < 2) usage();

    if (0 == strcmp(argv[1], "extract")) {
        cpioExtractFile(argv[0], ".");
        exit(0);
    }

    int i = 0;
    const char *listenPort = "8080";
    const char *repo = 0;
    while (i<argc) {
        const char *arg = argv[i];
        i++;
        if (0 == strcmp(arg, "--port")) {
            if (i<argc) listenPort = argv[i];
        } else {
            repo = arg;
        }
    }

    if (!repo) usage();

    // Load all projects
    dbLoad(repo);
    UserBase::load(repo);
    struct mg_context *ctx;
    const char *options[] = {"listening_ports", listenPort, "document_root", repo, NULL};
    struct mg_callbacks callbacks;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_request = begin_request_handler;
    callbacks.upload = upload_handler;

    LOG_DEBUG("Starting httpd server on port %s", options[1]);
    ctx = mg_start(&callbacks, NULL, options);

    while (1) sleep(1); // block until ctrl-C
    getchar();  // Wait until user hits "enter"
    mg_stop(ctx);

    return 0;
}
