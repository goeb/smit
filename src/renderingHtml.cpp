/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include <stdlib.h>
#include <sstream>
#include <string.h>
#include <set>
#include <stdarg.h>

#include "renderingHtml.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "stringTools.h"
#include "dateTools.h"
#include "session.h"
#include "global.h"


ContextParameters::ContextParameters(struct mg_connection *cnx, User u, Project &p)
{
    project = &p;
    username = u.username;
    userRole = u.getRole(p.getName());
    conn = cnx;
}

ContextParameters::ContextParameters(struct mg_connection *cnx, User u)
{
    project = 0;
    username = u.username;
    conn = cnx;
}

const Project &ContextParameters::getProject() const
{
    if (!project) {
        LOG_ERROR("Invalid null project. Expect crash...");
    }
    return *project;
}


#define K_SM_DIV_NAVIGATION_GLOBAL "SM_DIV_NAVIGATION_GLOBAL"
#define K_SM_DIV_NAVIGATION_ISSUES "SM_DIV_NAVIGATION_ISSUES"
#define K_SM_HTML_PROJECT_NAME "SM_HTML_PROJECT_NAME"
#define K_SM_URL_PROJECT_NAME "SM_URL_PROJECT_NAME"
#define K_SM_RAW_ISSUE_ID "SM_RAW_ISSUE_ID"
#define K_SM_SCRIPT_PROJECT_CONFIG_UPDATE "SM_SCRIPT_PROJECT_CONFIG_UPDATE"
#define K_SM_DIV_PREDEFINED_VIEWS "SM_DIV_PREDEFINED_VIEWS"
#define K_SM_DIV_PROJECTS "SM_DIV_PROJECTS"
#define K_SM_DIV_ISSUES "SM_DIV_ISSUES"
#define K_SM_DIV_ISSUE "SM_DIV_ISSUE"
#define K_SM_DIV_ISSUE_SUMMARY "SM_DIV_ISSUE_SUMMARY"
#define K_SM_DIV_ISSUE_FORM "SM_DIV_ISSUE_FORM"


std::string enquoteJs(const std::string &in)
{
    std::string out = replaceAll(in, '\'', "\\'");
    return out;
}

std::string htmlEscape(const std::string &value)
{
    std::string result = replaceAll(value, '&', "&#38;");
    result = replaceAll(result, '"', "&quot;");
    result = replaceAll(result, '<', "&lt;");
    result = replaceAll(result, '>', "&gt;");
    result = replaceAll(result, '\'', "&#39;");
    return result;
}


/** Load a page for a specific project
  *
  * By default pages (typically HTML pages) are loaded from $REPO/public/ directory.
  * But the administrator may override this by pages located in $REPO/$PROJECT/html/ directory.
  *
  * The caller is responsible for calling 'free' on the returned pointer (if not null).
  */
int loadProjectPage(struct mg_connection *conn, const std::string &projectPath, const std::string &page, const char **data)
{
    // first look for the page in $REPO/$PROJECT/html/
    std::string path = projectPath + "/html/" + page;
    int n = loadFile(path.c_str(), data);
    if (n > 0) return n;

    // secondly, look at $REPO/public/
    path = Database::Db.getRootDir() + "/public/" + page;
    n = loadFile(path.c_str(), data);

    if (n > 0) return n;

    // no page found. This is an error
    LOG_ERROR("Page not found (or empty): %s", page.c_str());
    mg_printf(conn, "Missing page (or empty): %s", htmlEscape(page).c_str());
    return n;
}

class VariableNavigator {
public:
    std::vector<struct Issue*> *issueList;
    std::vector<struct Issue*> *issueListFullContents;
    std::list<std::string> *colspec;
    const ContextParameters &ctx;
    const std::list<std::pair<std::string, std::string> > *projectList;
    const Issue *currentIssue;
    const std::list<Entry*> *entries;
    const std::map<std::string, std::map<std::string, Role> > *userRolesByProject;

    VariableNavigator(const std::string basename, const ContextParameters &context) : ctx(context) {
        buffer = 0;
        issueList = 0;
        issueListFullContents = 0;
        colspec = 0;
        projectList = 0;
        currentIssue = 0;
        entries = 0;
        userRolesByProject = 0;

        int n;
        if (ctx.project) n = loadProjectPage(ctx.conn, ctx.project->getPath(), basename, &buffer);
        else {
            std::string path = Database::Db.getRootDir() + "/public/" + basename;
            n = loadFile(path.c_str(), &buffer);
        }

        if (n > 0) {
            size = n;
            dumpStart = buffer;
            dumpEnd = buffer;
            searchFromHere = buffer;
        } else buffer = 0;
    }

    ~VariableNavigator() {
        if (buffer) free((void*)buffer);
    }

    std::string getNextVariable() {

        if (searchFromHere >= (buffer+size)) return "";

        const char *p0 = strstr(searchFromHere, "SM_");
        if (!p0) {
            // no SM variable found
            dumpEnd = buffer+size;
            return "";
        }

        const char *p = p0;
        while ( (p < buffer+size) && (isalnum(*p) || ('_' == *p)) ) p++;

        std::string varname(p0, p-p0);
        searchFromHere = p;
        dumpEnd = p0;

        return varname;
    }

    void dumpPrevious(struct mg_connection *conn) {
        if (dumpEnd == dumpStart) {
            LOG_ERROR("dumpPrevious: dumpEnd == dumpStart");
            return;
        }
        mg_write(conn, dumpStart, dumpEnd-dumpStart);
        dumpStart = searchFromHere;
        dumpEnd = dumpStart;
    }

