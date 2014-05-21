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

/** Build a context for a user and project
  *
  * ContextParameters::projectConfig should be user rather than ContextParameters::project.getConfig()
  * as getConfig locks a mutex.
  * ContextParameters::projectConfig gets the config once at initilisation,
  * and afterwards one can work with the copy (without locking).
  */
ContextParameters::ContextParameters(struct mg_connection *cnx, User u, Project &p)
{
    project = &p;
    projectConfig = p.getConfig(); // take a copy of the config
    user = u;
    userRole = u.getRole(p.getName());
    conn = cnx;
}

ContextParameters::ContextParameters(struct mg_connection *cnx, User u)
{
    project = 0;
    user = u;
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
#define K_SM_DIV_USERS "SM_DIV_USERS"
#define K_SM_DIV_ISSUES "SM_DIV_ISSUES"
#define K_SM_DIV_ISSUE "SM_DIV_ISSUE"
#define K_SM_DIV_ISSUE_SUMMARY "SM_DIV_ISSUE_SUMMARY" // deprecated
#define K_SM_HTML_ISSUE_SUMMARY "SM_HTML_ISSUE_SUMMARY"
#define K_SM_DIV_ISSUE_FORM "SM_DIV_ISSUE_FORM"
#define K_SM_DIV_ISSUE_MSG_PREVIEW "SM_DIV_ISSUE_MSG_PREVIEW"

std::string enquoteJs(const std::string &in)
{
    std::string out = replaceAll(in, '\\', "\\\\"); // escape backslahes
    out = replaceAll(out, '\'', "\\'"); // escape '
    out = replaceAll(out, '"', "\\\""); // escape "
    out = replaceAll(out, '\n', "\\n"); // escape \n
    return out;

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

/** class in charge of replacing the SM_ variables in the HTML pages
  *
  */
class VariableNavigator {
public:
    std::vector<struct Issue*> *issueList;
    std::vector<struct Issue*> *issueListFullContents;
    std::list<std::string> *colspec;
    const ContextParameters &ctx;
    const std::list<std::pair<std::string, std::string> > *projectList;
    const std::list<User> *usersList;
    const Issue *currentIssue;
    const std::map<std::string, std::map<std::string, Role> > *userRolesByProject;

    VariableNavigator(const std::string basename, const ContextParameters &context) : ctx(context) {
        buffer = 0;
        issueList = 0;
        issueListFullContents = 0;
        colspec = 0;
        projectList = 0;
        usersList = 0;
        currentIssue = 0;
        userRolesByProject = 0;

        int n;
        if (ctx.project) n = loadProjectPage(ctx.conn, ctx.getProject().getPath(), basename, &buffer);
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
        if (buffer) {
            LOG_DEBUG("Freeing buffer %p", buffer);
            free((void*)buffer); // the buffer was allocated in loadFile or in loadProjectPage
            buffer = 0;
        }
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
                if (ctx.getProject().getName().empty()) mg_printf(ctx.conn, "(new)");
                else mg_printf(ctx.conn, "%s", htmlEscape(ctx.getProject().getName()).c_str());

            } else if (varname == K_SM_URL_PROJECT_NAME && ctx.project) {
                    mg_printf(ctx.conn, "%s", ctx.getProject().getUrlName().c_str());

            } else if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                RHtml::printNavigationGlobal(ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES && ctx.project) {
                // do not print this in case a a new project
                if (! ctx.getProject().getName().empty()) RHtml::printNavigationIssues(ctx, false);

            } else if (varname == K_SM_DIV_PROJECTS && projectList) {
                RHtml::printProjects(ctx, *projectList, userRolesByProject);

            } else if (varname == K_SM_DIV_USERS && usersList) {
                RHtml::printUsers(ctx.conn, *usersList);

            } else if (varname == K_SM_SCRIPT_PROJECT_CONFIG_UPDATE) {
                RHtml::printScriptUpdateConfig(ctx);

            } else if (varname == K_SM_RAW_ISSUE_ID && currentIssue) {
                mg_printf(ctx.conn, "%s", htmlEscape(currentIssue->id).c_str());

            } else if (varname == K_SM_HTML_ISSUE_SUMMARY && currentIssue) {
                mg_printf(ctx.conn, "%s", htmlEscape(currentIssue->getSummary()).c_str());

            } else if (varname == K_SM_DIV_ISSUES && issueList && colspec) {
                RHtml::printIssueList(ctx, *issueList, *colspec);

            } else if (varname == K_SM_DIV_ISSUES && issueListFullContents) {
                RHtml::printIssueListFullContents(ctx, *issueListFullContents);

            } else if (varname == K_SM_DIV_ISSUE && currentIssue) {
                RHtml::printIssue(ctx, *currentIssue);

            } else if (varname == K_SM_DIV_ISSUE_SUMMARY && currentIssue) {
                RHtml::printIssueSummary(ctx, *currentIssue);

            } else if (varname == K_SM_DIV_ISSUE_FORM) {
                Issue issue;
                if (!currentIssue)  currentIssue = &issue;
                RHtml::printIssueForm(ctx, currentIssue, false);

            } else if (varname == K_SM_DIV_PREDEFINED_VIEWS) {
                RHtml::printLinksToPredefinedViews(ctx);

            } else if (varname == K_SM_DIV_ISSUE_MSG_PREVIEW) {
                mg_printf(ctx.conn, "<div id=\"sm_entry_preview\" class=\"sm_entry_message\">"
                          "%s"
                          "</div>", htmlEscape(_("...message preview...")).c_str());

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


void RHtml::printPageUser(const ContextParameters &ctx, const User *u)
{
    VariableNavigator vn("user.html", ctx);
    vn.printPage();

    // add javascript for updating the inputs
    struct mg_connection *conn = ctx.conn;

    mg_printf(conn, "<script>\n");

    if (!ctx.user.superadmin) {
        // hide what is reserved to superadmin
        mg_printf(conn, "hideSuperadminZone();\n");
    } else {
        if (u) mg_printf(conn, "setName('%s');\n", enquoteJs(u->username).c_str());
    }

    if (u && u->superadmin) {
        mg_printf(conn, "setSuperadminCheckbox();\n");
    }

    std::list<std::string> projects = Database::getProjects();
    mg_printf(conn, "Projects = %s;\n", toJavascriptArray(projects).c_str());
    std::list<std::string> roleList = getAvailableRoles();
    mg_printf(conn, "Roles = %s;\n", toJavascriptArray(roleList).c_str());

    if (u) {
        std::map<std::string, enum Role>::const_iterator rop;
        FOREACH(rop, u->rolesOnProjects) {
            mg_printf(conn, "addProject('roles_on_projects', '%s', '%s');\n",
                      enquoteJs(rop->first).c_str(),
                      enquoteJs(roleToString(rop->second)).c_str());
        }
    }
    mg_printf(conn, "addProject('roles_on_projects', '', '');\n");

    mg_printf(conn, "</script>\n");
}

void RHtml::printPageView(const ContextParameters &ctx, const PredefinedView &pv)
{
    struct mg_connection *conn = ctx.conn;

    VariableNavigator vn("view.html", ctx);
    vn.printPage();


    // add javascript for updating the inputs
    mg_printf(conn, "<script>\n");

    if (ctx.userRole != ROLE_ADMIN && !ctx.user.superadmin) {
        // hide what is reserved to admin
        mg_printf(conn, "hideAdminZone();\n");
    } else {
        mg_printf(conn, "setName('%s');\n", enquoteJs(pv.name).c_str());
    }
    if (pv.isDefault) mg_printf(conn, "setDefaultCheckbox();\n");
    std::list<std::string> properties = ctx.projectConfig.getPropertiesNames();
    mg_printf(conn, "Properties = %s;\n", toJavascriptArray(properties).c_str());

    // add PropertiesLists, for proposing the values in filterin/out
    mg_printf(conn, "PropertiesLists = {};\n");
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, ctx.projectConfig.properties) {
        PropertyType t = pspec->type;
        if (t == F_SELECT || t == F_MULTISELECT) {
            mg_printf(conn, "PropertiesLists['%s'] = %s;\n", pspec->name.c_str(),
                      toJavascriptArray(pspec->selectOptions).c_str());

        } else if (t == F_SELECT_USER) {
            std::set<std::string>::const_iterator u;
            std::set<std::string> users = UserBase::getUsersOfProject(ctx.getProject().getName());
            // convert to std::list
            std::list<std::string> userList;
            userList.push_back("me");
            FOREACH(u, users) { userList.push_back(*u); }
            mg_printf(conn, "PropertiesLists['%s'] = %s;\n", pspec->name.c_str(),
                      toJavascriptArray(userList).c_str());
        }
    }

    mg_printf(conn, "setSearch('%s');\n", enquoteJs(pv.search).c_str());
    mg_printf(conn, "setUrl('/%s/issues/?%s');\n", ctx.getProject().getUrlName().c_str(),
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

    const ProjectConfig &c = ctx.projectConfig;
    std::map<std::string, PredefinedView>::const_iterator pv;
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

    if (ctx.project && (ctx.userRole == ROLE_ADMIN || ctx.user.superadmin) ) {
        // link for modifying project structure
        HtmlNode linkToModify("a");
        linkToModify.addAttribute("class", "sm_link_modify_project");
        linkToModify.addAttribute("href", "/%s/config", ctx.getProject().getUrlName().c_str());
        linkToModify.addContents("%s", _("Project configuration"));
        div.addContents(" ");
        div.addContents(linkToModify);

        // link to config of predefined views
        HtmlNode linkToViews("a");
        linkToViews.addAttribute("class", "sm_link_views");
        linkToViews.addAttribute("href", "/%s/views/", ctx.getProject().getUrlName().c_str());
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
    username.addContents("%s", htmlEscape(ctx.user.username).c_str());
    userinfo.addContents(username);
    div.addContents(userinfo);
    div.addContents(" - ");

    // form to sign-out
    HtmlNode signout("form");
    signout.addAttribute("action", "/signout");
    signout.addAttribute("method", "post");
    signout.addAttribute("id", "sm_signout");
    HtmlNode linkSignout("a");
    linkSignout.addAttribute("href", "javascript:;");
    linkSignout.addAttribute("onclick", "document.getElementById('sm_signout').submit();");
    linkSignout.addContents("%s", _("Sign out"));
    signout.addContents(linkSignout);
    div.addContents(signout);

    div.addContents(" - ");

    // link to user profile
    HtmlNode linkToProfile("a");
    linkToProfile.addAttribute("href", "/users/%s", urlEncode(ctx.user.username).c_str());
    linkToProfile.addContents(_("Profile"));
    div.addContents(linkToProfile);

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
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW || ctx.user.superadmin) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/new", ctx.getProject().getUrlName().c_str());
        a.addAttribute("class", "sm_link_new_issue");
        a.addContents("%s", _("Create new issue"));
        div.addContents(a);
    }

    std::map<std::string, PredefinedView>::const_iterator pv;
    const ProjectConfig &config = ctx.projectConfig;
    FOREACH (pv, config.predefinedViews) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/?%s", ctx.getProject().getUrlName().c_str(),
                       makeQueryString(pv->second).c_str());
        a.addAttribute("class", "sm_predefined_view");
        a.addContents("%s", pv->first.c_str());
        div.addContents(a);
    }

    HtmlNode form("form");
    form.addAttribute("class", "sm_searchbox");
    form.addAttribute("action", "/%s/issues", ctx.getProject().getUrlName().c_str());
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
    std::string view;
    if (!ctx.originView.empty()) {
        view = "?" QS_ORIGIN_VIEW "=" + urlEncode(ctx.originView);
    }
    a.addAttribute("href", "/%s/views/_%s", ctx.getProject().getUrlName().c_str(), view.c_str());
    a.addAttribute("class", "sm_advanced_search");
    a.addContents(_("Advanced Search"));
    div.addContents(a);


    div.print(conn);
}


void RHtml::printProjects(const ContextParameters &ctx,
                          const std::list<std::pair<std::string, std::string> > &pList,
                          const std::map<std::string, std::map<std::string, Role> > *userRolesByProject)
{
    struct mg_connection *conn = ctx.conn;

    std::list<std::pair<std::string, std::string> >::const_iterator p;
    mg_printf(conn, "<table class=\"sm_projects\">\n");
    mg_printf(conn, "<tr class=\"sm_projects\"><th class=\"sm_projects\">%s</th>"
              "<th class=\"sm_projects\">%s</th><th class=\"sm_projects\">%s</th></tr>\n",
              _("Projects"), _("My Role"), _("Other Stakeholders"));
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
    mg_printf(conn, "</table><br>\n");
    if (ctx.user.superadmin) mg_printf(conn, "<div class=\"sm_projects_new\">"
                                       "<a href=\"/_\" class=\"sm_projects_new\">%s</a></div><br>",
                                       htmlEscape(_("New Project")).c_str());
}

void RHtml::printUsers(struct mg_connection *conn, const std::list<User> &usersList)
{
    std::list<User>::const_iterator u;

    if (usersList.empty()) return;

    mg_printf(conn, "<table class=\"sm_users\">\n");
    mg_printf(conn, "<tr class=\"sm_users\">");
    mg_printf(conn, "<th class=\"sm_users\">%s</th>\n", _("Users"));
    mg_printf(conn, "<th class=\"sm_users\">%s</th></tr>\n", _("Capabilities"));
    mg_printf(conn, "</tr>");

    FOREACH(u, usersList) {
        mg_printf(conn, "<tr class=\"sm_users\">");
        mg_printf(conn, "<td class=\"sm_users\">\n");
        mg_printf(conn, "<a href=\"/users/%s\">%s<a><br>",
                  urlEncode(u->username).c_str(), htmlEscape(u->username).c_str());
        mg_printf(conn, "</td>");
        // capability
        if (u->superadmin) mg_printf(conn, "<td class=\"sm_users\">%s</td>\n", _("superadmin"));
        else mg_printf(conn, "<td class=\"sm_users\"> </td>\n");

        mg_printf(conn, "</tr>\n");

    }    
    mg_printf(conn, "</table><br>\n");
    mg_printf(conn, "<div class=\"sm_users_new\">"
              "<a href=\"/users/_\" class=\"sm_users_new\">%s</a></div><br>",
              htmlEscape(_("New User")).c_str());

}

void RHtml::printPageProjectList(const ContextParameters &ctx,
                                 const std::list<std::pair<std::string, std::string> > &pList,
                                 const std::map<std::string, std::map<std::string, Role> > &userRolesByProject,
                                 const std::list<User> &users)
{
    VariableNavigator vn("projects.html", ctx);
    vn.projectList = &pList;
    vn.usersList = &users;
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
    std::list<std::string> reserved = ctx.projectConfig.getReservedProperties();
    std::list<std::string>::iterator r;
    FOREACH(r, reserved) {
        std::string label = ctx.projectConfig.getLabelOfProperty(*r);
        mg_printf(conn, "addProperty('%s', '%s', 'reserved', '');\n", enquoteJs(*r).c_str(),
                  enquoteJs(label).c_str());
    }

    // other properties
    const ProjectConfig &c = ctx.projectConfig;
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, c.properties) {
        const char *type = "";
        switch (pspec->type) {
        case F_TEXT: type = "text"; break;
        case F_SELECT: type = "select"; break;
        case F_MULTISELECT: type = "multiselect"; break;
        case F_SELECT_USER: type = "selectUser"; break;
        case F_TEXTAREA: type = "textarea"; break;
        case F_TEXTAREA2: type = "textarea2"; break;
        case F_ASSOCIATION: type = "association"; break;
        }

        std::string label = ctx.projectConfig.getLabelOfProperty(pspec->name);
        std::list<std::string>::const_iterator i;
        std::string options;
		if (pspec->type == F_SELECT || pspec->type == F_MULTISELECT) {
			FOREACH (i, pspec->selectOptions) {
				if (i != pspec->selectOptions.begin()) options += "\\n";
				options += enquoteJs(*i);
			}
        } else if (pspec->type == F_ASSOCIATION) {
            options = pspec->reverseLabel;
		}
        mg_printf(conn, "addProperty('%s', '%s', '%s', '%s');\n", pspec->name.c_str(),
                  enquoteJs(label).c_str(),
                  type, options.c_str());
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

    mg_printf(ctx.conn, "<script>");
    if (!ctx.user.superadmin) {
        // hide what is reserved to superadmin
        mg_printf(ctx.conn, "hideSuperadminZone();\n");
    } else {
        if (ctx.project) mg_printf(ctx.conn, "setProjectName('%s');\n", enquoteJs(ctx.getProject().getName()).c_str());
    }
    mg_printf(ctx.conn, "</script>");

}

/** Get the property name that will be used for grouping

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

    // get the first property sorted-by
    std::string property;
    size_t n = sortingSpec.find_first_of(colspecDelimiters, i);
    if (n == std::string::npos) property = sortingSpec.substr(i);
    else property = sortingSpec.substr(i, n-i);

    // check the type of this property
    const PropertySpec *propertySpec = pconfig.getPropertySpec(property);
    if (!propertySpec) return "";
    enum PropertyType type = propertySpec->type;

    if (type == F_SELECT || type == F_MULTISELECT || type == F_SELECT_USER) return property;
    return "";
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
        int r = ctx.getProject().get((*i)->id, issue);
        if (r < 0) {
            // issue not found (or other error)
            LOG_INFO("Issue disappeared: %s", (*i)->id.c_str());

        } else {

            // deactivate user role
            ContextParameters ctxCopy = ctx;
            ctxCopy.userRole = ROLE_RO;
            printIssueSummary(ctxCopy, issue);
            printIssue(ctxCopy, issue);

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

    std::string group = getPropertyForGrouping(ctx.projectConfig, ctx.sort);
    std::string currentGroup;

    mg_printf(conn, "<table class=\"sm_issues\">\n");

    // print header of the table
    mg_printf(conn, "<tr class=\"sm_issues\">\n");
    std::list<std::string>::const_iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {

        std::string label = ctx.projectConfig.getLabelOfProperty(*colname);
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
                      colspec.size(), htmlEscape(ctx.projectConfig.getLabelOfProperty(group)).c_str());
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
                std::string href = "/" + ctx.getProject().getUrlName() + "/issues/";
                href += (char*)(*i)->id.c_str();
                href += "?" QS_ORIGIN_VIEW "=" + urlEncode(ctx.originView);
                href_lhs = href_lhs + href;
                href_lhs = href_lhs +  + "\">";
                // add origin view

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
  *    *a b c* => <span class="sm_bold">a b c</span>
  *    _a b c_ => <span class="sm_underline">a b c</span>
  *    /a b c/ => <span class="sm_highlight">a b c</span>
  *    [a b c] => <a href="a b c" class="sm_hyperlink">a b c</a>
  *    > a b c =>  <span class="sm_quote">a b c</span> (> must be at the beginning of the line)
  *
  * (optional) Characters before and after block must be [\t \n.;:]
  * A line break in the middle prevents the pattern from being recognized.
  */
std::string RHtml::convertToRichText(const std::string &raw)
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
void RHtml::printIssueSummary(const ContextParameters &ctx, const Issue &issue)
{
    struct mg_connection *conn = ctx.conn;
    mg_printf(conn, "<div class=\"sm_issue_header\">\n");
    mg_printf(conn, "<span class=\"sm_issue_id\">%s</span>\n", issue.id.c_str());
    mg_printf(conn, "<span class=\"sm_issue_summary\">%s</span>\n", htmlEscape(issue.getSummary()).c_str());
    mg_printf(conn, "</div>\n");

}

void RHtml::printIssue(const ContextParameters &ctx, const Issue &issue)
{
    struct mg_connection *conn = ctx.conn;
    mg_printf(conn, "<div class=\"sm_issue\">");

    // issue properties in a two-column table
    // -------------------------------------------------
    mg_printf(conn, "<table class=\"sm_issue_properties\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;

    const ProjectConfig &pconfig = ctx.projectConfig;

    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, pconfig.properties) {

        std::string pname = pspec->name;
        enum PropertyType type = pspec->type;

        // look if the issue has a value for this property
        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(pname);
        // if the property is an association, but with no associated issues, then do not display
        if (type == F_ASSOCIATION) {
            LOG_DEBUG("F_ASSOCIATION: %s, p->second.size()=%d", pname.c_str(), p->second.size());
            if (p == issue.properties.end()) continue; // this issue has no such property yet
            if (p->second.empty()) continue;
        }

        std::string label = pconfig.getLabelOfProperty(pname);

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }

        // if textarea type add specific class, for enabling line breaks in the CSS
        const char *pvalueStyle = "sm_issue_pvalue";
        const char *colspan = "";
        int workingColumnIncrement = 1;

        if (type == F_TEXTAREA || type == F_TEXTAREA2) pvalueStyle = "sm_issue_pvalue_ta";

        if (type == F_TEXTAREA2 || type == F_ASSOCIATION) {
            // the property spans over 4 columns (1 col for the label and 3 for the value)
            if (workingColumn == 1) {
                // nothing to do
            } else {
                // add a placeholder in order to align the property with next row
                // close current row and start a new row
                mg_printf(conn, "<td></td><td></td></tr><tr>\n");
            }
            colspan = "colspan=\"3\"";
            workingColumn = 1;
            workingColumnIncrement = 2;
        }

        // label
        mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n",
                  pname.c_str(), htmlEscape(label).c_str());

        // print the value
        if (type == F_ASSOCIATION) {
            std::list<std::string> associatedIssues;
            if (p != issue.properties.end()) associatedIssues = p->second;
            // split the value
            mg_printf(conn, "<td %s class=\"%s sm_issue_pvalue_%s\">",
                      colspan, pvalueStyle, pname.c_str());

            std::list<std::string>::iterator id;
            FOREACH(id, associatedIssues) {
                if (id != associatedIssues.begin()) mg_printf(conn, ", ");
                mg_printf(conn, "<a href=\"%s\">%s</a>", urlEncode(*id).c_str(),
                          htmlEscape(*id).c_str());
            }
           mg_printf(conn, "</td>\n");

        } else {
            std::string value;
            if (p != issue.properties.end()) value = toString(p->second);

            mg_printf(conn, "<td %s class=\"%s sm_issue_pvalue_%s\">%s</td>\n",
                      colspan, pvalueStyle, pname.c_str(), htmlEscape(value).c_str());
        }

        workingColumn += workingColumnIncrement;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }

    // reverse associated issues, if any
    std::map<std::string, std::set<std::string> > rAssociatedIssues = ctx.project->getReverseAssociations(issue.id);
    if (!rAssociatedIssues.empty()) {

        std::map<std::string, std::set<std::string> >::const_iterator ra;
        FOREACH(ra, rAssociatedIssues) {
            if (ra->second.empty()) continue;
            if (!ctx.projectConfig.isValidPropertyName(ra->first)) continue;

            mg_printf(conn, "<tr class=\"sm_issue_reverse_asso\">");
            std::string rlabel = ctx.projectConfig.getReverseLabelOfProperty(ra->first);
            mg_printf(conn, "<td class=\"sm_issue_plabel\">%s: </td>", htmlEscape(rlabel).c_str());
            std::set<std::string>::const_iterator otherIssue;
            mg_printf(conn, "<td colspan=\"3\">");
            FOREACH(otherIssue, ra->second) {
                if (otherIssue != ra->second.begin()) mg_printf(conn, ", ");
                mg_printf(conn, "<a href=\"%s\">%s</a>", urlEncode(*otherIssue).c_str(),
                          htmlEscape(*otherIssue).c_str());
            }
            mg_printf(conn, "</td>");
            mg_printf(conn, "</td></tr>");
        }

    }

    mg_printf(conn, "</table>\n");


    // tags of the entries of the issue
    if (!pconfig.tags.empty()) {
        mg_printf(conn, "<div class=\"sm_issue_tags\">\n");
        std::map<std::string, TagSpec>::const_iterator tspec;
        FOREACH(tspec, pconfig.tags) {
            if (tspec->second.display) {
                std::string style = "sm_issue_notag";
                int n = issue.getNumberOfTaggedIEntries(tspec->second.id);
                if (n > 0) {
                    // issue has at least one such tagged entry
                    style = "sm_issue_tagged sm_issue_tag_" + tspec->second.id;
                }
                mg_printf(conn, "<span id=\"sm_issue_tag_%s\" class=\"%s\" data-n=\"%d\">%s</span>\n",
                          tspec->second.id.c_str(), style.c_str(), n, htmlEscape(tspec->second.label).c_str());

            }
        }

        mg_printf(conn, "</div>\n");
    }


    // entries
    // -------------------------------------------------
    Entry *e = issue.latest;
    while (e && e->prev) e = e->prev; // go to the first one
    while (e) {
        Entry ee = *e;

        if (!e->next) {
            // latest entry. Add an anchor.
            mg_printf(conn, "<span id=\"sm_last_entry\"></span>");
        }

        // look if class sm_no_contents is applicable
        // an entry has no contents if no message and no file
        const char* classNoContents = "";
        if (ee.getMessage().empty()) {
            std::map<std::string, std::list<std::string> >::iterator files = ee.properties.find(K_FILE);
            if (files == ee.properties.end() || files->second.empty()) {
                classNoContents = "sm_entry_no_contents";
            }
        }

        // add tag-related styles, for the tags of the entry
        std::string classTagged = "sm_entry_notag";
        if (!ee.tags.empty()) {
            classTagged = "sm_entry_tagged";
            std::set<std::string>::iterator tag;
            FOREACH(tag, ee.tags) {
                // check that this tag is declared in project config
                if (pconfig.tags.find(*tag) != pconfig.tags.end()) {
                    classTagged += "sm_entry_tag_" + *tag + " ";
                }
            }
        }

        mg_printf(conn, "<div class=\"sm_entry %s %s\" id=\"%s\">\n", classNoContents,
                  classTagged.c_str(), ee.id.c_str());

        mg_printf(conn, "<div class=\"sm_entry_header\">\n");
        mg_printf(conn, "<span class=\"sm_entry_author\">%s</span>", htmlEscape(ee.author).c_str());
        mg_printf(conn, ", <span class=\"sm_entry_ctime\">%s</span>\n", epochToString(ee.ctime).c_str());
        // conversion of date in javascript
        // document.write(new Date(%d)).toString());

        // delete button
        time_t delta = time(0) - ee.ctime;
        if ( (delta < DELETE_DELAY_S) && (ee.author == ctx.user.username) &&
             (e == issue.latest) && e->prev &&
             (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) ) {
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

        // display the tags of the entry
        if (!pconfig.tags.empty()) {
            std::map<std::string, TagSpec>::const_iterator tagIt;
            FOREACH(tagIt, pconfig.tags) {
                TagSpec tag = tagIt->second;
                LOG_DEBUG("tag: id=%s, label=%s", tag.id.c_str(), tag.label.c_str());
                std::string tagStyle = "sm_entry_notag";
                if (ee.tags.find(tag.id) != ee.tags.end()) tagStyle = "sm_entry_tagged sm_entry_tag_" + tag.id;

                if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
                    const char *tagTitle = _("Click to tag/untag");

                    mg_printf(conn, "<a href=\"#\" onclick=\"tagEntry('/%s/tags/%s', '%s', '%s');return false;\""
                              " title=\"%s\" class=\"sm_entry_tag\">",
                              ctx.getProject().getUrlName().c_str(), issue.id.c_str(), ee.id.c_str(),
                              tag.id.c_str(), tagTitle);

                    // the tag itself
                    mg_printf(conn, "<span class=\"%s\" id=\"sm_tag_%s_%s\">",
                              tagStyle.c_str(), ee.id.c_str(), tag.id.c_str());
                    mg_printf(conn, "[%s]", tag.label.c_str());
                    mg_printf(conn, "</span>\n");

                    mg_printf(conn, "</a>\n");

                } else {
                    // read-only
                    // if tag is not active, do not display
                    if (ee.tags.find(tag.id) != ee.tags.end()) {
                        mg_printf(conn, "<span class=\"%s\">", tagStyle.c_str());
                        mg_printf(conn, "[%s]", tag.label.c_str());
                        mg_printf(conn, "</span>\n");
                    }

                }

            }

        }

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
                // remove the id: up to first dot .
                size_t dot = shortName.find_first_of('.');
                if (dot != std::string::npos) {
                    shortName = shortName.substr(dot+1);
                }

                mg_printf(conn, "<div class=\"sm_entry_file\">\n");
                mg_printf(conn, "<a href=\"../%s/%s\" class=\"sm_entry_file\">", K_UPLOADED_FILES_DIR,
                          urlEncode(*f).c_str());
                if (isImage(*f)) {
                    mg_printf(conn, "<img src=\"../files/%s\" class=\"sm_entry_file\"><br>", urlEncode(*f).c_str());
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

        // process summary first as it is not part of orderedFields
        std::map<std::string, std::list<std::string> >::const_iterator p;
        bool first = true;
        FOREACH(p, ee.properties) {
            if (p->first == K_MESSAGE) continue;

            if (!first) otherProperties << ", "; // separate properties by a comma
            first = false;

            std::string value = toString(p->second);
            otherProperties << "<span class=\"sm_entry_pname\">" << ctx.projectConfig.getLabelOfProperty(p->first)
                            << ": </span>";
            otherProperties << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";

        }

        if (otherProperties.str().size() > 0) {
            mg_printf(conn, "<div class=\"sm_entry_other_properties\">\n");
            mg_printf(conn, "%s", otherProperties.str().c_str());
            mg_printf(conn, "</div>\n");
        }

        mg_printf(conn, "</div>\n"); // end entry

        e = e->next;
    } // end of entries

    mg_printf(conn, "</div>\n");

}

