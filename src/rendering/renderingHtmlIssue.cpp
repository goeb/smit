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
#include "renderingHtmlUtil.h"
#include "repository/db.h"
#include "utils/parseConfig.h"
#include "utils/logging.h"
#include "utils/stringTools.h"
#include "utils/dateTools.h"
#include "utils/filesystem.h"
#include "utils/jTools.h"
#include "global.h"
#include "restApi.h"

#define FLAG_ASSOCIATION_NOMINAL 0
#define FLAG_ASSOCIATION_REVERSE (1 << 0)
#define FLAG_ASSOCIATION_OFFLINE (1 << 1)

/** print id and summary of an issue
  *
  */
static std::string renderIssueSummary(const ContextParameters &ctx, const IssueCopy &issue)
{
    StringStream ss;
    ss.printf("<div class=\"sm_issue_header\">\n");
    ss.printf("<a href=\"%s\" class=\"sm_issue_id\">%s</a>\n", htmlEscape(issue.id).c_str(),
              htmlEscape(issue.id).c_str());
    ss.printf("<span class=\"sm_issue_summary\">%s</span>\n", htmlEscape(issue.getSummary()).c_str());
    ss.printf("</div>\n");
    return ss.str();
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
        std::string summary = renderIssueSummary(ctx, issue);
        ctx.req->printf("%s", summary.c_str());
        printIssue(ctx, issue, 0);
    }
    ctx.req->printf("</div>\n");

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
                std::string href = ctx.req->getUrlRewritingRoot() + "/";
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

/** Print associated issues
  *
  * A <tr> must have been opened by the caller,
  * and must be closed by the caller.
  */
static std::string renderAssociations(const ContextParameters &ctx, const std::string &associationId,
                                      const IssueCopy &i, int flags)
{
    std::string label;
    StringStream ss;
    bool reverse = flags & FLAG_ASSOCIATION_REVERSE;
    bool offline = flags & FLAG_ASSOCIATION_OFFLINE;

    if (reverse) {
        label = ctx.projectConfig.getReverseLabelOfProperty(associationId);
    } else {
        label = ctx.projectConfig.getLabelOfProperty(associationId);
    }

    ss.printf("<td class=\"sm_issue_plabel\">%s: </td>", htmlEscape(label).c_str());
    ss.printf("<td colspan=\"3\" class=\"sm_issue_asso\">");

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
            if (is != issuesSummaries.begin()) ss.printf("<br>\n");

            if (!offline) ss.printf("<a href=\"%s\">", urlEncode(is->id).c_str());

            ss.printf("<span class=\"sm_issue_asso_id\">%s</span>"
                      " <span class=\"sm_issue_asso_summary\">%s</span>",
                      htmlEscape(is->id).c_str(),
                      htmlEscape(is->summary).c_str());

            if (!offline) ss.printf("</a>");
        }
    }
    ss.printf("</td>");
    return ss.str();
}

std::string RHtmlIssue::renderPropertiesTable(const ContextParameters &ctx, const IssueCopy &issue, bool offline)
{
    StringStream ss;
    // issue properties in a two-column table
    // -------------------------------------------------
    ss.printf("<table class=\"sm_issue_properties\">");
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
            ss.printf("<tr %s>\n", trStyle);
        }

        // manage the start of the row
        if (type == F_TEXTAREA2 || type == F_ASSOCIATION) {
            // the property spans over 4 columns (1 col for the label and 3 for the value)
            if (workingColumn == 1) {
                // tr already opened. nothing more to do here
            } else {
                // add a placeholder in order to align the property with next row
                // close current row and start a new row
                ss.printf("<td></td><td></td></tr><tr %s>\n", trStyle);
            }
            colspan = "colspan=\"3\"";
            workingColumn = 1;
            workingColumnIncrement = 2;

        }

        // print the value
        if (type == F_ASSOCIATION) {
            std::list<std::string> associatedIssues;
            if (p != issue.properties.end()) associatedIssues = p->second;

            int flags = FLAG_ASSOCIATION_NOMINAL;
            if (offline) flags |= FLAG_ASSOCIATION_OFFLINE;
            ss << renderAssociations(ctx, pname, issue, flags);

        } else {
            // print label and value of property (other than an association)

            // label
            ss.printf("<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n",
                      urlEncode(pname).c_str(), htmlEscape(label).c_str());

            // value
            std::string value;
            if (p != issue.properties.end()) value = toString(p->second);

            // convert to rich text in case of textarea2
            if (type == F_TEXTAREA2) value = convertToRichText(htmlEscape(value));
            else value = htmlEscape(value);


            ss.printf("<td %s class=\"%s sm_issue_pvalue_%s\">%s</td>\n",
                      colspan, pvalueStyle, urlEncode(pname).c_str(), value.c_str());
            workingColumn += workingColumnIncrement;
        }

        if (workingColumn > MAX_COLUMNS) {
            workingColumn = 1;
        }
    }

    // align wrt missing cells (ie: fulfill missnig columns and close current row)
    if (workingColumn != 1) ss.printf("<td></td><td></td>\n");

    ss.printf("</tr>\n");

    // reverse associated issues, if any
    if (!issue.reverseAssociations.empty()) {
        std::map<AssociationId, std::set<IssueSummary> >::const_iterator ra;
        FOREACH(ra, issue.reverseAssociations) {
            if (ra->second.empty()) continue;
            if (!ctx.projectConfig.isValidPropertyName(ra->first)) continue;

            ss.printf("<tr class=\"sm_issue_asso\">");

            int flags = FLAG_ASSOCIATION_REVERSE;
            if (offline) flags |= FLAG_ASSOCIATION_OFFLINE;
            renderAssociations(ctx, ra->first, issue, flags);
            ss.printf("</tr>");
        }
    }

    ss.printf("</table>\n");
    return ss.str();
}

