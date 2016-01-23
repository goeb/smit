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

#include <stdlib.h>
#include <sstream>
#include <string.h>
#include <set>
#include <stdarg.h>

#include "renderingHtmlIssue.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "stringTools.h"
#include "dateTools.h"
#include "global.h"
#include "filesystem.h"
#include "restApi.h"
#include "jTools.h"


/** Add "filterout" to the query string
 */
std::string getQsAddFilterOut(std::string qs, const std::string &propertyName, const std::string &propertyValue)
{
	// TODO be more smart and do not add filterout if already there (but it might never happen)
	qs += "&filterout=" + urlEncode(propertyName) + ":" + urlEncode(propertyValue);
	return qs;
}

/** Modify the query string by removing the given property from the colspec
  *
  * @param qs
  *    The input query string
  *
  * @param property
  *    The property to be removed
  *
  * @param defaultCols
  *    The default list of columns to use, when no colspec is contained in the query string
  *
  */
std::string getQsRemoveColumn(std::string qs, const std::string &property, const std::list<std::string> &defaultCols)
{
    LOG_DEBUG("getQsRemoveColumn: in=%s", qs.c_str());
    std::string result;

    const char *COLSPEC_HEADER = "colspec=";
    bool colspecFound = false;

    while (qs.size() > 0) {
        std::string part = popToken(qs, '&');
        if (0 == strncmp(COLSPEC_HEADER, part.c_str(), strlen(COLSPEC_HEADER))) {
            // This is the colspec part, that we want to alter
            colspecFound = true;

            popToken(part, '=');
            std::list<std::string> cols = split(part, " +");
            cols.remove(property);
            // build the new colspec
            if (cols.empty()) part = "";
            else {
                std::string newColspec = join(cols, "+");
                part = COLSPEC_HEADER + newColspec;
            }

        } // else, keep the part unchanged

        // append to the result
        if (!part.empty()) {
            if (result == "") result = part;
            else result = result + '&' + part;
        }
    }
    if (!colspecFound) {
        // do as if the query string had a colspec equal to defaultCols
        std::list<std::string> cols = defaultCols; // make a copy
        cols.remove(property);
        if (!cols.empty()) {
            std::string newColspec = join(cols, "+");
            std::string part = COLSPEC_HEADER + newColspec;
            if (result == "") result = part;
            else result = result + '&' + part;
        }
    }

    LOG_DEBUG("getQsRemoveColumn: result=%s", result.c_str());

    return result;
}


/** Build a new query string based on the current one, and update the sorting part
  *
  *
  * @param qs
  *     The query string to be modified.
  *
  * Example:
  * In: current query string is sort=xx&filterin=...&whatever...
  *     property name is 'assignee'
  * Out when exclusive is true:
  *     sort=assignee&filterin=...&whatever...
  * Out when exclusive is false:
  *     sort=xx+assignee&filterin=...&whatever...
  *
  * When exclusive is false, the existing sorting is kept, except that:
  * - if the property is already present, its order is inverted (+/-)
  * - else, the property is added after the others
  *
  */
std::string getQsSubSorting(std::string qs, const std::string &property, bool exclusive)
{
    LOG_DEBUG("getSubSortingSpec: in=%s, exclusive=%d", qs.c_str(), exclusive);
    std::string result;

    const char *SORT_SPEC_HEADER = "sort=";
    std::string newSortingSpec = "";

    while (qs.size() > 0) {
        std::string part = popToken(qs, '&');
        if (0 == strncmp(SORT_SPEC_HEADER, part.c_str(), strlen(SORT_SPEC_HEADER))) {
            // This is the sorting spec part, that we want to alter
            popToken(part, '=');

            if (exclusive) {
                newSortingSpec = SORT_SPEC_HEADER;
                newSortingSpec += property;
            } else {
                size_t len = part.size();
                size_t currentOffset = 0;
                std::string currentPropertyName;
                char currentOrder = '+';
                bool propertyFound = false; // detect if specified property is already present
                while (currentOffset < len) {
                    char c = part[currentOffset];
                    if (c == '+' || c == ' ' || c == '-' ) {
                        if (currentPropertyName.size()>0) {
                            // store property name
                            if (currentPropertyName == property) {
                                if (currentOrder == '+') currentOrder = '-';
                                else currentOrder = '+';
                                propertyFound = true;
                            }
                            newSortingSpec += currentOrder;
                            newSortingSpec += currentPropertyName;
                            currentPropertyName = "";
                        }
                        currentOrder = c;
                    } else {
                        currentPropertyName += c;
                    }
                    currentOffset++;
                }
                // flush remaining
                if (currentPropertyName.size()>0) {
                    // store property name
                    if (currentPropertyName == property) {
                        if (currentOrder == '+') currentOrder = '-';
                        else currentOrder = '+';
                        propertyFound = true;
                    }
                    newSortingSpec += currentOrder;
                    newSortingSpec += currentPropertyName;

                }
                if (!propertyFound) {
                    // add it at the end
                    newSortingSpec += '+';
                    newSortingSpec += property;
                }

                newSortingSpec = newSortingSpec.insert(0, SORT_SPEC_HEADER); // add the "sort=" at the beginning
            }
            part = newSortingSpec;
        } // else, keep the part unchanged

        // append to the result
        if (result == "") result = part;
        else result = result + '&' + part;
    }

    if (newSortingSpec.empty()) {
        // no previous sort=...
        // add one
        newSortingSpec = "sort=";
        newSortingSpec += property;
        if (result == "") result = newSortingSpec;
        else result = result + '&' + newSortingSpec;
    }
    LOG_DEBUG("getSubSortingSpec: result=%s", result.c_str());

    return result;
}

/** Get the property name that will be used for grouping
  *
  * Grouping will occur only :
  *     - on the first property sorted-by
  *     - and if this property is of type select, multiselect or selectUser
  *
  * If the grouping must not occur, then an empty  string is returned.
  *
  * @param[out] type
  *      type of the first property sorted-by
  *      undefined if property name not found
  */