    void printPage() {
        if (!buffer) return;
        mg_printf(ctx.conn, "Content-Type: text/html\r\n\r\n");

        while (1) {
            std::string varname = getNextVariable();
            dumpPrevious(ctx.conn);
            if (varname.empty()) break;

            if (varname == K_SM_HTML_PROJECT_NAME && ctx.project) {
                mg_printf(ctx.conn, "%s", htmlEscape(ctx.project->getName()).c_str());

            } else if (varname == K_SM_URL_PROJECT_NAME && ctx.project) {
                    mg_printf(ctx.conn, "%s", ctx.project->getUrlName().c_str());

            } else if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                RHtml::printNavigationGlobal(ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES && ctx.project) {
                RHtml::printNavigationIssues(ctx, false);

            } else if (varname == K_SM_DIV_PROJECTS && projectList) {
                RHtml::printProjects(ctx.conn, *projectList, userRolesByProject);

            } else if (varname == K_SM_SCRIPT_PROJECT_CONFIG_UPDATE) {
                RHtml::printScriptUpdateConfig(ctx);

            } else if (varname == K_SM_RAW_ISSUE_ID && currentIssue) {
                mg_printf(ctx.conn, "%s", currentIssue->id.c_str());

            } else if (varname == K_SM_DIV_ISSUES && issueList && colspec) {
                RHtml::printIssueList(ctx, *issueList, *colspec);

            } else if (varname == K_SM_DIV_ISSUES && issueListFullContents) {
                RHtml::printIssueListFullContents(ctx, *issueListFullContents);

            } else if (varname == K_SM_DIV_ISSUE && currentIssue && entries) {
                RHtml::printIssue(ctx, *currentIssue, *entries);

            } else if (varname == K_SM_DIV_ISSUE_SUMMARY && currentIssue) {
                RHtml::printIssueSummary(ctx, *currentIssue);

            } else if (varname == K_SM_DIV_ISSUE_FORM) {
                Issue issue;
                RHtml::printIssueForm(ctx, issue, true);

            } else if (varname == K_SM_DIV_PREDEFINED_VIEWS) {
                RHtml::printLinksToPredefinedViews(ctx);

            } else {
                // unknown variable name
                mg_printf(ctx.conn, "%s", varname.c_str());
            }
        }

    }

private:
    const char *buffer;
    size_t size;
    const char * dumpStart;
    const char * dumpEnd;
    const char * searchFromHere;

};

/** Print the signin page and set the redirection field
  *
  * @param redirect
  *    May include a query string
  */
void RHtml::printPageSignin(struct mg_connection *conn, const char *redirect)
{

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::string path = Database::Db.getRootDir() + "/public/signin.html";
    const char *data;
    int r = loadFile(path.c_str(), &data);
    if (r > 0) {
        mg_write(conn, data, r);
        // add javascript for updating the redirect URL
        mg_printf(conn, "<script>document.getElementById(\"redirect\").value = \"%s\"</script>",
                  enquoteJs(redirect).c_str());
        free((void*)data);
    } else {
        LOG_ERROR("Could not load %s (or empty file)", path.c_str());
    }
}

void RHtml::printPageView(const ContextParameters &ctx, const PredefinedView &pv)
{
    struct mg_connection *conn = ctx.conn;

    VariableNavigator vn("view.html", ctx);
    vn.printPage();


    // add javascript for updating the inputs
    mg_printf(conn, "<script>\n");

    if (ctx.userRole != ROLE_ADMIN) {
        // hide what is reserved to admin
        mg_printf(conn, "hideAdminZone();\n");
    } else {
        mg_printf(conn, "setName('%s');\n", enquoteJs(pv.name).c_str());
    }
    if (pv.isDefault) mg_printf(conn, "setDefaultCheckbox();\n");
    std::list<std::string> properties = ctx.project->getPropertiesNames();
    mg_printf(conn, "Properties = %s;\n", toJavascriptArray(properties).c_str());
    mg_printf(conn, "setSearch('%s');\n", enquoteJs(pv.search).c_str());
    mg_printf(conn, "setUrl('/%s/issues/?%s');\n", ctx.project->getUrlName().c_str(),
              pv.generateQueryString().c_str());

    // filter in and out
    std::map<std::string, std::list<std::string> >::const_iterator f;
    std::list<std::string>::const_iterator v;
    FOREACH(f, pv.filterin) {
        FOREACH(v, f->second) {
            mg_printf(conn, "addFilter('filterin', '%s', '%s');\n",
                      enquoteJs(f->first).c_str(),
                      enquoteJs(*v).c_str());
        }
    }
    mg_printf(conn, "addFilter('filterin', '', '');\n");

    FOREACH(f, pv.filterout) {
        FOREACH(v, f->second) {
            mg_printf(conn, "addFilter('filterout', '%s', '%s');\n",
                      enquoteJs(f->first).c_str(),
                      enquoteJs(*v).c_str());
        }
    }
    mg_printf(conn, "addFilter('filterout', '', '');\n");

    // Colums specification
    if (!pv.colspec.empty()) {
        std::vector<std::string> items = split(pv.colspec, " +");
        std::vector<std::string>::iterator i;
        FOREACH(i, items) {
            mg_printf(conn, "addColspec('%s');\n", enquoteJs(*i).c_str());
        }
    }
    mg_printf(conn, "addColspec('');\n");

    // sort
    std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(pv.sort.c_str());
    std::list<std::pair<bool, std::string> >::iterator s;
    FOREACH(s, sSpec) {
        std::string direction = PredefinedView::getDirectionName(s->first);
        mg_printf(conn, "addSort('%s', '%s');\n", enquoteJs(direction).c_str(),
                  enquoteJs(s->second).c_str());
    }
    mg_printf(conn, "addSort('', '');\n");


    mg_printf(conn, "</script>\n");
}

void RHtml::printLinksToPredefinedViews(const ContextParameters &ctx)
{
    struct mg_connection *conn = ctx.conn;

    ProjectConfig c = ctx.getProject().getConfig();
    std::map<std::string, PredefinedView>::iterator pv;
    mg_printf(conn, "<table class=\"sm_views\">");
    mg_printf(conn, "<tr><th>%s</th><th>%s</th></tr>\n", _("Name"), _("Associated Url"));
    FOREACH(pv, c.predefinedViews) {
        mg_printf(conn, "<tr><td class=\"sm_views_name\">");
        mg_printf(conn, "<a href=\"%s\">%s</a>", urlEncode(pv->first).c_str(), htmlEscape(pv->first).c_str());
        mg_printf(conn, "</td><td class=\"sm_views_link\">");
        std::string qs = pv->second.generateQueryString();
        mg_printf(conn, "<a href=\"../issues/?%s\">%s</a>", qs.c_str(), htmlEscape(qs).c_str());
        mg_printf(conn, "</td></tr>\n");
    }
    mg_printf(conn, "<table>\n");
}