void RHtml::printPageIssue(const ContextParameters &ctx, const Issue &issue)
{
    VariableNavigator vn("issue.html", ctx);
    vn.currentIssue = &issue;
    vn.printPage();

    // update the next/previous links
    struct mg_connection *conn = ctx.conn;
    // class name of the HTML nodes to be updated
    const char* SM_ISSUE_NEXT = "sm_issue_next";
    const char* SM_ISSUE_PREVIOUS = "sm_issue_previous";
    mg_printf(conn, "<script>");
    if (ctx.originView.empty()) {
        // disable the next/previous links
        mg_printf(conn, "updateHref('%s', null);\n", SM_ISSUE_NEXT);
        mg_printf(conn, "updateHref('%s', null);\n", SM_ISSUE_PREVIOUS);
    } else {
        std::string qs;
        qs = ctx.originView + "&" QS_GOTO_NEXT "=" + urlEncode(issue.id);
        mg_printf(conn, "updateHref('%s', './?%s');\n", SM_ISSUE_NEXT, enquoteJs(qs).c_str());
        qs = ctx.originView + "&" QS_GOTO_PREVIOUS "=" + urlEncode(issue.id);
        mg_printf(conn, "updateHref('%s', './?%s');\n", SM_ISSUE_PREVIOUS, enquoteJs(qs).c_str());
    }
    mg_printf(conn, "</script>");
}