std::string getPropertyForGrouping(const ProjectConfig &pconfig, const std::string &sortingSpec, PropertyType &type)
{
    const char* colspecDelimiters = "+- ";
    if (sortingSpec.empty()) return "";

    size_t i = 0;
    if (strchr(colspecDelimiters, sortingSpec[0])) i = 1;

    // get the first property sorted-by
    std::string property;
    size_t n = sortingSpec.find_first_of(colspecDelimiters, i);
    if (n == std::string::npos) property = sortingSpec.substr(i);
    else property = sortingSpec.substr(i, n-i);

    // check the type of this property
    if (property == "p") return property; // enable grouping by project name

    const PropertySpec *propertySpec = pconfig.getPropertySpec(property);
    if (!propertySpec) return "";
    type = propertySpec->type;

    if (type == F_SELECT || type == F_MULTISELECT || type == F_SELECT_USER) return property;
    return "";
}

void printFilterLogicalExpr(const RequestContext *req,
                            const std::map<std::string, std::list<std::string> > &filter, FilterMode mode)
{
    std::map<std::string, std::list<std::string> >::const_iterator f;
    FOREACH(f, filter) {
        if (f != filter.begin()) {
            if (mode == FILTER_IN) req->printf(" AND ");
            else req->printf(" OR ");
        }
        req->printf("(%s: ", htmlEscape(f->first).c_str()); // print property name
        std::list<std::string>::const_iterator value;
        FOREACH(value, f->second) {
            if (value != f->second.begin()) req->printf(" OR ");
            req->printf("%s", htmlEscape(*value).c_str());
        }
        req->printf(")");
    }
}

/** print chosen filters and search parameters
  */
void printFilters(const ContextParameters &ctx)
{
    if (    ctx.search.empty() &&
            ctx.filterin.empty() &&
            ctx.filterout.empty() &&
            ctx.sort.empty()) {
        return;
    }
    ctx.req->printf("<div class=\"sm_issues_filters\">");
    if (!ctx.search.empty()) ctx.req->printf("<span class=\"sm_issues_filters\">search:</span> %s<br>", htmlEscape(ctx.search).c_str());
    if (!ctx.filterin.empty()) {
        ctx.req->printf("<span class=\"sm_issues_filters\">filterin:</span> ");
        printFilterLogicalExpr(ctx.req, ctx.filterin, FILTER_IN);
        ctx.req->printf("<br>");
    }
    if (!ctx.filterout.empty()) {
        ctx.req->printf("<span class=\"sm_issues_filters\">filterout:</span> ");
        printFilterLogicalExpr(ctx.req, ctx.filterout, FILTER_OUT);
        ctx.req->printf("<br>");
    }
    if (!ctx.sort.empty())  ctx.req->printf("<span class=\"sm_issues_filters\">sort:</span> %s<br>", htmlEscape(ctx.sort).c_str());
    ctx.req->printf("</div>");
}

void RHtmlIssue::printIssueListFullContents(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList)
{
    ctx.req->printf("<div class=\"sm_issues\">\n");

    printFilters(ctx);
    // number of issues
    ctx.req->printf("<div class=\"sm_issues_count\">%s: <span class=\"sm_issues_count\">%lu</span></div>\n",
                    _("Issues found"), L(issueList.size()));

    std::vector<IssueCopy>::const_iterator i;
    FOREACH (i, issueList) {
        const IssueCopy &issue = *i;
        // deactivate user role, as no edit form is to be displayed
        ContextParameters ctxCopy = ctx;
        ctxCopy.userRole = ROLE_RO;
        printIssueSummary(ctxCopy, issue);
        printIssue(ctxCopy, issue, "");
    }
    ctx.req->printf("</div>\n");

}

/** concatenate a param to the query string (add ? or &)
  */
std::string queryStringAdd(const RequestContext *req, const char *param)
{
    std::string qs = req->getQueryString();

    if (!param) return qs;

    std::string url = "?";
    if (qs.size() > 0) url = url + qs + '&' + param;
    else url += param;

    return url;
}