void RHtml::printPageListOfViews(const ContextParameters &ctx)
{
    VariableNavigator vn("views.html", ctx);
    vn.printPage();
}

#define LOCAL_SIZE 512
class HtmlNode {
public:
    HtmlNode(const std::string &_nodeName) {
        nodeName = _nodeName;
    }
    /** Constructor for text contents */
    HtmlNode() { }

    std::string nodeName;// empty if text content only
    std::map<std::string, std::string> attributes;
    std::string text;

    std::list<HtmlNode> contents;

    /** the values must be html-escaped */
    void addAttribute(const char *name, const char* format, ...) {
        va_list list;
        va_start(list, format);
        char value[LOCAL_SIZE];
        int n = vsnprintf(value, LOCAL_SIZE, format, list);
        if (n >= LOCAL_SIZE || n < 0) {
            LOG_ERROR("addAttribute error: vsnprintf n=%d", n);
        } else {
            attributes[name] = value;
        }
    }

    void print(struct mg_connection *conn) {
        if (nodeName.empty()) {
            // text contents
            mg_printf(conn, "%s", text.c_str());
        } else {
            mg_printf(conn, "<%s ", nodeName.c_str());
            std::map<std::string, std::string>::iterator i;
            FOREACH(i, attributes) {
                mg_printf(conn, "%s=\"%s\" ", i->first.c_str(), i->second.c_str());
            }
            mg_printf(conn, ">\n");

            if (nodeName == "input") return; // no closing tag nor any contents

            std::list<HtmlNode>::iterator c;
            FOREACH(c, contents) {
                c->print(conn);
            }
            // close HTML node
            mg_printf(conn, "</%s>\n", nodeName.c_str());
        }

    }
    void addContents(const char* format, ...) {
        va_list list;
        va_start(list, format);
        char buffer[LOCAL_SIZE];
        int n = vsnprintf(buffer, LOCAL_SIZE, format, list);
        if (n >= LOCAL_SIZE || n < 0) {
            LOG_ERROR("addText error: vsnprintf n=%d", n);
        } else {
            HtmlNode node;
            node.text = htmlEscape(buffer);
            contents.push_back(node);
        }
    }

    void addContents(const class HtmlNode &node) {
        contents.push_back(node);
    }

};

/** Return the query string associated to the predefined view
  */
std::string makeQueryString(const PredefinedView &pv)
{
    std::string qs;
    if (! pv.sort.empty()) {
        qs += "sort=" + pv.sort;
    }
    if (! pv.colspec.empty()) {
        if (!qs.empty()) qs += '&';
        qs += "colspec=" + pv.colspec;
    }

    if (! pv.search.empty()) {
        if (!qs.empty()) qs += '&';
        qs += "search=" + urlEncode(pv.search);
    }

    if (! pv.filterin.empty()) {
        std::map<std::string, std::list<std::string> >::const_iterator filterin;
        FOREACH(filterin, pv.filterin) {
            std::list<std::string>::const_iterator value;
            FOREACH(value, filterin->second) {
                if (!qs.empty()) qs += '&';
                qs += "filterin=" + filterin->first + ":" + urlEncode(*value);
            }
        }
    }

    if (! pv.filterout.empty()) {
        std::map<std::string, std::list<std::string> >::const_iterator filterout;
        FOREACH(filterout, pv.filterout) {
            std::list<std::string>::const_iterator value;
            FOREACH(value, filterout->second) {
                if (!qs.empty()) qs += '&';
                qs += "filterout=" + filterout->first + ":" + urlEncode(*value);
            }
        }
    }
    return qs;
}

/** Print global navigation bar
  *
  * - link to all projects page
  * - link to modify project (if admin)
  * - signed-in user indication and link to signout
  */
void RHtml::printNavigationGlobal(const ContextParameters &ctx)
{
    struct mg_connection *conn = ctx.conn;
    HtmlNode div("div");
    div.addAttribute("class", "sm_navigation_global");
    HtmlNode linkToProjects("a");
    linkToProjects.addAttribute("class", "sm_link_projects");
    linkToProjects.addAttribute("href", "/");
    linkToProjects.addContents("%s", _("All projects"));
    div.addContents(linkToProjects);

    if (ctx.userRole == ROLE_ADMIN) {
        // link for modifying project structure
        HtmlNode linkToModify("a");
        linkToModify.addAttribute("class", "sm_link_modify_project");
        linkToModify.addAttribute("href", "/%s/config", ctx.project->getUrlName().c_str());
        linkToModify.addContents("%s", _("Project configuration"));
        div.addContents(" ");
        div.addContents(linkToModify);

        // link to config of predefined views
        HtmlNode linkToViews("a");
        linkToViews.addAttribute("class", "sm_link_views");
        linkToViews.addAttribute("href", "/%s/views/", ctx.project->getUrlName().c_str());
        linkToViews.addContents("%s", _("Predefined Views"));
        div.addContents(" ");
        div.addContents(linkToViews);

    }

    // signed-in
    HtmlNode userinfo("span");
    userinfo.addAttribute("class", "sm_userinfo");
    userinfo.addContents("%s", _("Logged in as: "));
    HtmlNode username("span");
    username.addAttribute("class", "sm_username");
    username.addContents("%s", htmlEscape(ctx.username).c_str());
    userinfo.addContents(username);
    div.addContents(userinfo);

    // form to sign-out
    HtmlNode signout("form");
    signout.addAttribute("action", "/signout");
    signout.addAttribute("method", "post");
    signout.addAttribute("id", "sm_signout");
    signout.addContents("(");
    HtmlNode linkSignout("a");
    linkSignout.addAttribute("href", "javascript:;");
    linkSignout.addAttribute("onclick", "document.getElementById('sm_signout').submit();");
    linkSignout.addContents("%s", _("Sign out"));
    signout.addContents(linkSignout);
    signout.addContents(")");
    div.addContents(signout);

    div.print(conn);

}


