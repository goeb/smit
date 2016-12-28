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

#include <archive.h>
#include <archive_entry.h>
#include <stdint.h>
#include <set>

#include "renderingZip.h"
#include "renderingHtmlIssue.h"
#include "utils/logging.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "global.h"
#include "repository/db.h"
#include "project/Object.h"


static int sendZippedFile(const ContextParameters &ctx, struct archive *a, const std::string &filename, const std::string &data)
{
    LOG_DIAG("sendZippedFile: filename=%s, %lu bytes", filename.c_str(), L(data.size()));

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, filename.c_str());
    archive_entry_set_size(entry, data.size());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    int ret = archive_write_header(a, entry);
    if (ret != ARCHIVE_OK) {
        LOG_ERROR("archive_write_header error: %s", archive_error_string(a));
        return -1;
    }
    la_ssize_t n = archive_write_data(a, data.data(), data.size());
    if (n < 0) {
        LOG_ERROR("archive_write_data error: n=%d, %s", n, archive_error_string(a));
        return -2;
    }
    if ((size_t)n != data.size()) {
        LOG_ERROR("archive_write_data error (incomplete write): n=%d, %s", n, archive_error_string(a));
        return -3;
    }
    archive_entry_free(entry);
    return 0;
}


static int attachFiles(const ContextParameters &ctx, struct archive *a, const std::string &projectPath, const IssueCopy &issue)
{
    Entry *e = issue.first;
    std::set<std::string> sentFiles; // used to detect duplicated files (same name)
    while (e) {
        PropertiesIt files = e->properties.find(K_FILE);
        if (files != e->properties.end()) {
            std::list<std::string>::const_iterator f;
            FOREACH(f, files->second) {
                std::string fname = *f;
                std::string objectId = popToken(fname, '/');

                std::string objectsDir = projectPath + '/' + PATH_OBJECTS;
                std::string data;
                int n = Object::load(objectsDir, objectId, data);
                if (n < 0) {
                    LOG_ERROR("Cannot load attached file: %s", fname.c_str());
                    return -1;
                }

                // prefix with the issue id
                std::string fpath = issue.id + "/" + (*f);

                // check if same file was already added to the archive
                if (sentFiles.count(fpath) > 0) {
                    // already added, do not add twice
                    LOG_DIAG("attachFiles: filename=%s, already sent", fpath.c_str());

                    continue;
                }

                int ret = sendZippedFile(ctx, a, fpath, data);
                if (ret < 0) return -1;
                sentFiles.insert(fpath);
            }
        }

        e = e->getNext();
    }
    return 0; // success
}

static std::string buildHtml(const ContextParameters &ctx, const IssueCopy &issue)
{
    return "hello"; // TODO
}

static int startChunkedTransfer(struct archive *a, void *ctxData)
{
    LOG_DIAG("startChunkedTransfer");
    const ContextParameters *ctx = (ContextParameters *)ctxData;
    ctx->req->printf("Transfer-Encoding: chunked\r\n");
    return ARCHIVE_OK;
}

static la_ssize_t sendChunk(struct archive *a, void *ctxData, const void *buffer, size_t length)
{
    LOG_DIAG("sendChunk: length=%lu", L(length));
    const ContextParameters *ctx = (ContextParameters *)ctxData;
    ctx->req->printf("%x\r\n", length);
    ctx->req->write(buffer, length);
    ctx->req->printf("\r\n");
    return length;
}

static int closeChunkedTransfer(struct archive *a, void *ctxData)
{
    LOG_DIAG("closeChunkedTransfer");
    const ContextParameters *ctx = (ContextParameters *)ctxData;
    ctx->req->printf("0\r\n\r\n");

    return ARCHIVE_OK;
}

int RZip::printIssue(const ContextParameters &ctx, const IssueCopy &issue)
{
    LOG_DEBUG("RZip::printIssue...");
    ctx.req->printf("Content-Type: application/zip\r\n");

    std::string indexHtml = buildHtml(ctx, issue);

    struct archive *a = archive_write_new();
    archive_write_set_format_zip(a);
    int ret = archive_write_open(a, (void*)&ctx, startChunkedTransfer, sendChunk, closeChunkedTransfer);
    if (ret !=ARCHIVE_OK) {
        LOG_ERROR("archive_write_open error: %s", archive_error_string(a));
        ctx.req->printf("\r\n\r\nError in archive_write_open error: %s", archive_error_string(a));
    }

    std::string index = issue.id + "/index.html";
    sendZippedFile(ctx, a, index, indexHtml);    // TODO handle errors

    attachFiles(ctx, a, ctx.projectPath, issue);    // TODO handle errors

    ctx.req->printf("\r\n");

    ret = archive_write_free(a);
    if (ret != ARCHIVE_OK) {
        LOG_ERROR("archive_write_free error: %s", archive_error_string(a));
    }
    return 0;
}