void RHtmlIssue::printIssueList(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList,
                           const std::list<std::string> &colspec, bool showOtherFormats)
{
    ctx.req->printf("<div class=\"sm_issues\">\n");

    // add links to alternate download formats (CSV and full-contents)
    if (showOtherFormats) {
        ctx.req->printf("<div class=\"sm_issues_other_formats\">");
        ctx.req->printf("<a href=\"%s\" class=\"sm_issues_other_formats\">csv</a> ",
                        queryStringAdd(ctx.req, "format=csv").c_str());
        ctx.req->printf("<a href=\"%s\" class=\"sm_issues_other_formats\">full-contents</a></div>\n",
                        queryStringAdd(ctx.req, "full=1").c_str());
    }

    printFilters(ctx);

    // number of issues
    ctx.req->printf("<div class=\"sm_issues_count\">%s: <span class=\"sm_issues_count\">%lu</span></div>\n",
                    _("Issues found"), L(issueList.size()));

    PropertyType groupPropertyType;
    std::string group = getPropertyForGrouping(ctx.projectConfig, ctx.sort, groupPropertyType);
    std::string currentGroup;

    ctx.req->printf("<table class=\"sm_issues\">\n");

    // print header of the table
    ctx.req->printf("<tr class=\"sm_issues\">\n");
    std::list<std::string>::const_iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {

        std::string label = ctx.projectConfig.getLabelOfProperty(*colname);
        std::string newQueryString = getQsSubSorting(ctx.req->getQueryString(), *colname, true);
        ctx.req->printf("<th class=\"sm_issues\"><a class=\"sm_issues_sort\" href=\"?%s\" title=\"Sort ascending\">%s</a>\n",
                        newQueryString.c_str(), htmlEscape(label).c_str());
        newQueryString = getQsSubSorting(ctx.req->getQueryString(), *colname, false);
        ctx.req->printf("\n<br><a href=\"?%s\" class=\"sm_issues_sort_cumulative\" ", newQueryString.c_str());
        ctx.req->printf("title=\"%s\">&gt;&gt;&gt;</a>",
                        _("Sort while preserving order of other columns\n(or invert current column if already sorted-by)"));

        std::list<std::string> defaultCols = ctx.projectConfig.getPropertiesNames();
        newQueryString = getQsRemoveColumn(ctx.req->getQueryString(), *colname, defaultCols);
        ctx.req->printf(" <a href=\"?%s\" class=\"sm_issues_delete_col\" title=\"%s\">&#10008;</a>\n", newQueryString.c_str(),
                        _("Hide this column"));
        ctx.req->printf("</th>\n");
    }
    ctx.req->printf("</tr>\n");

    // print the rows of the issues
    std::vector<IssueCopy>::const_iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        if (! group.empty() &&
                (i == issueList.begin() || i->getProperty(group) != currentGroup) ) {
            // insert group bar if relevant
            ctx.req->printf("<tr class=\"sm_issues_group\">\n");
            currentGroup = i->getProperty(group);
            ctx.req->printf("<td class=\"sm_group\" colspan=\"%lu\"><span class=\"sm_issues_group_label\">%s: </span>",
                            L(colspec.size()), htmlEscape(ctx.projectConfig.getLabelOfProperty(group)).c_str());
            ctx.req->printf("<span class=\"sm_issues_group\">%s</span>", htmlEscape(currentGroup).c_str());

            if (groupPropertyType == F_SELECT_USER || groupPropertyType == F_SELECT) {
                // add an x to let the user hide this group (via a filterout)
                std::string newQueryString = getQsAddFilterOut(ctx.req->getQueryString(), group, currentGroup);
                ctx.req->printf(" <a href=\"?%s\" class=\"sm_issues_delete_col\" title=\"%s\">&#10008;</a>\n", newQueryString.c_str(),
                        _("Hide this group"));
			}
            ctx.req->printf("</td>\n");
            ctx.req->printf("</tr>\n");
        }

        ctx.req->printf("<tr class=\"sm_issues\">\n");

        std::list<std::string>::const_iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::ostringstream text;
            std::string column = *c;

            if (column == "id") text << i->id.c_str();
            else if (column == "ctime") text << epochToStringDelta(i->ctime);
            else if (column == "mtime") text << epochToStringDelta(i->mtime);
            else if (column == "p") text << i->project;
            else {
                std::map<std::string, std::list<std::string> >::const_iterator p;
                const std::map<std::string, std::list<std::string> > & properties = i->properties;

                p = properties.find(column);
                if (p != properties.end()) text << toString(p->second);
            }
            // add href if column is 'id' or 'summary'
            std::string href_lhs = "";
            std::string href_rhs = "";
            if ( (column == "id") || (column == "summary") ) {
                href_lhs = "<a href=\"";
                std::string href = MongooseServerContext::getInstance().getUrlRewritingRoot() + "/";
                href += Project::urlNameEncode(i->project) + "/issues/";
                href += urlEncode(i->id);
                href_lhs = href_lhs + href;
                href_lhs = href_lhs +  + "\">";

                href_rhs = "</a>";
            }

            ctx.req->printf("<td class=\"sm_issues\">%s%s%s</td>\n",
                            href_lhs.c_str(), htmlEscape(text.str()).c_str(), href_rhs.c_str());
        }
        ctx.req->printf("</tr>\n");
    }
    ctx.req->printf("</table>\n");
    ctx.req->printf("</div>\n");
}

void RHtmlIssue::printIssuesAccrossProjects(const ContextParameters &ctx,
                                       const std::vector<IssueCopy> &issues,
                                       const std::list<std::string> &colspec)
{
    printIssueList(ctx, issues, colspec, false);
}

std::string convertToRichTextWholeline(const std::string &in, const char *start, const char *htmlTag, const char *htmlClass)
{
    std::string result;
    size_t i = 0;
    size_t block = 0; // beginning of block, relevant only when insideBlock == true
    size_t len = in.size();
    bool insideBlock = false;
    bool startOfLine = true;
    size_t sizeStart = strlen(start);
    while (i<len) {
        char c = in[i];
        if (c == '\n') {
            startOfLine = true;

            if (insideBlock) {
                // end of line and end of block
                std::ostringstream currentBlock;

                currentBlock << "<" << htmlTag;
                currentBlock << " class=\"" << htmlClass << "\">";
                // the \n is included in the block. This is to solve the separation between \r and \n.
                currentBlock << in.substr(block, i-block+1);
                currentBlock << "</" << htmlTag << ">";
                result += currentBlock.str();
                insideBlock = false;
            } else result += c;

        } else if (startOfLine && (i+sizeStart-1 < len) && (0 == strncmp(start, in.c_str()+i, sizeStart)) ) {
            // beginning of new block
            insideBlock = true;
            block = i;

        } else {
            startOfLine = false;
            if (!insideBlock) result += c;
        }

        i++;
    }

    if (insideBlock) {
        // flush pending block
        std::ostringstream currentBlock;

        currentBlock << "<" << htmlTag;
        currentBlock << " class=\"" << htmlClass << "\">";
        currentBlock << in.substr(block, i-block+1);
        currentBlock << "</" << htmlTag << ">";
        result += currentBlock.str();
    }
    return result;

}

/** Convert text to HTML rich text according to 1 rich text pattern
  *
  *
  * @param dropBlockSeparators
  *    If true, the begin and end separators a removed from the final HTML
  *
  * {{{ ... }}}  surround verbatim blocks (no rich text is done inside)
  *
  */
