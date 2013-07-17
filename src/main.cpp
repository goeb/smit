
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "mongoose.h"
#include "httpdHandlers.h"
#include "db.h"


void usage()
{
    printf("Usage: smit <repository>\n"
           "\n"
           "The repository is a directory where the projects are stored.\n");
    exit(1);
}

int main(int argc, const char **argv)
{

    if (argc != 2) usage();


    // Load all projects
    db_init(argv[1]);
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