/** Print links for navigating through issues;
  * - "create new issue"
  * - predefined views
  * - quick search form
  */
void RHtml::printNavigationIssues(const ContextParameters &ctx, bool autofocus)
{
    struct mg_connection *conn = ctx.conn;

    HtmlNode div("div");
    div.addAttribute("class", "sm_navigation_project");
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/new", ctx.project->getUrlName().c_str());
        a.addAttribute("class", "sm_link_new_issue");
        a.addContents("%s", _("Create new issue"));
        div.addContents(a);
    }

    std::map<std::string, PredefinedView>::iterator pv;
    ProjectConfig config = ctx.project->getConfig();
    FOREACH (pv, config.predefinedViews) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/?%s", ctx.project->getUrlName().c_str(),
                       makeQueryString(pv->second).c_str());
        a.addAttribute("class", "sm_predefined_view");
        a.addContents("%s", pv->first.c_str());
        div.addContents(a);
    }

    HtmlNode form("form");
    form.addAttribute("class", "sm_searchbox");
    form.addAttribute("action", "/%s/issues", ctx.project->getUrlName().c_str());
    form.addAttribute("method", "get");

    HtmlNode input("input");
    input.addAttribute("class", "sm_searchbox");
    input.addAttribute("placeholder", htmlEscape(_("Search text...")).c_str());
    input.addAttribute("type", "text");
    input.addAttribute("name", "search");
    if (autofocus) input.addAttribute("autofocus", "autofocus");
    form.addContents(input);
    div.addContents(form);

    // advanced search
    HtmlNode a("a");
    a.addAttribute("href", "/%s/views/_", ctx.project->getUrlName().c_str());
    a.addAttribute("class", "sm_advanced_search");
    a.addContents(_("Advanced Search"));
    div.addContents(a);


    div.print(conn);
}


void RHtml::printProjects(struct mg_connection *conn,
                          const std::list<std::pair<std::string, std::string> > &pList,
                          const std::map<std::string, std::map<std::string, Role> > *userRolesByProject)
{
    std::list<std::pair<std::string, std::string> >::const_iterator p;
    mg_printf(conn, "<table class=\"sm_projects\">\n");
    mg_printf(conn, "<tr class=\"sm_projects\"><th class=\"sm_projects\">%s</th>"
              "<th class=\"sm_projects\">%s</th><th class=\"sm_projects\">%s</th></tr>\n",
              _("Project"), _("Your Role"), _("Other Stakeholders"));
    for (p=pList.begin(); p!=pList.end(); p++) {
        std::string pname = p->first.c_str();
        mg_printf(conn, "<tr class=\"sm_projects\">\n");

        mg_printf(conn, "<td class=\"sm_projects_link\">");
        mg_printf(conn, "<a href=\"/%s/issues/?defaultView=1\" class=\"sm_projects_link\">%s</a></td>\n",
                  Project::urlNameEncode(pname).c_str(), htmlEscape(pname).c_str());

        mg_printf(conn, "<td>%s</td>\n", _(p->second.c_str()));

        mg_printf(conn, "<td>");
        std::map<std::string, std::map<std::string, Role> >::const_iterator urit;
        std::map<std::string, Role>::const_iterator urole;
        urit = userRolesByProject->find(pname);
        if (urit != userRolesByProject->end()) FOREACH(urole, urit->second) {
            if (urole != urit->second.begin()) mg_printf(conn, ", ");
            mg_printf(conn, "<span class=\"sm_projects_stakeholder\">%s</span>"
                      " <span class=\"sm_projects_stakeholder_role\">(%s)</span>",
                      htmlEscape(urole->first).c_str(), htmlEscape(roleToString(urole->second)).c_str());
        }
        mg_printf(conn, "</td>");
        mg_printf(conn, "</tr>\n");
    }
    mg_printf(conn, "</table>\n");
}


void RHtml::printPageProjectList(const ContextParameters &ctx,
                                 const std::list<std::pair<std::string, std::string> > &pList,
                                 const std::map<std::string, std::map<std::string, Role> > &userRolesByProject)
{
    VariableNavigator vn("projects.html", ctx);
    vn.projectList = &pList;
    vn.userRolesByProject = &userRolesByProject;
    vn.printPage();
}

/** Build a new query string based on the current one, and update the sorting part
  *
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
std::string getNewSortingSpec(struct mg_connection *conn, const std::string property, bool exclusive)
{
    const char *qstring = mg_get_request_info(conn)->query_string;
    std::string qs;
    if (qstring) qs = qstring;
    LOG_DEBUG("getNewSortingSpec: in=%s, exclusive=%d", qs.c_str(), exclusive);
    std::string result;

    const char *SORT_SPEC_HEADER = "sort=";
    std::string newSortingSpec = "";

    while (qs.size() > 0) {
        std::string part = popToken(qs, '&');
        if (0 == strncmp(SORT_SPEC_HEADER, part.c_str(), strlen(SORT_SPEC_HEADER))) {
            // sorting spec, that we want to alter
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
        }

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
    LOG_DEBUG("getNewSortingSpec: result=%s", result.c_str());


    return result;
}

/** Print javascript that will fulfill the inputs of the project configuration
  */