std::string convertToRichTextInline(const std::string &in, const char *begin, const char *end,
                                    bool dropDelimiters, bool multiline, const char *htmlTag, const char *htmlClass)
{
    std::string result;
    size_t i = 0;
    size_t block = 0; // beginning of block, relevant only when insideBlock == true
    size_t len = in.size();
    size_t sizeEnd = strlen(end);
    size_t sizeBeginning = strlen(begin);
    bool insideBlock = false;
    const char *verbatimBegin = "{{{";
    const char *verbatimEnd = "}}}";
    const size_t VerbatimSize = 3;
    bool insideVerbatim = false;
    while (i<len) {
        char c = in[i];

        if (insideBlock) {
            // look if we are at the end of a block
            if ( (i <= len-sizeEnd) && (0 == strncmp(end, in.c_str()+i, sizeEnd)) ) {
                // end of block detected
                size_t sizeBlock;
                if (dropDelimiters) {
                    block += sizeBeginning;
                    sizeBlock = i-block;
                } else {
                    sizeBlock = i-block+sizeEnd;
                }
                i += sizeEnd-1; // -1 because i++ below...
                std::ostringstream currentBlock;

                if (0 == strcmp("a", htmlTag)) {
                    // for "a" tags, add "href=..."
                    std::string hyperlink = in.substr(block, sizeBlock);
                    currentBlock << "<" << htmlTag;
                    currentBlock << " href=\"" << hyperlink << "\">" << hyperlink;
                    currentBlock << "</" << htmlTag << ">";

                } else {
                    currentBlock << "<" << htmlTag;
                    currentBlock << " class=\"" << htmlClass << "\">";
                    currentBlock << in.substr(block, sizeBlock);
                    currentBlock << "</" << htmlTag << ">";
                }
                result += currentBlock.str();
                insideBlock = false;

            } else if (c == '\n' && !multiline) {
                // end of line cancels the pending block
                result += in.substr(block, i-block+1);
                insideBlock = false;
            }
        } else if ( !insideVerbatim && (i <= len-sizeBeginning) && (0 == strncmp(begin, in.c_str()+i, sizeBeginning)) ) {
            // beginning of new block
            insideBlock = true;
            block = i;
            i += sizeBeginning-1; // -1 because i++ below

        } else if ( (i <= len-VerbatimSize) && (0 == strncmp(verbatimBegin, in.c_str()+i, VerbatimSize)) ) {
            // beginning of verbatim block
            insideVerbatim = true;
            result += c;
        } else if ( insideVerbatim && (i <= len-VerbatimSize) && (0 == strncmp(verbatimEnd, in.c_str()+i, VerbatimSize)) ) {
            // end of verbatim block
            insideVerbatim = false;
            result += c;
        } else result += c;
        i++;
    }
    if (insideBlock) {
        // cancel pending block
        result += in.substr(block);
    }
    return result;
}

/** Convert text to HTML rich text
  *
  *    **a b c** => <span class="sm_bold">a b c</span>
  *    __a b c__ => <span class="sm_underline">a b c</span>
  *    ++a b c++ => <span class="sm_highlight">a b c</span>
  *    [[a b c]] => <a href="a b c" class="sm_hyperlink">a b c</a>
  *    > a b c =>  <span class="sm_quote">a b c</span> (> must be at the beginning of the line)
  *    etc.
  *
  * (optional) Characters before and after block must be [\t \n.;:]
  * A line break in the middle prevents the pattern from being recognized.
  */
std::string RHtmlIssue::convertToRichText(const std::string &raw)
{
    std::string result = convertToRichTextInline(raw, "**", "**", true, false, "strong", "");
    result = convertToRichTextInline(result, "__", "__", true, false, "span", "sm_underline");
    result = convertToRichTextInline(result, "&quot;", "&quot;", false, false, "span", "sm_double_quote");
    result = convertToRichTextInline(result, "++", "++", true, false, "em", "");
    result = convertToRichTextInline(result, "[[", "]]", true, false, "a", "sm_hyperlink");
    result = convertToRichTextWholeline(result, "&gt;", "span", "sm_quote");
    result = convertToRichTextInline(result, "{{{", "}}}", true, true, "span", "sm_block");
    return result;

}

/** return if a file is an image, based on the file extension
  *
  * Supported extensions: gif, jpg, jpeg, png, svg
  *
  */
bool isImage(const std::string &filename)
{
    std::string extension;
    size_t p = filename.find_last_of('.');
    if (p == std::string::npos) return false;
    extension = filename.substr(p);
    if (0 == strcasecmp(extension.c_str(), ".gif")) return true;
    if (0 == strcasecmp(extension.c_str(), ".png")) return true;
    if (0 == strcasecmp(extension.c_str(), ".jpg")) return true;
    if (0 == strcasecmp(extension.c_str(), ".jpeg")) return true;
    if (0 == strcasecmp(extension.c_str(), ".svg")) return true;
    return false;
}

/** print id and summary of an issue
  *
  */
void RHtmlIssue::printIssueSummary(const ContextParameters &ctx, const IssueCopy &issue)
{
    ctx.req->printf("<div class=\"sm_issue_header\">\n");
    ctx.req->printf("<a href=\"%s\" class=\"sm_issue_id\">%s</a>\n", htmlEscape(issue.id).c_str(),
            htmlEscape(issue.id).c_str());
    ctx.req->printf("<span class=\"sm_issue_summary\">%s</span>\n", htmlEscape(issue.getSummary()).c_str());
    ctx.req->printf("</div>\n");

}

/** Print associatied issues
  *
  * A <tr> must have been opened by the caller,
  * and must be closed by the caller.
  */
void printAssociations(const ContextParameters &ctx, const std::string &associationId, const IssueCopy &i, bool reverse)
{
    std::string label;
    if (reverse) {
        label = ctx.projectConfig.getReverseLabelOfProperty(associationId);
    } else {
        label = ctx.projectConfig.getLabelOfProperty(associationId);
    }

    ctx.req->printf("<td class=\"sm_issue_plabel\">%s: </td>", htmlEscape(label).c_str());
    ctx.req->printf("<td colspan=\"3\" class=\"sm_issue_asso\">");

    std::map<AssociationId, std::set<IssueSummary> >::const_iterator ait;
    const std::map<AssociationId, std::set<IssueSummary> > *atable;
    if (reverse) atable = &i.reverseAssociations;
    else atable = &i.associations;

    ait = atable->find(associationId);

    if (ait != atable->end()) {
        std::set<IssueSummary>::const_iterator is;
        const std::set<IssueSummary> &issuesSummaries = ait->second;
        FOREACH(is, issuesSummaries) {
            // separate by a line feed (LF)
            if (is != issuesSummaries.begin()) ctx.req->printf("<br>\n");
            ctx.req->printf("<a href=\"%s\"><span class=\"sm_issue_asso_id\">%s</span>"
                            " <span class=\"sm_issue_asso_summary\">%s</span></a>",
                            urlEncode(is->id).c_str(),
                            htmlEscape(is->id).c_str(),
                            htmlEscape(is->summary).c_str());
        }

    }
    ctx.req->printf("</td>");
}