void RHtml::printPageNewIssue(const ContextParameters &ctx)
{
    VariableNavigator vn("newIssue.html", ctx);
    vn.printPage();
}


/** print form for adding a message / modifying the issue
  */
void RHtml::printIssueForm(const ContextParameters &ctx, const Issue *issue, bool autofocus)
{
    if (!issue) {
        LOG_ERROR("printIssueForm: Invalid null issue");
        return;
    }
    if (ctx.userRole != ROLE_ADMIN && ctx.userRole != ROLE_RW) {
        return;
    }

    struct mg_connection *conn = ctx.conn;
    const ProjectConfig &pconfig = ctx.projectConfig;

    mg_printf(conn, "<form enctype=\"multipart/form-data\" method=\"post\"  class=\"sm_issue_form\">");
    // print the fields of the issue in a two-column table

    // The form is made over a table with 4 columns.
    // each row is made of 1 label, 1 input, 1 label, 1 input (4 columns)
    // except for the summary.
    // summary
    mg_printf(conn, "<table class=\"sm_issue_properties\">");
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_summary\">%s: </td>\n",
              pconfig.getLabelOfProperty("summary").c_str());
    mg_printf(conn, "<td class=\"sm_issue_pinput\" colspan=\"3\">");

    mg_printf(conn, "<input class=\"sm_issue_pinput_summary\" required=\"required\" type=\"text\" name=\"summary\" value=\"%s\"",
              htmlEscape(issue->getSummary()).c_str());
    if (autofocus) mg_printf(conn, " autofocus");
    mg_printf(conn, ">");
    mg_printf(conn, "</td>\n");
    mg_printf(conn, "</tr>\n");

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

            for (so = pspec->selectOptions.begin(); so != pspec->selectOptions.end(); so++) {
                input << "<option" ;
                if (value == *so) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (pspec->type == F_MULTISELECT) {
            std::list<std::string>::const_iterator so;
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\"";
            if (pspec->type == F_MULTISELECT) input << " multiple=\"multiple\"";
            input << ">";

            for (so = pspec->selectOptions.begin(); so != pspec->selectOptions.end(); so++) {
                input << "<option" ;
                if (inList(propertyValues, *so)) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (pspec->type == F_SELECT_USER) {
            if (propertyValues.size()>0) value = propertyValues.front();
            else {
                // by default, if no selection is made, select the current user
                value = ctx.user.username;
            }
            input << "<select class=\"sm_issue_pinput_" << pname << "\" name=\"" << pname << "\">";

            std::set<std::string> users = UserBase::getUsersOfProject(ctx.getProject().getName());
            std::set<std::string>::iterator u;
            for (u = users.begin(); u != users.end(); u++) {
                input << "<option" ;
                if (value == *u) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*u) << "</option>";
            }

            input << "</select>";

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
                mg_printf(conn, "<td></td><td></td></tr><tr>\n");
            }
            colspan = "colspan=\"3\"";
            workingColumn = 1;
            workingColumnIncrement = 2;

            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<textarea class=\"sm_ta2 sm_issue_pinput_" << pname << "\" name=\""
                  << pname << "\">" << htmlEscape(value) << "</textarea>\n";

        } else if (pspec->type == F_ASSOCIATION) {
            if (propertyValues.size()>0) value = join(propertyValues, ", ");
            input << "<input class=\"sm_pinput_" << pname << "\" type=\"text\" name=\""
                  << pname << "\" value=\"" << htmlEscape(value) << "\">\n";

        } else {
            LOG_ERROR("invalid fieldSpec->type=%d", pspec->type);
            continue;
        }

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }

        // label
        mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_%s\">%s: </td>\n",
                  pname.c_str(), label.c_str());

        // input
        mg_printf(conn, "<td %s class=\"sm_issue_pinput\">%s</td>\n", colspan, input.str().c_str());

        workingColumn += workingColumnIncrement;
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
    mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_message\" >%s: </td>\n",  htmlEscape(_("Message")).c_str());
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
    mg_printf(conn, "<td class=\"sm_issue_plabel sm_issue_plabel_file\" >%s: </td>\n", htmlEscape(_("File Upload")).c_str());
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<input type=\"file\" name=\"%s\" class=\"sm_issue_input_file\" onchange=\"updateFileInput('sm_issue_input_file');\">\n", K_FILE);
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<button onclick=\"previewMessage(); return false;\">%s</button>\n", htmlEscape(_("Preview")).c_str());
    mg_printf(conn, "<input type=\"submit\" value=\"%s\">\n", htmlEscape(_("Post")).c_str());
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "</table>\n");

    mg_printf(conn, "</form>");

}
