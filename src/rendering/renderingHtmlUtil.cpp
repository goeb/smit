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

#include <sstream>

#include "renderingHtmlUtil.h"
#include "utils/logging.h"
#include "global.h"

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

static void printFilterLogicalExpr(const RequestContext *req,
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