void RHtmlIssue::printPropertiesTable(const ContextParameters &ctx, const IssueCopy &issue)
{
    // issue properties in a two-column table
    // -------------------------------------------------
    ctx.req->printf("<table class=\"sm_issue_properties\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;

    const ProjectConfig &pconfig = ctx.projectConfig;

    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, pconfig.properties) {

        std::string pname = pspec->name;
        enum PropertyType type = pspec->type;

        // take the value of this property
        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(pname);

        // if the property is an association, but with no value (ie: no associated issue), then do not display
        if (type == F_ASSOCIATION) {
            if (p == issue.properties.end()) continue; // this issue has no such property yet
            if (p->second.empty()) continue; // no associated issue
            if (p->second.front() == "") continue; // associated issue
        }

        std::string label = pconfig.getLabelOfProperty(pname);

        // if textarea type add specific class, for enabling line breaks in the CSS
        const char *pvalueStyle = "sm_issue_pvalue";
        const char *colspan = "";
        int workingColumnIncrement = 1;

        if (type == F_TEXTAREA) pvalueStyle = "sm_issue_pvalue_ta";
        else if (type == F_TEXTAREA2) pvalueStyle = "sm_issue_pvalue_ta2";

        const char *trStyle = "";
        if (type == F_ASSOCIATION) trStyle = "class=\"sm_issue_asso\"";

        if (workingColumn == 1) {
            ctx.req->printf("<tr %s>\n", trStyle);
        }

        // manage the start of the row
        if (type == F_TEXTAREA2 || type == F_ASSOCIATION) {
            // the property spans over 4 columns (1 col for the label and 3 for the value)
            if (workingColumn == 1) {
                // tr already opened. nothing more to do here
            } else {
                // add a placeholder in order to align the property with next row
                // close current row and start a new row
                ctx.req->printf("<td></td><td></td></tr><tr %s>\n", trStyle);
            }
            colspan = "colspan=\"3\"";
            workingColumn = 1;
            workingColumnIncrement = 2;

        }

        // print the value
        if (type == F_ASSOCIATION) {
            std::list<std::string> associatedIssues;
            if (p != issue.properties.end()) associatedIssues = p->second;

            printAssociations(ctx, pname, issue, false);

        } else {
            // print label and value of property (other than an association)

            // label
            ctx.req->printf("<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n",
                            urlEncode(pname).c_str(), htmlEscape(label).c_str());

            // value
            std::string value;
            if (p != issue.properties.end()) value = toString(p->second);

            // convert to rich text in case of textarea2
            if (type == F_TEXTAREA2) value = convertToRichText(htmlEscape(value));
            else value = htmlEscape(value);


            ctx.req->printf("<td %s class=\"%s sm_issue_pvalue_%s\">%s</td>\n",
                            colspan, pvalueStyle, urlEncode(pname).c_str(), value.c_str());
            workingColumn += workingColumnIncrement;
        }

        if (workingColumn > MAX_COLUMNS) {
            workingColumn = 1;
        }
    }

    // align wrt missing cells (ie: fulfill missnig columns and close current row)
    if (workingColumn != 1) ctx.req->printf("<td></td><td></td>\n");

    ctx.req->printf("</tr>\n");

    // reverse associated issues, if any
    if (!issue.reverseAssociations.empty()) {
        std::map<AssociationId, std::set<IssueSummary> >::const_iterator ra;
        FOREACH(ra, issue.reverseAssociations) {
            if (ra->second.empty()) continue;
            if (!ctx.projectConfig.isValidPropertyName(ra->first)) continue;

            ctx.req->printf("<tr class=\"sm_issue_asso\">");
            printAssociations(ctx, ra->first, issue, true);
            ctx.req->printf("</tr>");
        }
    }

    ctx.req->printf("</table>\n");
}