void RHtml::printScriptUpdateConfig(const ContextParameters &ctx)
{
    struct mg_connection *conn = ctx.conn;

    mg_printf(conn, "<script>\n");

    // fulfill reserved properties first
    std::list<std::string> reserved = ctx.getProject().getReservedProperties();
    std::list<std::string>::iterator r;
    FOREACH(r, reserved) {
        std::string x = replaceAll(ctx.getProject().getLabelOfProperty(*r), '\'', "\\\'");
        mg_printf(conn, "addProperty('%s', '%s', 'reserved', '');\n", r->c_str(),
                  x.c_str());
    }

    // other properties
    ProjectConfig c = ctx.getProject().getConfig();
    std::map<std::string, PropertySpec>::const_iterator ps;
    std::list<std::string>::const_iterator p;
    FOREACH(p, c.orderedProperties) {
        ps = c.properties.find((*p));
        if (ps != c.properties.end()) {
            PropertySpec pspec = ps->second;
            const char *type = "";
            switch (pspec.type) {
            case F_TEXT: type = "text"; break;
            case F_SELECT: type = "select"; break;
            case F_MULTISELECT: type = "multiselect"; break;
            case F_SELECT_USER: type = "selectUser"; break;
            }

            std::string property = replaceAll(ctx.getProject().getLabelOfProperty(*p), '\'', "\\\'");
            std::string value = replaceAll(toString(pspec.selectOptions, "\\n"), '\'', "\\\'");

            mg_printf(conn, "addProperty('%s', '%s', '%s', '%s');\n", p->c_str(),
                      property.c_str(),
                      type, value.c_str());
        }
    }

    // add 3 more empty properties
    mg_printf(conn, "addProperty('', '', '', '');\n");
    mg_printf(conn, "addProperty('', '', '', '');\n");
    mg_printf(conn, "addProperty('', '', '', '');\n");

    mg_printf(conn, "replaceContentInContainer();\n");
    mg_printf(conn, "</script>\n");
}


void RHtml::printProjectConfig(const ContextParameters &ctx)
{
    VariableNavigator vn("project.html", ctx);
    vn.printPage();
}

/** Get the property name that will be used for gouping

  * Grouping will occur only :
  *     - on the first property sorted-by
  *     - and if this property is of type select, multiselect or selectUser
  *
  * If the grouping must not occur, then an empty  string is returned.
  */
std::string getPropertyForGrouping(const ProjectConfig &pconfig, const std::string &sortingSpec)
{
    const char* colspecDelimiters = "+- ";
    if (sortingSpec.empty()) return "";

    size_t i = 0;
    if (strchr(colspecDelimiters, sortingSpec[0])) i = 1;

    std::string property;
    size_t n = sortingSpec.find_first_of(colspecDelimiters, i);
    if (n == std::string::npos) property = sortingSpec.substr(i);
    else property = sortingSpec.substr(i, n-i);

    // check the type of the property
    std::map<std::string, PropertySpec>::const_iterator propertySpec = pconfig.properties.find(property);
    if (propertySpec == pconfig.properties.end()) return "";
    enum PropertyType type = propertySpec->second.type;

    if (type == F_TEXT) return "";
    return property;
}

/** print chosen filters and search parameters
  */
void printFilters(const ContextParameters &ctx)
{
    struct mg_connection *conn = ctx.conn;
    if (!ctx.search.empty() || !ctx.filterin.empty() || !ctx.filterout.empty()) {
       mg_printf(conn, "<div class=\"sm_issues_filters\">");
       if (!ctx.search.empty()) mg_printf(conn, "search: %s<br>", htmlEscape(ctx.search).c_str());
       if (!ctx.filterin.empty()) mg_printf(conn, "filterin: %s<br>", htmlEscape(toString(ctx.filterin)).c_str());
       if (!ctx.filterout.empty()) mg_printf(conn, "filterout: %s", htmlEscape(toString(ctx.filterout)).c_str());
       mg_printf(conn, "</div>");
   }
}

void RHtml::printIssueListFullContents(const ContextParameters &ctx, std::vector<struct Issue*> issueList)
{
    struct mg_connection *conn = ctx.conn;

    mg_printf(conn, "<div class=\"sm_issues\">\n");

    printFilters(ctx);
    // number of issues
    mg_printf(conn, "<div class=\"sm_issues_count\">%s: <span class=\"sm_issues_count\">%u</span></div>\n",
              _("Issues found"), issueList.size());


    std::vector<struct Issue*>::iterator i;
    if (!ctx.project) {
        LOG_ERROR("Null project");
        return;
    }

    FOREACH (i, issueList) {
        Issue issue;
        int r = ctx.project->get((*i)->id.c_str(), issue);
        if (r < 0) {
            // issue not found (or other error)
            LOG_INFO("Issue disappeared: %s", (*i)->id.c_str());

        } else {

            std::list<Entry*> entries;
            issue.getEntries(entries);
            // deactivate user role
            ContextParameters ctxCopy = ctx;
            ctxCopy.userRole = ROLE_RO;
            printIssueSummary(ctxCopy, issue);
            printIssue(ctxCopy, issue, entries);

        }
    }
    mg_printf(conn, "</div>\n");

}

/** concatenate a param to the URL (add ? or &)
  */
std::string urlAdd(struct mg_connection *conn, const char *param)
{
    const struct mg_request_info *rq = mg_get_request_info(conn);
    std::string url;
    if (rq && rq->uri) {
        url = rq->uri;
        url += '?';
        if (rq->query_string && strlen(rq->query_string)) url = url + rq->query_string + '&' + param;
        else url += param;
    }
    return url;
}