std::string RHtmlIssue::renderTags(const ContextParameters &ctx, const IssueCopy &issue)
{
    const ProjectConfig &pconfig = ctx.projectConfig;
    StringStream ss;

    // tags of the entries of the issue
    if (!pconfig.tags.empty()) {
        ss.printf("<div class=\"sm_issue_tags\">\n");
        std::map<std::string, TagSpec>::const_iterator tspec;
        FOREACH(tspec, pconfig.tags) {
            if (tspec->second.display) {
                std::string style = "sm_issue_notag";
                int n = issue.getNumberOfTaggedIEntries(tspec->second.id);
                if (n > 0) {
                    // issue has at least one such tagged entry
                    style = "sm_issue_tagged " + urlEncode("sm_issue_tag_" + tspec->second.id);
                }
                ss.printf("<span id=\"sm_issue_tag_%s\" class=\"%s\" data-n=\"%d\">%s</span>\n",
                                urlEncode(tspec->second.id).c_str(), style.c_str(), n, htmlEscape(tspec->second.label).c_str());

            }
        }

        ss.printf("</div>\n");
    }
    return ss.str();
}

/** Get a list of CSS styles, separated by spaces
 *
 * The extra styles depend on:
 * - is the entry being amended?
 * - has the entry specific tags?
 *
 */
std::string RHtmlIssue::getEntryExtraStyles(const ProjectConfig &pconfig, const IssueCopy &issue,
                                            uint32_t entryIndex, bool beingAmended)
{
    std::string extraStyles = "";
    if (beingAmended) {
        // the page will display a form for editing this entry.
        // we want here a special display to help the user understand the link
        extraStyles += " sm_entry_being_amended";
    }

    const Entry &ee = issue.entries[entryIndex];

    // look if class sm_no_contents is applicable
    // an entry has no contents if no message and no file
    if (ee.getMessage().empty() || ee.isAmending()) {
        if (ee.files.empty()) {
            extraStyles += " sm_entry_no_contents";
        }
    }

    // add tag-related styles, for the tags of the entry
    std::string classTagged = "sm_entry_notag";
    std::map<uint32_t, std::set<std::string> >::const_iterator tit = issue.tags.find(entryIndex);
    if (tit != issue.tags.end()) {

        classTagged = "sm_entry_tagged";
        std::set<std::string>::iterator tag;
        FOREACH(tag, tit->second) {
            // check that this tag is declared in project config
            if (pconfig.tags.find(*tag) != pconfig.tags.end()) {
                classTagged += " sm_entry_tag_" + *tag;
            }
        }
    }
    extraStyles += " " + htmlAttributeEscape(classTagged);

    return extraStyles;
}

