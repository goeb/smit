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
#include "renderingZipHtml.h"
#include "renderingHtmlIssue.h"
#include "utils/logging.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "global.h"
#include "repository/db.h"
#include "restApi.h"

static int sendZippedFile(const ContextParameters &ctx, struct archive *a, const std::string &filename, const std::string &data)
{
    LOG_DIAG("sendZippedFile: filename=%s, %lu bytes", filename.c_str(), L(data.size()));

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, filename.c_str());
    archive_entry_set_size(entry, data.size());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);

    int ret = archive_write_header(a, entry);

    archive_entry_free(entry);

    if (ret != ARCHIVE_OK) {
        LOG_ERROR("archive_write_header error: %s (%s, %ld)",
                  archive_error_string(a), filename.c_str(), L(data.size()));
        return -1;
    }
    ssize_t n = archive_write_data(a, data.data(), data.size());
    if (n < 0) {
        LOG_ERROR("archive_write_data error: n=%ld, %s (%s, %ld)",
                  L(n), archive_error_string(a), filename.c_str(), L(data.size()));
        return -2;
    }
    if ((size_t)n != data.size()) {
        LOG_ERROR("archive_write_data error (incomplete write): n=%ld, %s (%s, %ld)",
                  L(n), archive_error_string(a), filename.c_str(), L(data.size()));
        return -3;
    }
    return 0;
}


static int attachFiles(const ContextParameters &ctx, struct archive *a, const std::string &projectPath, const IssueCopy &issue)
{
    std::set<std::string> sentFiles; // used to detect duplicated files (same name)
    std::vector<Entry>::const_iterator e;
    FOREACH(e, issue.entries) {

        std::list<AttachedFileRef>::const_iterator f;
        FOREACH(f, e->files) {

            std::string data;
//load the file
            GitObject object(projectPath, f->id);
            int err = object.open();
            // TODO handle err

            const int BUF_SIZ = 4096;
            char buffer[BUF_SIZ];

            while (1) {
                int n = object.read(buffer, BUF_SIZ);
                if (n < 0) {
                    // error
                    LOG_ERROR("xxxxxxxxxxxx Error while sending object %s (%s)", f->id.c_str(), projectPath.c_str());
                    // TODO how to deal with this server side error ?
                    break;
                }
                if (n == 0) break; // end of file
                data.append(buffer, n);
            }
            object.close();

            // prefix with the issue id
            std::string fpath = issue.id + "/" RESOURCE_FILES  "/" + f->id + "/" + f->filename;

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
    return 0; // success
}

static std::string buildHtml(const ContextParameters &ctx, const IssueCopy &issue)
{
    StringStream oss;
    oss.printf(HTML_HEADER, issue.id.c_str());

    oss.printf("<h1>Smit project: %s</h1>", htmlEscape(ctx.projectName).c_str());

    // summary
    oss.printf("<div class=\"sm_issue_header\">\n");
    oss.printf("<span class=\"sm_issue_id\">%s</span>\n", htmlEscape(issue.id).c_str(),
               htmlEscape(issue.id).c_str());
    oss.printf("<span class=\"sm_issue_summary\">%s</span>\n", htmlEscape(issue.getSummary()).c_str());
    oss.printf("</div>\n");

    // properties
    oss << "<div class=\"sm_issue\">";
    std::string pt = RHtmlIssue::renderPropertiesTable(ctx, issue, true);
    oss << pt;

    // tags
    std::string tags = RHtmlIssue::renderTags(ctx, issue);
    oss << tags;

    // entries
    std::vector<Entry>::const_iterator e;
    FOREACH(e, issue.entries) {
        std::string entry = RHtmlIssue::renderEntry(ctx, issue, *e, FLAG_ENTRY_OFFLINE);
        oss << entry;
    } // end of entries

    oss << "</div>\n";

    oss << HTML_FOOTER;
    return oss.str();
}

static int startChunkedTransfer(struct archive *a, void *ctxData)
{
    LOG_DIAG("startChunkedTransfer");
    return ARCHIVE_OK;
}

static ssize_t sendChunk(struct archive *a, void *ctxData, const void *buffer, size_t length)
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
    return ARCHIVE_OK;
}

static void finalizeChunkedTransfer(const ContextParameters &ctx)
{
    LOG_DIAG("finalizeChunkedTransfer");
    ctx.req->printf("0\r\n\r\n");
}

int RZip::printIssue(const ContextParameters &ctx, const IssueCopy &issue)
{
    LOG_DEBUG("RZip::printIssue...");
    ctx.req->printf("Content-Type: application/zip\r\n");
    ctx.req->printf("Content-Disposition: attachment; filename=\"%s.zip\"\r\n", issue.id.c_str());
    ctx.req->printf("Transfer-Encoding: chunked\r\n");
    ctx.req->printf("\r\n"); // end of HTTP headers

    std::string indexHtml = buildHtml(ctx, issue);

    struct archive *a = archive_write_new();

    if (!a) {
        LOG_ERROR("archive_write_set_format_zip error: %s", archive_error_string(a));
        finalizeChunkedTransfer(ctx);
        return -1;
    }

    int ret = archive_write_set_format_zip(a);

    if (ret != ARCHIVE_OK) {
        LOG_ERROR("archive_write_set_format_zip error: %s", archive_error_string(a));
        archive_write_free(a);
        finalizeChunkedTransfer(ctx);
        return -1;
    }

    ret = archive_write_open(a, (void*)&ctx, startChunkedTransfer, sendChunk, closeChunkedTransfer);

    if (ret != ARCHIVE_OK) {
        LOG_ERROR("archive_write_open error: %s", archive_error_string(a));
        archive_write_free(a);
        finalizeChunkedTransfer(ctx);
        return -1;
    }

    std::string index = issue.id + "/issues/" + issue.id + ".html";
    ret = sendZippedFile(ctx, a, index, indexHtml);

    if (ret != 0) {
        archive_write_free(a);
        finalizeChunkedTransfer(ctx);
        return -1;
    }

    ret = attachFiles(ctx, a, ctx.projectPath, issue);    // TODO handle errors

    if (ret != 0) {
        archive_write_free(a);
        finalizeChunkedTransfer(ctx);
        return -1;
    }

    std::string styleCss = issue.id + "/style.css";
    ret = sendZippedFile(ctx, a, styleCss, HTML_STYLES);

    if (ret != 0) {
        archive_write_free(a);
        finalizeChunkedTransfer(ctx);
        return -1;
    }

    ret = archive_write_free(a);

    if (ret != ARCHIVE_OK) {
        LOG_ERROR("archive_write_free error: %s", archive_error_string(a));
    }

    finalizeChunkedTransfer(ctx);

    return 0;
}