void RHtml::printIssueList(const ContextParameters &ctx, const std::vector<struct Issue*> &issueList,
                     const std::list<std::string> &colspec)
{
    struct mg_connection *conn = ctx.conn;

    mg_printf(conn, "<div class=\"sm_issues\">\n");

    // add links to alternate download formats (CSV and full-contents)
    mg_printf(conn, "<div class=\"sm_issues_other_formats\">");
    mg_printf(conn, "<a href=\"%s\" class=\"sm_issues_other_formats\">csv</a> ", urlAdd(conn, "format=csv").c_str());
    mg_printf(conn, "<a href=\"%s\" class=\"sm_issues_other_formats\">full-contents</a></div>\n", urlAdd(conn, "full=1").c_str());

    printFilters(ctx);

    // number of issues
    mg_printf(conn, "<div class=\"sm_issues_count\">%s: <span class=\"sm_issues_count\">%u</span></div>\n",
              _("Issues found"), issueList.size());

    std::string group = getPropertyForGrouping(ctx.project->getConfig(), ctx.sort);
    std::string currentGroup;

    mg_printf(conn, "<table class=\"sm_issues\">\n");

    // print header of the table
    mg_printf(conn, "<tr class=\"sm_issues\">\n");
    std::list<std::string>::const_iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {

        std::string label = ctx.getProject().getLabelOfProperty(*colname);
        std::string newQueryString = getNewSortingSpec(conn, *colname, true);
        mg_printf(conn, "<th class=\"sm_issues\"><a class=\"sm_issues_sort\" href=\"?%s\" title=\"Sort ascending\">%s</a>\n",
                  newQueryString.c_str(), label.c_str());
        newQueryString = getNewSortingSpec(conn, *colname, false);
        mg_printf(conn, "\n<br><a href=\"?%s\" class=\"sm_issues_sort_cumulative\" ", newQueryString.c_str());
        mg_printf(conn, "title=\"%s\">&gt;&gt;&gt;</a></th>\n",
                  _("Sort while preserving order of other columns\n(or invert current column if already sorted-by)"));

    }
    mg_printf(conn, "</tr>\n");

    std::vector<struct Issue*>::const_iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        if (! group.empty() &&
            (i == issueList.begin() || getProperty((*i)->properties, group) != currentGroup) ) {
                // insert group bar if relevant
            mg_printf(conn, "<tr class=\"sm_issues_group\">\n");
            currentGroup = getProperty((*i)->properties, group);
            mg_printf(conn, "<td class=\"sm_group\" colspan=\"%u\"><span class=\"sm_issues_group_label\">%s: </span>",
                      colspec.size(), htmlEscape(ctx.getProject().getLabelOfProperty(group)).c_str());
            mg_printf(conn, "<span class=\"sm_issues_group\">%s</span></td>\n", htmlEscape(currentGroup).c_str());
            mg_printf(conn, "</tr>\n");
        }

        mg_printf(conn, "<tr class=\"sm_issues\">\n");

        std::list<std::string>::const_iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::ostringstream text;
            std::string column = *c;

            if (column == "id") text << (*i)->id.c_str();
            else if (column == "ctime") text << epochToStringDelta((*i)->ctime);
            else if (column == "mtime") text << epochToStringDelta((*i)->mtime);
            else {
                std::map<std::string, std::list<std::string> >::iterator p;
                std::map<std::string, std::list<std::string> > & properties = (*i)->properties;

                p = properties.find(column);
                if (p != properties.end()) text << toString(p->second);
            }
            // add href if column is 'id' or 'summary'
            std::string href_lhs = "";
            std::string href_rhs = "";
            if ( (column == "id") || (column == "summary") ) {
                href_lhs = "<a href=\"";
                href_lhs = href_lhs + "/" + ctx.getProject().getUrlName() + "/issues/";
                href_lhs = href_lhs + (char*)(*i)->id.c_str() + "\">";
                href_rhs = "</a>";
            }

            mg_printf(conn, "<td class=\"sm_issues\">%s%s%s</td>\n", href_lhs.c_str(), text.str().c_str(), href_rhs.c_str());
        }
        mg_printf(conn, "</tr>\n");
    }
    mg_printf(conn, "</table>\n");
    mg_printf(conn, "</div\n");
}

/** Print HTML page with the given issues and their full contents
  *
  */
void RHtml::printPageIssuesFullContents(const ContextParameters &ctx, std::vector<struct Issue*> issueList)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueListFullContents = &issueList;
    vn.printPage();
}

void RHtml::printPageIssueList(const ContextParameters &ctx,
                           std::vector<struct Issue*> issueList, std::list<std::string> colspec)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueList = &issueList;
    vn.colspec = &colspec;
    vn.printPage();
}