std::string RHtmlIssue::renderEntry(const ContextParameters &ctx, const IssueCopy &issue, uint32_t entryIndex, int flags)
{
    const ProjectConfig &pconfig = ctx.projectConfig;
    StringStream ss;
    const Entry &ee = issue.entries[entryIndex];
    std::ostringstream entryIdStream; // for HTML styles and ids
    entryIdStream << issue.id << "_" << entryIndex; // concatenate the issue id and the index of the entry
    const std::string entryId = entryIdStream.str();


    bool beingAmended = flags & FLAG_ENTRY_BEING_AMENDED;
    bool offline = flags & FLAG_ENTRY_OFFLINE;

    std::string extraStyles = getEntryExtraStyles(pconfig, issue, entryIndex, beingAmended);
    ss.printf("<div class=\"sm_entry %s\" id=\"%s\">\n", extraStyles.c_str(),
                    urlEncode(entryId).c_str());

    ss.printf("<div class=\"sm_entry_header\">\n");
    ss.printf("<span class=\"sm_entry_author\">%s</span>", htmlEscape(ee.author).c_str());
    ss.printf(", <span class=\"sm_entry_ctime\">%s</span>\n", epochToString(ee.ctime).c_str());

    // edit button
    time_t delta = time(0) - ee.ctime;
    if ( (delta < Database::getEditDelay()) && (ee.author == ctx.user.username) &&
         (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) &&
         !ee.isAmending() &&
         !offline) {
        // entry was created less than 10 minutes ago, and by same user, and is latest in the issue
        ss.printf("<a href=\"?amend=%u\" class=\"sm_entry_edit\" "
                        "title=\"Edit this message (at most %d minutes after posting)\">",
                        entryIndex, (Database::getEditDelay()/60));
        ss.printf("&#9998; %s", _("edit"));
        ss.printf("</a>\n");
    }

    if (!offline) {
        // link to raw entry
        ss.printf("(<a href=\"../" RESOURCE_FILES "/%s\" class=\"sm_entry_raw\">%s</a>",
                  urlEncode(ee.id).c_str(), _("raw"));
        // link to possible amendments
        int i = 1;
        std::map<uint32_t, std::list<uint32_t> >::const_iterator as = issue.amendments.find(entryIndex);
        if (as != issue.amendments.end()) {
            std::list<uint32_t>::const_iterator a;
            FOREACH(a, as->second) {
                const Entry &entry = issue.entries[*a]; // TODO nee to check out of range ?
                // href link to the entry that amends the current
                ss.printf(", <a href=\"../" RESOURCE_FILES "/%s\" class=\"sm_entry_raw\">%s%d</a>",
                          urlEncode(entry.id).c_str(), _("amend"), i);
                i++;
            }
        }
        ss.printf(")");
    }

    // display the tags of the entry
    if (!pconfig.tags.empty()) {
        std::map<std::string, TagSpec>::const_iterator tagIt;
        FOREACH(tagIt, pconfig.tags) {
            TagSpec tag = tagIt->second;
            LOG_DEBUG("tag: id=%s, label=%s", tag.id.c_str(), tag.label.c_str());
            std::string tagStyle = "sm_entry_notag";
            bool tagged = issue.hasTag(entryIndex, tag.id);
            if (tagged) tagStyle = "sm_entry_tagged " + urlEncode("sm_entry_tag_" + tag.id);

            if (!offline && ( ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) ) {
                const char *tagTitle = _("Click to tag/untag");

                ss.printf("<a href=\"#\" onclick=\"tagEntry('/%s/tags', '%s', '%s');return false;\""
                          " title=\"%s\" class=\"sm_entry_tag\">",
                          ctx.getProjectUrlName().c_str(),
                          enquoteJs(entryId).c_str(),
                          enquoteJs(tag.id).c_str(),
                          tagTitle);

                // the tag itself
                ss.printf("<span class=\"%s\" id=\"sm_tag_%s_%s\">",
                                tagStyle.c_str(), urlEncode(entryId).c_str(), urlEncode(tag.id).c_str());
                ss.printf("[%s]", htmlEscape(tag.label).c_str());
                ss.printf("</span>\n");

                ss.printf("</a>\n");

            } else {
                // read-only
                // if tag is not active, do not display
                if (tagged) {
                    ss.printf("<span class=\"%s\">", tagStyle.c_str());
                    ss.printf("[%s]", htmlEscape(tag.label).c_str());
                    ss.printf("</span>\n");
                }
            }
        }
    }

    ss.printf("</div>\n"); // end header

    std::string m = ee.getMessage();
    if (! m.empty() && !ee.isAmending()) {
        ss.printf("<div class=\"sm_entry_message\">");
        ss.printf("%s\n", convertToRichText(htmlEscape(m)).c_str());
        ss.printf("</div>\n"); // end message
    } // else, do not display


    // uploaded / attached files
    if (!ee.files.empty()) {
        ss.printf("<div class=\"sm_entry_files\">\n");
        std::list<AttachedFileRef>::const_iterator afr;
        FOREACH(afr, ee.files) {

            std::string href = RESOURCE_FILES "/" + urlEncode(afr->id) + "/" + urlEncode(afr->filename);
            ss.printf("<div class=\"sm_entry_file\">\n");
            ss.printf("<a href=\"../%s\" class=\"sm_entry_file\">", href.c_str());
            if (isImage(afr->filename)) {
                // do not escape slashes
                ss.printf("<img src=\"../%s\" class=\"sm_entry_file\"><br>", href.c_str());
            }
            ss.printf("%s", htmlEscape(afr->filename).c_str());

            // size of the file
            ss.printf("<span> (%s)</span>", filesize2string(afr->size).c_str());

            ss.printf("</a>");
            ss.printf("</div>\n"); // end file
        }
        ss.printf("</div>\n"); // end files
    }


    // -------------------------------------------------
    // print other modified properties
    ss << printOtherProperties(ctx, ee, false, "sm_entry_other_properties");

    ss.printf("</div>\n"); // end entry
    return ss.str();
}