void RHtmlIssue::printTags(const ContextParameters &ctx, const IssueCopy &issue)
{
    const ProjectConfig &pconfig = ctx.projectConfig;

    // tags of the entries of the issue
    if (!pconfig.tags.empty()) {
        ctx.req->printf("<div class=\"sm_issue_tags\">\n");
        std::map<std::string, TagSpec>::const_iterator tspec;
        FOREACH(tspec, pconfig.tags) {
            if (tspec->second.display) {
                std::string style = "sm_issue_notag";
                int n = issue.getNumberOfTaggedIEntries(tspec->second.id);
                if (n > 0) {
                    // issue has at least one such tagged entry
                    style = "sm_issue_tagged " + urlEncode("sm_issue_tag_" + tspec->second.id);
                }
                ctx.req->printf("<span id=\"sm_issue_tag_%s\" class=\"%s\" data-n=\"%d\">%s</span>\n",
                                urlEncode(tspec->second.id).c_str(), style.c_str(), n, htmlEscape(tspec->second.label).c_str());

            }
        }

        ctx.req->printf("</div>\n");
    }
}
void RHtmlIssue::printEntry(const ContextParameters &ctx, const IssueCopy &issue, const Entry &ee, bool beingAmended)
{
    const ProjectConfig &pconfig = ctx.projectConfig;
    const char *styleBeingAmended = "";
    if (beingAmended) {
        // the page will display a form for editing this entry.
        // we want here a special display to help the user understand the link
        styleBeingAmended = "sm_entry_being_amended";
    }

    // look if class sm_no_contents is applicable
    // an entry has no contents if no message and no file
    const char* classNoContents = "";
    if (ee.getMessage().empty() || ee.isAmending()) {
        std::map<std::string, std::list<std::string> >::const_iterator files = ee.properties.find(K_FILE);
        if (files == ee.properties.end() || files->second.empty()) {
            classNoContents = "sm_entry_no_contents";
        }
    }

    // add tag-related styles, for the tags of the entry
    std::string classTagged = "sm_entry_notag";
    std::map<std::string, std::set<std::string> >::const_iterator tit = issue.tags.find(ee.id);
    if (tit != issue.tags.end()) {

        classTagged = "sm_entry_tagged";
        std::set<std::string>::iterator tag;
        FOREACH(tag, tit->second) {
            // check that this tag is declared in project config
            if (pconfig.tags.find(*tag) != pconfig.tags.end()) {
                classTagged += "sm_entry_tag_" + *tag + " ";
            }
        }
    }

    ctx.req->printf("<div class=\"sm_entry %s %s %s\" id=\"%s\">\n", classNoContents,
                    urlEncode(classTagged).c_str(), styleBeingAmended, urlEncode(ee.id).c_str());

    ctx.req->printf("<div class=\"sm_entry_header\">\n");
    ctx.req->printf("<span class=\"sm_entry_author\">%s</span>", htmlEscape(ee.author).c_str());
    ctx.req->printf(", <span class=\"sm_entry_ctime\">%s</span>\n", epochToString(ee.ctime).c_str());
    // conversion of date in javascript
    // document.write(new Date(%d)).toString());

    // edit button
    time_t delta = time(0) - ee.ctime;
    if ( (delta < DELETE_DELAY_S) && (ee.author == ctx.user.username) &&
         (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) &&
         !ee.isAmending()) {
        // entry was created less than 10 minutes ago, and by same user, and is latest in the issue
        ctx.req->printf("<a href=\"%s/%s/issues/%s?amend=%s\" class=\"sm_entry_edit\" "
                        "title=\"Edit this message (at most %d minutes after posting)\">",
                        MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                        ctx.getProjectUrlName().c_str(), enquoteJs(issue.id).c_str(),
                        enquoteJs(ee.id).c_str(), (DELETE_DELAY_S/60));
        ctx.req->printf("&#9998; %s", _("edit"));
        ctx.req->printf("</a>\n");
    }

    // link to raw entry
    ctx.req->printf("(<a href=\"%s/%s/" RESOURCE_FILES "/%s\" class=\"sm_entry_raw\">%s</a>",
                    MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                    ctx.getProjectUrlName().c_str(),
                    urlEncode(ee.id).c_str(), _("raw"));
    // link to possible amendments
    int i = 1;
    std::map<std::string, std::list<std::string> >::const_iterator as = issue.amendments.find(ee.id);
    if (as != issue.amendments.end()) {
        std::list<std::string>::const_iterator a;
        FOREACH(a, as->second) {
            ctx.req->printf(", <a href=\"/%s/" RESOURCE_FILES "/%s\" class=\"sm_entry_raw\">%s%d</a>",
                            ctx.getProjectUrlName().c_str(),
                            urlEncode(*a).c_str(), _("amend"), i);
            i++;
        }
    }
    ctx.req->printf(")");

    // display the tags of the entry
    if (!pconfig.tags.empty()) {
        std::map<std::string, TagSpec>::const_iterator tagIt;
        FOREACH(tagIt, pconfig.tags) {
            TagSpec tag = tagIt->second;
            LOG_DEBUG("tag: id=%s, label=%s", tag.id.c_str(), tag.label.c_str());
            std::string tagStyle = "sm_entry_notag";
            bool tagged = issue.hasTag(ee.id, tag.id);
            if (tagged) tagStyle = "sm_entry_tagged " + urlEncode("sm_entry_tag_" + tag.id);

            if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
                const char *tagTitle = _("Click to tag/untag");

                ctx.req->printf("<a href=\"#\" onclick=\"tagEntry('/%s/tags', '%s', '%s');return false;\""
                                " title=\"%s\" class=\"sm_entry_tag\">",
                                ctx.getProjectUrlName().c_str(), enquoteJs(ee.id).c_str(),
                                enquoteJs(tag.id).c_str(), tagTitle);

                // the tag itself
                ctx.req->printf("<span class=\"%s\" id=\"sm_tag_%s_%s\">",
                                tagStyle.c_str(), urlEncode(ee.id).c_str(), urlEncode(tag.id).c_str());
                ctx.req->printf("[%s]", htmlEscape(tag.label).c_str());
                ctx.req->printf("</span>\n");

                ctx.req->printf("</a>\n");

            } else {
                // read-only
                // if tag is not active, do not display
                if (tagged) {
                    ctx.req->printf("<span class=\"%s\">", tagStyle.c_str());
                    ctx.req->printf("[%s]", htmlEscape(tag.label).c_str());
                    ctx.req->printf("</span>\n");
                }

            }

        }

    }

    ctx.req->printf("</div>\n"); // end header

    std::string m = ee.getMessage();
    if (! m.empty() && !ee.isAmending()) {
        ctx.req->printf("<div class=\"sm_entry_message\">");
        ctx.req->printf("%s\n", convertToRichText(htmlEscape(m)).c_str());
        ctx.req->printf("</div>\n"); // end message
    } // else, do not display


    // uploaded / attached files
    std::map<std::string, std::list<std::string> >::const_iterator files = ee.properties.find(K_FILE);
    if (files != ee.properties.end() && files->second.size() > 0) {
        ctx.req->printf("<div class=\"sm_entry_files\">\n");
        std::list<std::string>::const_iterator itf;
        FOREACH(itf, files->second) {
            std::string f = *itf;
            std::string objectId = popToken(f, '/');
            std::string basename = f;

            std::string href = RESOURCE_FILES "/" + urlEncode(objectId) + "/" + urlEncode(basename);
            ctx.req->printf("<div class=\"sm_entry_file\">\n");
            ctx.req->printf("<a href=\"../%s\" class=\"sm_entry_file\">", href.c_str());
            if (isImage(f)) {
                // do not escape slashes
                ctx.req->printf("<img src=\"../%s\" class=\"sm_entry_file\"><br>", href.c_str());
            }
            ctx.req->printf("%s", htmlEscape(basename).c_str());
            // size of the file
            std::string path = Project::getObjectPath(ctx.projectPath, objectId);
            std::string size = getFileSize(path);
            ctx.req->printf("<span> (%s)</span>", size.c_str());
            ctx.req->printf("</a>");
            ctx.req->printf("</div>\n"); // end file
        }
        ctx.req->printf("</div>\n"); // end files
    }


    // -------------------------------------------------
    // print other modified properties
    printOtherProperties(ctx, ee, false, "sm_entry_other_properties");

    ctx.req->printf("</div>\n"); // end entry
}