bool RHtml::inList(const std::list<std::string> &listOfValues, const std::string &value)
{
    std::list<std::string>::const_iterator v;
    for (v=listOfValues.begin(); v!=listOfValues.end(); v++) if (*v == value) return true;

    return false;
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
                                   bool dropDelimiters, const char *htmlTag, const char *htmlClass)
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

            } else if (c == '\n') {
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
  *    *a b c* => <span class="sm_bold">a b c</span>
  *    _a b c_ => <span class="sm_underline">a b c</span>
  *    /a b c/ => <span class="sm_highlight">a b c</span>
  *    [a b c] => <a href="a b c" class="sm_hyperlink">a b c</a>
  *    > a b c =>  <span class="sm_quote">a b c</span> (> must be at the beginning of the line)
  *
  * (optional) Characters before and after block must be [\t \n.;:]
  * A line break in the middle prevents the pattern from being recognized.
  */
std::string convertToRichText(const std::string &raw)
{
    std::string result = convertToRichTextInline(raw, "**", "**", true, "strong", "");
    result = convertToRichTextInline(result, "__", "__", true, "span", "sm_underline");
    result = convertToRichTextInline(result, "&quot;", "&quot;", false, "span", "sm_double_quote");
    result = convertToRichTextInline(result, "++", "++", true, "em", "");
    result = convertToRichTextInline(result, "[[", "]]", true, "a", "sm_hyperlink");
    result = convertToRichTextWholeline(result, "&gt;", "span", "sm_quote");
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
    if (extension == ".gif") return true;
    if (extension == ".png") return true;
    if (extension == ".jpg") return true;
    if (extension == ".jpeg") return true;
    if (extension == ".svg") return true;
    return false;
}

/** print id and summary of an issue
  *
  */
void RHtml::printIssueSummary(const ContextParameters &ctx, const Issue &issue)
{
    struct mg_connection *conn = ctx.conn;
    mg_printf(conn, "<div class=\"sm_issue_header\">\n");
    mg_printf(conn, "<span class=\"sm_issue_id\">%s</span>\n", issue.id.c_str());
    mg_printf(conn, "<span class=\"sm_issue_summary\">%s</span>\n", htmlEscape(issue.getSummary()).c_str());
    mg_printf(conn, "</div>\n");

}

void RHtml::printIssue(const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    struct mg_connection *conn = ctx.conn;
    mg_printf(conn, "<div class=\"sm_issue\">");

    // print id and summary
    //RHtml::printIssueSummary(ctx, issue);

    // issue properties in a two-column table
    // -------------------------------------------------
    mg_printf(conn, "<table class=\"sm_issue_properties\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;

    std::list<std::string> orderedProperties = ctx.getProject().getConfig().orderedProperties;

    std::list<std::string>::const_iterator f;
    for (f=orderedProperties.begin(); f!=orderedProperties.end(); f++) {
        std::string pname = *f;
        std::string label = ctx.getProject().getLabelOfProperty(pname);

        std::string value;
        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(pname);

        if (p != issue.properties.end()) value = toString(p->second);

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }
        mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n", pname.c_str(), htmlEscape(label).c_str());
        mg_printf(conn, "<td class=\"sm_issue_pvalue sm_issue_pvalue_%s\">%s</td>\n", pname.c_str(), htmlEscape(value).c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }
    mg_printf(conn, "</table>\n");

    // links to id of the same page
    mg_printf(conn, "<div class=\"sm_issue_links\">");

    // add a link to edit form if role enables it
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
        mg_printf(conn, "<span class=\"sm_issue_link_edit\">");
        mg_printf(conn, "<a href=\"#edit_form\" class=\"sm_issue_link_edit\">%s</a>", _("Edit"));
        mg_printf(conn, "</span> ");
    }
#if 0
    >>> deactivate this block, as floating menu prevents from having an ergonomic link to last entry
    mg_printf(conn, "<span class=\"sm_issue_link_last_entry\">");
    mg_printf(conn, "<a href=\"#%s\" class=\"sm_issue_link_edit\">%s</a>",
              entries.back()->id.c_str(), _("Go to latest message"));
    mg_printf(conn, "</span>");
#endif

    mg_printf(conn, "</div>"); // links

    // entries
    // -------------------------------------------------
    std::list<Entry*>::const_iterator e;
    for (e = entries.begin(); e != entries.end(); e++) {
        Entry ee = *(*e);
        mg_printf(conn, "<div class=\"sm_entry\" id=\"%s\">\n", ee.id.c_str());

        mg_printf(conn, "<div class=\"sm_entry_header\">\n");
        mg_printf(conn, "<span class=\"sm_entry_author\">%s</span>", htmlEscape(ee.author).c_str());
        mg_printf(conn, ", <span class=\"sm_entry_ctime\">%s</span>\n", epochToString(ee.ctime).c_str());
        // conversion of date in javascript
        // document.write(new Date(%d)).toString());

        // delete button
        time_t delta = time(0) - ee.ctime;
        std::list<Entry*>::const_iterator lastEntryIt = entries.end();
        lastEntryIt--;
        if ( (delta < DELETE_DELAY_S) && (ee.author == ctx.username) && (e == lastEntryIt) ) {
            // entry was created less than 10 minutes ago, and by same user, and is latest in the issue
            mg_printf(conn, "<a href=\"#\" class=\"sm_entry_delete\" title=\"Delete this entry (at most %d minutes after posting)\" ", (DELETE_DELAY_S/60));
            mg_printf(conn, " onclick=\"deleteEntry('/%s/issues/%s', '%s');return false;\">\n",
                      ctx.getProject().getUrlName().c_str(), issue.id.c_str(), ee.id.c_str());
            mg_printf(conn, "&#10008; delete");
            mg_printf(conn, "</a>\n");
        }

        // link to raw entry
        mg_printf(conn, "(<a href=\"/%s/issues/%s/%s\" class=\"sm_entry_raw\">%s</a>)\n",
                  ctx.getProject().getUrlName().c_str(),
                  issue.id.c_str(), ee.id.c_str(), _("raw"));


        mg_printf(conn, "</div>\n"); // end header

        std::string m = ee.getMessage();
        if (! m.empty()) {
            mg_printf(conn, "<div class=\"sm_entry_message\">");
            mg_printf(conn, "%s\n", convertToRichText(htmlEscape(m)).c_str());
			mg_printf(conn, "</div>\n"); // end message
        }


        // uploaded files
        std::map<std::string, std::list<std::string> >::iterator files = ee.properties.find(K_FILE);
        if (files != ee.properties.end() && files->second.size() > 0) {
            mg_printf(conn, "<div class=\"sm_entry_files\">\n");
            std::list<std::string>::iterator f;
            FOREACH(f, files->second) {
                std::string shortName = *f;
                if (shortName.size()>28) shortName = shortName.substr(28); // 28 fist characters are the sha1 prefix
                mg_printf(conn, "<div class=\"sm_entry_file\">\n");
                mg_printf(conn, "<a href=\"../%s/%s\" class=\"sm_entry_file\">", K_UPLOADED_FILES_DIR,
                          urlEncode(*f).c_str());
                if (isImage(*f)) {
                    mg_printf(conn, "<img src=\"../files/%s\" class=\"sm_entry_file\"> ", urlEncode(*f).c_str());
                }
                mg_printf(conn, "%s", htmlEscape(shortName).c_str());
                mg_printf(conn, "</a>");
                mg_printf(conn, "</div>\n"); // end file
            }
            mg_printf(conn, "</div>\n"); // end files
        }

        // print other modified properties
        // -------------------------------------------------
        std::ostringstream otherProperties;
        bool firstInList = true;

        // process summary first as it is not part of orderedFields
        std::map<std::string, std::list<std::string> >::const_iterator p = ee.properties.find(K_SUMMARY);
        std::string value;
        if (p != ee.properties.end()) {
            value = toString(p->second);
            otherProperties << "<span class=\"sm_entry_pname\">" << ctx.getProject().getLabelOfProperty(K_SUMMARY) << ": </span>";
            otherProperties << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";
            firstInList = false;
        }

        // other properties
        FOREACH(f, orderedProperties) {
            std::string pname = *f;

            std::map<std::string, std::list<std::string> >::const_iterator p = ee.properties.find(pname);
            if (p != ee.properties.end()) {
                // the entry has this property
                value = toString(p->second);

                if (!firstInList) otherProperties << ", "; // separate properties by a comma
                otherProperties << "<span class=\"sm_entry_pname\">" << ctx.getProject().getLabelOfProperty(pname) << ": </span>";
                otherProperties << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";
                firstInList = false;
            }
        }

        if (otherProperties.str().size() > 0) {
            mg_printf(conn, "<div class=\"sm_entry_other_properties\">\n");
            mg_printf(conn, "%s", otherProperties.str().c_str());
            mg_printf(conn, "</div>\n");
        }

        mg_printf(conn, "</div>\n"); // end entry

    } // end of entries

    // print the form
    // -------------------------------------------------
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
        RHtml::printIssueForm(ctx, issue, false);
    }
    mg_printf(conn, "</div>\n");

}