void RHtmlIssue::printIssue(const ContextParameters &ctx, const IssueCopy &issue, const uint32_t *entryIdxToBeAmended)
{
    ctx.req->printf("<div class=\"sm_issue\">");

    std::string pt = renderPropertiesTable(ctx, issue, false);
    ctx.req->printf("%s", pt.c_str());

    std::string tags = renderTags(ctx, issue);
    ctx.req->printf("%s", tags.c_str());

    // entries
    // -------------------------------------------------
    uint32_t entryIndex;
    size_t nEntries = issue.entries.size();

    for(entryIndex=0; entryIndex<nEntries; entryIndex++) {
        const Entry &ee = issue.entries[entryIndex];

        int flag = FLAG_ENTRY_NOMINAL;
        if (entryIdxToBeAmended && entryIndex == *entryIdxToBeAmended) flag = FLAG_ENTRY_BEING_AMENDED;
        std::string entry = renderEntry(ctx, issue, entryIndex, flag);
        ctx.req->printf("%s", entry.c_str());

    } // end of entries

    ctx.req->printf("</div>\n");
}

std::string RHtmlIssue::printOtherProperties(const ContextParameters &ctx, const Entry &ee,
                                             bool printMessageHeading, const char *divStyle)
{
    std::ostringstream otherProperties;
    StringStream ss;

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
        ss.printf("<div");
        if (divStyle) ss.printf(" class=\"%s\"", divStyle);
        ss.printf(">\n");
        ss.printf("%s", otherProperties.str().c_str());
        ss.printf("</div>\n");
    }
    return ss.str();
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
                             uint32_t eIndexToBeAmended)
{
    if (!issue) {
        LOG_ERROR("printEditMessage: Invalid null issue");
        return;
    }
    if (ctx.userRole != ROLE_ADMIN && ctx.userRole != ROLE_RW) {
        return;
    }
    ctx.req->printf("<div class=\"sm_amend\">%s: %u</div>", _("Amend Messsage"), eIndexToBeAmended);
    ctx.req->printf("<form id=\"sm_issue_form\" enctype=\"multipart/form-data\" method=\"post\" class=\"sm_issue_form\">");
    ctx.req->printf("<input type=\"hidden\" value=\"%u\" name=\"%s\">", eIndexToBeAmended, K_AMEND);
    ctx.req->printf("<table class=\"sm_issue_properties\">");

    printFormMessage(ctx, issue->entries[eIndexToBeAmended].getMessage());

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

    // Table of subcolumns:
    // - 1 column for the labels
    // - 3 columns for values of type F_TEXTAREA2
    // - 1 column for other values
    const uint8_t MAX_COLUMNS = 4; // only 4 supported
    int workingColumn = 1; // 1 or 3
    std::list<PropertySpec>::const_iterator pspec;

    FOREACH(pspec, pconfig.properties) {
        std::string pname = pspec->name;
        std::string label = pconfig.getLabelOfProperty(pname);

        std::map<std::string, std::list<std::string> >::const_iterator p = issue->properties.find(pname);
        std::list<std::string> propertyValues;
        if (p!=issue->properties.end()) propertyValues = p->second;

        std::ostringstream input;
        std::string value;
        int colspan = 1;

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
            colspan = 3;

            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<textarea class=\"sm_ta2 sm_issue_pinput_" << urlEncode(pname) << "\" name=\""
                  << urlEncode(pname) << "\">";

            // textarea contents
            if (!value.empty()) {
                // print the regular value
                input << htmlEscape(value);
            } else if (! pspec->ta2Template.empty()) {
                // print the template
                input << htmlEscape(pspec->ta2Template);
            }
            input << "</textarea>\n";

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

        } else if (workingColumn + colspan > MAX_COLUMNS) {
            // add cells placeholders in order to align the property with next row
            // close current row and start a new row
            ctx.req->printf("<td></td><td></td></tr><tr>\n");
        }

        // label
        ctx.req->printf("<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n",
                        urlEncode(pname).c_str(), htmlEscape(label).c_str());

        // input
        ctx.req->printf("<td colspan=\"%d\" class=\"sm_issue_pinput\">%s</td>\n", colspan, input.str().c_str());

        workingColumn += colspan + 1;
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