void RHtmlIssue::printIssue(const ContextParameters &ctx, const IssueCopy &issue, const std::string &entryToBeAmended)
{
    ctx.req->printf("<div class=\"sm_issue\">");

    printPropertiesTable(ctx, issue);

    printTags(ctx, issue);

    // entries
    // -------------------------------------------------
    Entry *e = issue.first;
    while (e) {
        Entry ee = *e;

        bool beingAmended = (ee.id == entryToBeAmended);
        printEntry(ctx, issue, ee, beingAmended);

        e = e->getNext();
    } // end of entries

    ctx.req->printf("</div>\n");
}

void RHtmlIssue::printOtherProperties(const ContextParameters &ctx, const Entry &ee, bool printMessageHeading,
                                      const char *divStyle)
{
    std::ostringstream otherProperties;

    // process summary first as it is not part of orderedFields
    PropertiesIt p;
    bool first = true;
    FOREACH(p, ee.properties) {

        if (p->first == K_MESSAGE && !printMessageHeading) continue;

        std::string value;

        if (p->first == K_MESSAGE) {
            // print first characters of the message
            if (p->second.empty()) continue; // defensive programming, should not happen

            const uint32_t maxChar = 30;
            if (p->second.front().size() > maxChar) {
                value = p->second.front().substr(0, maxChar) + "...";
            } else {
                value = p->second.front();
            }
        } else {
            // normal property (other than the message)
            value = toString(p->second);
            // truncate if too long
            const uint32_t maxChar = 100;
            if (value.size() > maxChar) {
                value = value.substr(0, maxChar) + "...";
            }
        }

        if (!first) otherProperties << ", "; // separate properties by a comma
        first = false;

        otherProperties << "<span class=\"sm_entry_pname\">" << htmlEscape(ctx.projectConfig.getLabelOfProperty(p->first))
                        << ": </span>";
        otherProperties << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";

    }

    if (otherProperties.str().size() > 0) {
        ctx.req->printf("<div");
        if (divStyle) ctx.req->printf(" class=\"%s\"", divStyle);
        ctx.req->printf(">\n");
        ctx.req->printf("%s", otherProperties.str().c_str());
        ctx.req->printf("</div>\n");
    }
}



void RHtmlIssue::printFormMessage(const ContextParameters &ctx, const std::string &contents)
{
    ctx.req->printf("<tr>\n");
    ctx.req->printf("<td class=\"sm_issue_plabel sm_issue_plabel_message\" >%s: </td>\n",
                    htmlEscape(_("Message")).c_str());
    ctx.req->printf("<td colspan=\"3\">\n");
    ctx.req->printf("<textarea class=\"sm_issue_pinput sm_issue_pinput_message\" placeholder=\"%s\" name=\"%s\" wrap=\"hard\" cols=\"80\">\n",
                    _("Enter a message"), K_MESSAGE);
    ctx.req->printf("%s", htmlEscape(contents).c_str());
    ctx.req->printf("</textarea>\n");
    ctx.req->printf("</td></tr>\n");

    // check box "enable long lines"
    ctx.req->printf("<tr><td></td>\n");
    ctx.req->printf("<td class=\"sm_issue_longlines\" colspan=\"3\">\n");
    ctx.req->printf("<label><input type=\"checkbox\" onclick=\"changeWrapping();\">\n");
    ctx.req->printf("%s\n", _("Enable long lines"));
    ctx.req->printf("</label></td></tr>\n");
}

void RHtmlIssue::printEditMessage(const ContextParameters &ctx, const IssueCopy *issue,
                             const Entry &eToBeAmended)
{
    if (!issue) {
        LOG_ERROR("printEditMessage: Invalid null issue");
        return;
    }
    if (ctx.userRole != ROLE_ADMIN && ctx.userRole != ROLE_RW) {
        return;
    }
    ctx.req->printf("<div class=\"sm_amend\">%s: %s</div>", _("Amend Messsage"), urlEncode(eToBeAmended.id).c_str());
    ctx.req->printf("<form enctype=\"multipart/form-data\" method=\"post\" class=\"sm_issue_form\">");
    ctx.req->printf("<input type=\"hidden\" value=\"%s\" name=\"%s\">", urlEncode(eToBeAmended.id).c_str(), K_AMEND);
    ctx.req->printf("<table class=\"sm_issue_properties\">");

    printFormMessage(ctx, eToBeAmended.getMessage());

    ctx.req->printf("<tr><td></td>\n");
    ctx.req->printf("<td colspan=\"3\">\n");
    ctx.req->printf("<button onclick=\"previewMessage(); return false;\">%s</button>\n", htmlEscape(_("Preview")).c_str());
    ctx.req->printf("<input type=\"submit\" value=\"%s\">\n", htmlEscape(_("Post")).c_str());
    ctx.req->printf("</td></tr>\n");

    ctx.req->printf("</table>\n");
    ctx.req->printf("</form>");

}

/** print form for adding a message / modifying the issue
  *
  * @param autofocus
  *    Give focus to the summary field. Used mainly when creating a new issue.
  *    (not used for existing issues, as it would force the browser to scroll
  *    down to the summary field, and do not let the user read the top of
  *    the page first)
  */