void RHtml::printPageIssue(const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    VariableNavigator vn("issue.html", ctx);
    vn.currentIssue = &issue;
    vn.entries = &entries;
    vn.printPage();
}



void RHtml::printPageNewIssue(const ContextParameters &ctx)
{
    VariableNavigator vn("newIssue.html", ctx);
    vn.printPage();
}


/** print form for adding a message / modifying the issue
  */
void RHtml::printIssueForm(const ContextParameters &ctx, const Issue &issue, bool autofocus)
{
    struct mg_connection *conn = ctx.conn;

    // enctype=\"multipart/form-data\"
    mg_printf(conn, "<form enctype=\"multipart/form-data\" method=\"post\"  class=\"sm_issue_form\" id=\"edit_form\">");
    // print the fields of the issue in a two-column table

    // The form is made over a table with 4 columns.
    // each row is made of 1 label, 1 input, 1 label, 1 input (4 columns)
    // except for the summary.
    // summary
    mg_printf(conn, "<table class=\"sm_issue_properties\">");
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_summary\">%s: </td>\n", ctx.getProject().getLabelOfProperty("summary").c_str());
    mg_printf(conn, "<td class=\"sm_issue_pinput\" colspan=\"3\">");

    mg_printf(conn, "<input class=\"sm_issue_pinput_summary\" required=\"required\" type=\"text\" name=\"summary\" value=\"%s\"",
              htmlEscape(issue.getSummary()).c_str());
    if (autofocus) mg_printf(conn, " autofocus");
    mg_printf(conn, ">");
    mg_printf(conn, "</td>\n");
    mg_printf(conn, "</tr>\n");

    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;
    std::list<std::string>::const_iterator f;

    std::list<std::string> orderedProperties = ctx.getProject().getConfig().orderedProperties;

    std::map<std::string, PropertySpec> properties = ctx.getProject().getConfig().properties;


    for (f=orderedProperties.begin(); f!=orderedProperties.end(); f++) {
        std::string pname = *f;
        std::string label = ctx.getProject().getLabelOfProperty(pname);

        std::map<std::string, PropertySpec>::const_iterator propertySpec = properties.find(pname);
        if (propertySpec == properties.end()) {
            LOG_ERROR("Property '%s' (of setHtmlFieldDisplay) not found in addField options", pname.c_str());
            continue;
        }

        PropertySpec pspec = propertySpec->second;

        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(pname);
        std::list<std::string> propertyValues;
        if (p!=issue.properties.end()) propertyValues = p->second;

        std::ostringstream input;
        std::string value;

        if (pspec.type == F_TEXT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<input class=\"sm_pinput_" << pname << "\" type=\"text\" name=\""
                  << pname << "\" value=\"" << htmlEscape(value) << "\">\n";

        } else if (pspec.type == F_SELECT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\">";

            for (so = pspec.selectOptions.begin(); so != pspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (value == *so) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (pspec.type == F_MULTISELECT) {
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\"";
            if (pspec.type == F_MULTISELECT) input << " multiple=\"multiple\"";
            input << ">";

            for (so = pspec.selectOptions.begin(); so != pspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (inList(propertyValues, *so)) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (pspec.type == F_SELECT_USER) {
            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\">";

            // TODO
            std::set<std::string> users = UserBase::getUsersOfProject(ctx.getProject().getName());
            std::set<std::string>::iterator u;
            for (u = users.begin(); u != users.end(); u++) {
                input << "<option" ;
                if (value == *u) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*u) << "</option>";
            }

            input << "</select>";


        } else {
            LOG_ERROR("invalid fieldSpec->type=%d", pspec.type);
            continue;
        }

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }
        mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n", pname.c_str(), label.c_str());
        mg_printf(conn, "<td class=\"sm_issue_pinput\">%s</td>\n", input.str().c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }

    if (workingColumn != 1) {
        // add an empty cell
        mg_printf(conn, "<td></td></tr>\n");
    }
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_message\" >%s: </td>\n", _("Message"));
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<textarea class=\"sm_issue_pinput sm_issue_pinput_message\" placeholder=\"%s\" name=\"%s\" wrap=\"hard\" cols=\"80\">\n",
              _("Enter a message"), K_MESSAGE);
    mg_printf(conn, "</textarea>\n");
    mg_printf(conn, "</td></tr>\n");

    // check box "enable long lines"
    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td class=\"sm_issue_longlines\" colspan=\"3\">\n");
    mg_printf(conn, "<label><input type=\"checkbox\" onclick=\"changeWrapping();\">\n");
    mg_printf(conn, "%s\n", _("Enable long lines"));
    mg_printf(conn, "</label></td></tr>\n");


    // add file upload input
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_file\" >%s: </td>\n", _("File Upload"));
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<input type=\"file\" name=\"%s\" class=\"sm_issue_input_file\" onchange=\"updateFileInput('sm_input_file');\">\n", K_FILE);
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<input type=\"submit\" value=\"%s\">\n", ctx.getProject().getLabelOfProperty("Add-Message").c_str());
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "</table>\n");

    mg_printf(conn, "</form>");

    mg_printf(conn, "</div>");
}