void RHtmlIssue::printIssueForm(const ContextParameters &ctx, const IssueCopy *issue, bool autofocus)
{
    if (!issue) {
        LOG_ERROR("printIssueForm: Invalid null issue");
        return;
    }
    if (ctx.userRole != ROLE_ADMIN && ctx.userRole != ROLE_RW) {
        return;
    }

    const ProjectConfig &pconfig = ctx.projectConfig;

    ctx.req->printf("<form id=\"sm_issue_form\" enctype=\"multipart/form-data\" method=\"post\" class=\"sm_issue_form\">");

    // The form is made over a table with 4 columns.
    // each row is made of 1 label, 1 input, 1 label, 1 input (4 columns)
    // except for the summary.
    // summary
    ctx.req->printf("<table class=\"sm_issue_properties\">");

    // properties of the issue
    // summary
    ctx.req->printf("<tr>\n");
    ctx.req->printf("<td class=\"sm_issue_plabel sm_issue_plabel_summary\">%s: </td>\n",
                    htmlEscape(pconfig.getLabelOfProperty("summary")).c_str());
    ctx.req->printf("<td class=\"sm_issue_pinput\" colspan=\"3\">");

    ctx.req->printf("<input class=\"sm_issue_pinput_summary\" required=\"required\" type=\"text\" name=\"summary\" value=\"%s\"",
                    htmlEscape(issue->getSummary()).c_str());
    if (autofocus) ctx.req->printf(" autofocus");
    ctx.req->printf(">");
    ctx.req->printf("</td>\n");
    ctx.req->printf("</tr>\n");

    // other properties

    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;
    std::list<PropertySpec>::const_iterator pspec;

    FOREACH(pspec, pconfig.properties) {
        std::string pname = pspec->name;
        std::string label = pconfig.getLabelOfProperty(pname);

        std::map<std::string, std::list<std::string> >::const_iterator p = issue->properties.find(pname);
        std::list<std::string> propertyValues;
        if (p!=issue->properties.end()) propertyValues = p->second;

        std::ostringstream input;
        std::string value;
        const char *colspan = "";
        int workingColumnIncrement = 1;

        if (pspec->type == F_TEXT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<input class=\"sm_pinput_" << pname << "\" type=\"text\" name=\""
                  << pname << "\" value=\"" << htmlEscape(value) << "\">\n";

        } else if (pspec->type == F_SELECT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            std::list<std::string>::const_iterator so;
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\">";

            std::list<std::string> opts = pspec->selectOptions;
            // if the present value is not empty and not in the list of official values
            // then it means that probably this value has been removed lately from
            // the official values
            // but we want to allow the user keep the old value
            // then add it in the list
            if (!value.empty()) {
                if (!inList(opts, value)) opts.push_back(value); // add it in the list
            }

            for (so = opts.begin(); so != opts.end(); so++) {
                input << "<option" ;
                if (value == *so) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>\n";
            }

            input << "</select>\n";

        } else if (pspec->type == F_MULTISELECT) {
            std::list<std::string>::const_iterator so;
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\"";
            if (pspec->type == F_MULTISELECT) input << " multiple=\"multiple\"";
            input << ">";

            std::list<std::string> opts = pspec->selectOptions;
            // same as above : keep the old value even if no longer in official values
            std::list<std::string>::const_iterator v;
            FOREACH(v, propertyValues) {
                if (!v->empty() && !inList(opts, *v)) opts.push_back(*v);
            }

            for (so = opts.begin(); so != opts.end(); so++) {
                input << "<option" ;
                if (inList(propertyValues, *so)) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>\n";
            }

            input << "</select>\n";

            // add a hidden field to tell the server that this property was present, even if
            // no value selected
            input << "\n";
            input << "<input type=\"hidden\" name=\"" << pname << "\" value=\"\">";


        } else if (pspec->type == F_SELECT_USER) {
            if (propertyValues.size()>0) value = propertyValues.front();
            else {
                // by default, if no selection is made, select the current user
                value = ctx.user.username;
            }
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\">";

            std::set<std::string> users = UserBase::getUsersOfProject(ctx.projectName);
            // same a as above : keep old value even if not in official list
            if (!value.empty()) users.insert(value);

            std::set<std::string>::iterator u;
            for (u = users.begin(); u != users.end(); u++) {
                input << "<option" ;
                if (value == *u) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*u) << "</option>\n";
            }

            input << "</select>\n";

        } else if (pspec->type == F_TEXTAREA) {
            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<textarea class=\"sm_ta sm_issue_pinput_" << pname << "\" name=\""
                  << pname << "\">" << htmlEscape(value) << "</textarea>\n";

        } else if (pspec->type == F_TEXTAREA2) {
            // the property spans over 4 columns (1 col for the label and 3 for the value)
            if (workingColumn == 1) {
                // nothing to do
            } else {
                // add a placeholder in order to align the property with next row
                // close current row and start a new row
                ctx.req->printf("<td></td><td></td></tr><tr>\n");
            }
            colspan = "colspan=\"3\"";
            workingColumn = 1;
            workingColumnIncrement = 2;

            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<textarea class=\"sm_ta2 sm_issue_pinput_" << urlEncode(pname) << "\" name=\""
                  << urlEncode(pname) << "\">" << htmlEscape(value) << "</textarea>\n";

        } else if (pspec->type == F_ASSOCIATION) {
            if (propertyValues.size()>0) value = join(propertyValues, ", ");
            input << "<input class=\"sm_pinput_" << pname << "\" type=\"text\" name=\""
                  << pname << "\" value=\"" << htmlEscape(value) << "\">\n";

        } else {
            LOG_ERROR("invalid fieldSpec->type=%d", pspec->type);
            continue;
        }

        if (workingColumn == 1) {
            ctx.req->printf("<tr>\n");
        }

        // label
        ctx.req->printf("<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n",
                        urlEncode(pname).c_str(), htmlEscape(label).c_str());

        // input
        ctx.req->printf("<td %s class=\"sm_issue_pinput\">%s</td>\n", colspan, input.str().c_str());

        workingColumn += workingColumnIncrement;
        if (workingColumn > MAX_COLUMNS) {
            ctx.req->printf("</tr>\n");
            workingColumn = 1;
        }
    }

    if (workingColumn != 1) {
        // add 2 empty cells
        ctx.req->printf("<td></td><td></td></tr>\n");
    }

    // end of other properties

    // message
    printFormMessage(ctx, "");

    // add file upload input
    ctx.req->printf("<tr>\n");
    ctx.req->printf("<td class=\"sm_issue_plabel sm_issue_plabel_file\" >%s: </td>\n", htmlEscape(_("File Upload")).c_str());
    ctx.req->printf("<td colspan=\"3\">\n");
    ctx.req->printf("<input type=\"file\" name=\"%s\" class=\"sm_issue_input_file\" onchange=\"updateFileInput('sm_issue_input_file');\">\n", K_FILE);
    ctx.req->printf("</td></tr>\n");

    ctx.req->printf("<tr><td></td>\n");
    ctx.req->printf("<td colspan=\"3\">\n");
    ctx.req->printf("<button onclick=\"previewMessage(); return false;\">%s</button>\n", htmlEscape(_("Preview")).c_str());
    ctx.req->printf("<input type=\"submit\" value=\"%s\">\n", htmlEscape(_("Post")).c_str());
    ctx.req->printf("</td></tr>\n");

    ctx.req->printf("</table>\n");
    ctx.req->printf("</form>");

}
