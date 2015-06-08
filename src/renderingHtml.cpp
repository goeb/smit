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

#include "renderingHtml.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "stringTools.h"
#include "dateTools.h"
#include "session.h"
#include "global.h"
#include "filesystem.h"
#include "restApi.h"


/** Build a context for a user and project
  *
  * ContextParameters::projectConfig should be user rather than ContextParameters::project.getConfig()
  * as getConfig locks a mutex.
  * ContextParameters::projectConfig gets the config once at initilisation,
  * and afterwards one can work with the copy (without locking).
  */
ContextParameters::ContextParameters(const RequestContext *request, const User &u, const Project &p)
{
    init(request, u);
    project = &p;
    projectConfig = p.getConfig(); // take a copy of the config
    predefinedViews = p.getViews(); // take a copy of the config
    userRole = u.getRole(p.getName());
}

ContextParameters::ContextParameters(const RequestContext *request, const User &u)
{
    init(request, u);
}

void ContextParameters::init(const RequestContext *request, const User &u)
{
    project = 0;
    user = u;
    req = request;
    originView = 0;
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
#define K_SM_HTML_PROJECT "SM_HTML_PROJECT"
#define K_SM_URL_PROJECT "SM_URL_PROJECT"
#define K_SM_RAW_ISSUE_ID "SM_RAW_ISSUE_ID"
#define K_SM_SCRIPT_PROJECT_CONFIG_UPDATE "SM_SCRIPT_PROJECT_CONFIG_UPDATE"
#define K_SM_DIV_PREDEFINED_VIEWS "SM_DIV_PREDEFINED_VIEWS"
#define K_SM_DIV_PROJECTS "SM_DIV_PROJECTS"
#define K_SM_DIV_USERS "SM_DIV_USERS"
#define K_SM_DIV_ISSUES "SM_DIV_ISSUES"
#define K_SM_DIV_ISSUE "SM_DIV_ISSUE"
#define K_SM_HTML_ISSUE_SUMMARY "SM_HTML_ISSUE_SUMMARY"
#define K_SM_DIV_ISSUE_FORM "SM_DIV_ISSUE_FORM"
#define K_SM_DIV_ISSUE_MSG_PREVIEW "SM_DIV_ISSUE_MSG_PREVIEW"
#define K_SM_URL_ROOT "SM_URL_ROOT"
#define K_SM_TABLE_USER_PERMISSIONS "SM_TABLE_USER_PERMISSIONS"

/** Load a page template of a specific project
  *
  * By default pages (typically HTML pages) are loaded from $REPO/public/ directory.
  * But the administrator may override this by pages located in $REPO/$PROJECT/html/ directory.
  *
  * The caller is responsible for calling 'free' on the returned pointer (if not null).
  */
int loadProjectPage(const RequestContext *req, const Project *project, const std::string &page, const char **data)
{
    std::string path;
    if (project) {
        // first look in the templates of the project
        path = project->getPath() + "/" PATH_TEMPLATES "/" + page;
        int n = loadFile(path.c_str(), data);
        if (n > 0) return n;
    }

    // secondly, look in the templates of the repository
    path = Database::Db.getRootDir() + "/" PATH_REPO_TEMPLATES "/" + page;
    int n = loadFile(path.c_str(), data);

    if (n > 0) return n;

    // no page found. This is an error
    LOG_ERROR("Page not found (or empty): %s", page.c_str());
    req->printf("Missing page (or empty): %s", htmlEscape(page).c_str());
    return n;
}

/** class in charge of replacing the SM_ variables in the HTML pages
  *
  */
class VariableNavigator {
public:
    std::vector<Issue> *issueList;
    std::vector<Issue> *issuesOfAllProjects;
    std::vector<Issue> *issueListFullContents;
    std::list<std::string> *colspec;
    const ContextParameters &ctx;
    const std::list<std::pair<std::string, std::string> > *projectList;
    const std::list<User> *usersList;
    const Issue *currentIssue;
    const User *concernedUser;
    const Entry *entryToBeAmended;
    const std::map<std::string, std::map<Role, std::set<std::string> > > *userRolesByProject;

    VariableNavigator(const std::string basename, const ContextParameters &context) : ctx(context) {
        buffer = 0;
        issueList = 0;
        issuesOfAllProjects = 0;
        issueListFullContents = 0;
        colspec = 0;
        projectList = 0;
        usersList = 0;
        currentIssue = 0;
        userRolesByProject = 0;
        concernedUser = 0;
        entryToBeAmended = 0;

        int n = loadProjectPage(ctx.req, ctx.project, basename, &buffer);

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

    void dumpPrevious(const RequestContext *req) {
        if (dumpEnd == dumpStart) {
            LOG_ERROR("dumpPrevious: dumpEnd == dumpStart");
            return;
        }
        req->write(dumpStart, dumpEnd-dumpStart);
        dumpStart = searchFromHere;
        dumpEnd = dumpStart;
    }

    void printPage() {
        if (!buffer) return;
        ctx.req->printf("Content-Type: text/html\r\n\r\n");

        while (1) {
            std::string varname = getNextVariable();
            dumpPrevious(ctx.req);
            if (varname.empty()) break;

            if (varname == K_SM_HTML_PROJECT && ctx.project) {
                if (ctx.getProject().getName().empty()) ctx.req->printf("(new)");
                else ctx.req->printf("%s", htmlEscape(ctx.getProject().getName()).c_str());

            } else if (varname == K_SM_URL_PROJECT && ctx.project) {
                MongooseServerContext &mc = MongooseServerContext::getInstance();
                ctx.req->printf("%s/%s", mc.getUrlRewritingRoot().c_str(),
                                ctx.getProject().getUrlName().c_str());

            } else if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                RHtml::printNavigationGlobal(ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES && ctx.project) {
                // do not print this in case a a new project
                if (! ctx.getProject().getName().empty()) RHtml::printNavigationIssues(ctx, false);

            } else if (varname == K_SM_DIV_PROJECTS && projectList) {
                RHtml::printProjects(ctx, *projectList, userRolesByProject);

            } else if (varname == K_SM_DIV_USERS && usersList) {
                RHtml::printUsers(ctx.req, *usersList);

            } else if (varname == K_SM_TABLE_USER_PERMISSIONS) {
                if (concernedUser) RHtml::printUserPermissions(ctx.req, *concernedUser);
                // else, dont print the table

            } else if (varname == K_SM_SCRIPT_PROJECT_CONFIG_UPDATE) {
                RHtml::printScriptUpdateConfig(ctx);

            } else if (varname == K_SM_RAW_ISSUE_ID && currentIssue) {
                ctx.req->printf("%s", htmlEscape(currentIssue->id).c_str());

            } else if (varname == K_SM_HTML_ISSUE_SUMMARY && currentIssue) {
                ctx.req->printf("%s", htmlEscape(currentIssue->getSummary()).c_str());

            } else if (varname == K_SM_DIV_ISSUES && issueList && colspec) {
                RHtml::printIssueList(ctx, *issueList, *colspec, true);

            } else if (varname == K_SM_DIV_ISSUES && issuesOfAllProjects && colspec) {
                RHtml::printIssuesAccrossProjects(ctx, *issuesOfAllProjects, *colspec);

            } else if (varname == K_SM_DIV_ISSUES && issueListFullContents) {
                RHtml::printIssueListFullContents(ctx, *issueListFullContents);

            } else if (varname == K_SM_DIV_ISSUE && currentIssue) {
                std::string eAmended;
                if (entryToBeAmended) eAmended = entryToBeAmended->id;
                RHtml::printIssue(ctx, *currentIssue, eAmended);

            } else if (varname == K_SM_DIV_ISSUE_FORM) {
                Issue issue;
                if (!currentIssue) currentIssue = &issue; // set an empty issue

                if (entryToBeAmended) RHtml::printEditMessage(ctx, currentIssue, *entryToBeAmended);
                else RHtml::printIssueForm(ctx, currentIssue, false);

            } else if (varname == K_SM_DIV_PREDEFINED_VIEWS) {
                RHtml::printLinksToPredefinedViews(ctx);

            } else if (varname == K_SM_URL_ROOT) {
                // url-rewriting
                MongooseServerContext &mc = MongooseServerContext::getInstance();
                ctx.req->printf("%s", mc.getUrlRewritingRoot().c_str());

            } else if (varname == K_SM_DIV_ISSUE_MSG_PREVIEW) {
                ctx.req->printf("<div id=\"sm_entry_preview\" class=\"sm_entry_message\">"
                                "%s"
                                "</div>", htmlEscape(_("...message preview...")).c_str());

            } else {
                // unknown variable name
                ctx.req->printf("%s", varname.c_str());
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
void RHtml::printPageSignin(const ContextParameters &ctx, const char *redirect)
{
    VariableNavigator vn("signin.html", ctx);
    vn.printPage();

    // add javascript for updating the redirect URL
    ctx.req->printf("<script>document.getElementById(\"redirect\").value = \"%s\"</script>",
                enquoteJs(redirect).c_str());
}


void RHtml::printPageUser(const ContextParameters &ctx, const User *u)
{
    VariableNavigator vn("user.html", ctx);
    vn.concernedUser = u;
    vn.printPage();

    // add javascript for updating the inputs
    ctx.req->printf("<script>\n");

    if (!ctx.user.superadmin) {
        // hide what is reserved to superadmin
        ctx.req->printf("hideSuperadminZone();\n");
    } else {
        if (u) ctx.req->printf("setName('%s');\n", enquoteJs(u->username).c_str());
    }

    if (u && u->superadmin) {
        ctx.req->printf("setSuperadminCheckbox();\n");
    }

    std::list<std::string> projects = Database::getProjects();
    ctx.req->printf("Projects = %s;\n", toJavascriptArray(projects).c_str());
    std::list<std::string> roleList = getAvailableRoles();
    ctx.req->printf("Roles = %s;\n", toJavascriptArray(roleList).c_str());

    ctx.req->printf("addProjectDatalist('roles_on_projects');\n");

    if (u) {
        std::map<std::string, enum Role>::const_iterator rop;
        FOREACH(rop, u->permissions) {
            ctx.req->printf("addPermission('roles_on_projects', '%s', '%s');\n",
                            enquoteJs(rop->first).c_str(),
                            enquoteJs(roleToString(rop->second)).c_str());
        }
    }
    ctx.req->printf("addPermission('roles_on_projects', '', '');\n");
    ctx.req->printf("addPermission('roles_on_projects', '', '');\n");

    ctx.req->printf("</script>\n");
}

void RHtml::printPageView(const ContextParameters &ctx, const PredefinedView &pv)
{
    VariableNavigator vn("view.html", ctx);
    vn.printPage();


    // add javascript for updating the inputs
    ctx.req->printf("<script>\n");

    if (ctx.userRole != ROLE_ADMIN && !ctx.user.superadmin) {
        // hide what is reserved to admin
        ctx.req->printf("hideAdminZone();\n");
    } else {
        ctx.req->printf("setName('%s');\n", enquoteJs(pv.name).c_str());
    }
    if (pv.isDefault) ctx.req->printf("setDefaultCheckbox();\n");
    std::list<std::string> properties = ctx.projectConfig.getPropertiesNames();
    ctx.req->printf("Properties = %s;\n", toJavascriptArray(properties).c_str());

    // add datalists, for proposing the values in filterin/out
    // datalists are name 'datalist_' + <property-name>
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, ctx.projectConfig.properties) {
        PropertyType t = pspec->type;
        std::string propname = pspec->name;
        std::list<std::string> proposedValues;
        if (t == F_SELECT || t == F_MULTISELECT) {
            proposedValues = pspec->selectOptions;

        } else if (t == F_SELECT_USER) {
            std::set<std::string>::const_iterator u;
            std::set<std::string> users = UserBase::getUsersOfProject(ctx.getProject().getName());
            // fulfill the list of proposed values
            proposedValues.push_back("me");
            FOREACH(u, users) { proposedValues.push_back(*u); }
        }

        if (!propname.empty()) {
            ctx.req->printf("addFilterDatalist('filterin', 'datalist_%s', %s);\n",
                            enquoteJs(propname).c_str(),
                            toJavascriptArray(proposedValues).c_str());
        }
    }

    ctx.req->printf("setSearch('%s');\n", enquoteJs(pv.search).c_str());
    ctx.req->printf("setUrl('%s/%s/issues/?%s');\n",
                    MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                    ctx.getProject().getUrlName().c_str(),
                    pv.generateQueryString().c_str());

    // add datalists for all types select, multiselect and selectuser


    // filter in and out
    std::map<std::string, std::list<std::string> >::const_iterator f;
    std::list<std::string>::const_iterator v;
    FOREACH(f, pv.filterin) {
        FOREACH(v, f->second) {
            ctx.req->printf("addFilter('filterin', '%s', '%s');\n",
                            enquoteJs(f->first).c_str(),
                            enquoteJs(*v).c_str());
        }
    }
    ctx.req->printf("addFilter('filterin', '', '');\n");

    FOREACH(f, pv.filterout) {
        FOREACH(v, f->second) {
            ctx.req->printf("addFilter('filterout', '%s', '%s');\n",
                            enquoteJs(f->first).c_str(),
                            enquoteJs(*v).c_str());
        }
    }
    ctx.req->printf("addFilter('filterout', '', '');\n");

    // Colums specification
    if (!pv.colspec.empty()) {
        std::list<std::string> items = split(pv.colspec, " +");
        std::list<std::string>::iterator i;
        FOREACH(i, items) {
            ctx.req->printf("addColspec('%s');\n", enquoteJs(*i).c_str());
        }
    }
    ctx.req->printf("addColspec('');\n");

    // sort
    std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(pv.sort.c_str());
    std::list<std::pair<bool, std::string> >::iterator s;
    FOREACH(s, sSpec) {
        std::string direction = PredefinedView::getDirectionName(s->first);
        ctx.req->printf("addSort('%s', '%s');\n", enquoteJs(direction).c_str(),
                        enquoteJs(s->second).c_str());
    }
    ctx.req->printf("addSort('', '');\n");


    ctx.req->printf("</script>\n");
}

void RHtml::printLinksToPredefinedViews(const ContextParameters &ctx)
{
    std::map<std::string, PredefinedView>::const_iterator pv;
    ctx.req->printf("<table class=\"sm_views\">");
    ctx.req->printf("<tr><th>%s</th><th>%s</th></tr>\n", _("Name"), _("Associated Url"));
    FOREACH(pv, ctx.predefinedViews) {
        ctx.req->printf("<tr><td class=\"sm_views_name\">");
        ctx.req->printf("<a href=\"%s\">%s</a>", urlEncode(pv->first).c_str(), htmlEscape(pv->first).c_str());
        ctx.req->printf("</td><td class=\"sm_views_link\">");
        std::string qs = pv->second.generateQueryString();
        ctx.req->printf("<a href=\"../issues/?%s\">%s</a>", qs.c_str(), htmlEscape(qs).c_str());
        ctx.req->printf("</td></tr>\n");
    }
    ctx.req->printf("<table>\n");
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
            std::string val = value;
            std::string sname = name;
            // do url rewriting if needed
            if ( ( (sname == "href") || (sname == "src") || (sname == "action") )
                 && value[0] == '/') {
                val = MongooseServerContext::getInstance().getUrlRewritingRoot() + val;
            }

            attributes[name] = val;
        }
    }

    void print(const RequestContext *req) {
        if (nodeName.empty()) {
            // text contents
            req->printf("%s", text.c_str());
        } else {
            req->printf("<%s ", nodeName.c_str());
            std::map<std::string, std::string>::iterator i;
            FOREACH(i, attributes) {
                req->printf("%s=\"%s\" ", i->first.c_str(), i->second.c_str());
            }
            req->printf(">\n");

            if (nodeName == "input") return; // no closing tag nor any contents

            std::list<HtmlNode>::iterator c;
            FOREACH(c, contents) {
                c->print(req);
            }
            // close HTML node
            req->printf("</%s>\n", nodeName.c_str());
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

/** Print global navigation bar
  *
  * - link to all projects page
  * - link to modify project (if admin)
  * - signed-in user indication and link to signout
  */
void RHtml::printNavigationGlobal(const ContextParameters &ctx)
{
    HtmlNode div("div");
    div.addAttribute("class", "sm_navigation_global");
    HtmlNode linkToProjects("a");
    linkToProjects.addAttribute("class", "sm_link_projects");
    linkToProjects.addAttribute("href", "/");
    linkToProjects.addContents("%s", _("Projects"));
    div.addContents(linkToProjects);

    if (ctx.user.superadmin) {
        // link to all users
        HtmlNode allUsers("a");
        allUsers.addAttribute("class", "sm_link_users");
        allUsers.addAttribute("href", "/users");
        allUsers.addContents("%s", _("Users"));
        div.addContents(" ");
        div.addContents(allUsers);
    }

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
    username.addContents("%s", ctx.user.username.c_str());
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

    div.print(ctx.req);

}


/** Print links for navigating through issues;
  * - "create new issue"
  * - predefined views
  * - quick search form
  */
void RHtml::printNavigationIssues(const ContextParameters &ctx, bool autofocus)
{
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
    FOREACH (pv, ctx.predefinedViews) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/?%s", ctx.getProject().getUrlName().c_str(),
                       pv->second.generateQueryString().c_str());
        a.addAttribute("class", "sm_predefined_view");
        a.addContents("%s", pv->first.c_str());
        div.addContents(a);
    }

    HtmlNode form("form");
    form.addAttribute("class", "sm_searchbox");
    form.addAttribute("action", "/%s/issues/", ctx.getProject().getUrlName().c_str());
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
    a.addAttribute("href", "/%s/views/_", ctx.getProject().getUrlName().c_str());
    a.addAttribute("class", "sm_advanced_search");
    a.addContents(_("Advanced Search"));
    div.addContents(a);


    div.print(ctx.req);
}


void RHtml::printProjects(const ContextParameters &ctx,
                          const std::list<std::pair<std::string, std::string> > &pList,
                          const std::map<std::string, std::map<Role, std::set<std::string> > > *userRolesByProject)
{
    std::list<std::pair<std::string, std::string> >::const_iterator p;
    ctx.req->printf("<table class=\"sm_projects\">\n");
    ctx.req->printf("<tr>"
                    "<th>%s</th>"
                    "<th>%s</th>", _("Projects"), _("My Role"));
    // put a column for each role
    std::list<Role> roleColumns;
    roleColumns.push_back(ROLE_ADMIN);
    roleColumns.push_back(ROLE_RW);
    roleColumns.push_back(ROLE_RO);
    roleColumns.push_back(ROLE_REFERENCED);
    std::list<Role>::iterator rc;
    FOREACH(rc, roleColumns) {
        ctx.req->printf("<th>%s '%s'</th>", _("Users"), htmlEscape(roleToString(*rc)).c_str());
    }

    ctx.req->printf("</tr>\n"); // end header row

    for (p=pList.begin(); p!=pList.end(); p++) { // put a row for each project
        std::string pname = p->first.c_str();
        ctx.req->printf("<tr>\n");

        ctx.req->printf("<td class=\"sm_projects_link\">");
        ctx.req->printf("<a href=\"%s/%s/issues/?defaultView=1\">%s</a></td>\n",
                        MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                        Project::urlNameEncode(pname).c_str(), htmlEscape(pname).c_str());
        // my role
        ctx.req->printf("<td>%s</td>\n", htmlEscape(_(p->second.c_str())).c_str());

        std::map<std::string, std::map<Role, std::set<std::string> > >::const_iterator urit;
        urit = userRolesByProject->find(pname);
        if (urit != userRolesByProject->end()) {

            std::list<Role>::iterator r;
            FOREACH(r, roleColumns) {
                ctx.req->printf("<td>");
                std::map<Role, std::set<std::string> >::const_iterator urole;
                urole = urit->second.find(*r);
                if (urole != urit->second.end()) {
                    std::set<std::string>::iterator user;
                    FOREACH(user, urole->second) {
                        if (user != urole->second.begin()) ctx.req->printf(", ");
                        ctx.req->printf("<span class=\"sm_projects_stakeholder\">%s</span>", htmlEscape(*user).c_str());
                    }
                    ctx.req->printf("</td>");
                }
            }
        }
        ctx.req->printf("</tr>\n"); // end row for this project
    }
    ctx.req->printf("</table><br>\n");
    if (ctx.user.superadmin) ctx.req->printf("<div class=\"sm_projects_new\">"
                                             "<a href=\"_\" class=\"sm_projects_new\">%s</a></div><br>",
                                             htmlEscape(_("New Project")).c_str());
}

void RHtml::printUsers(const RequestContext *req, const std::list<User> &usersList)
{
    std::list<User>::const_iterator u;

    if (usersList.empty()) return;

    req->printf("<table class=\"sm_users\">\n");
    req->printf("<tr class=\"sm_users\">");
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Users"));
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Capabilities"));
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Authentication"));
    req->printf("</tr>");

    FOREACH(u, usersList) {
        req->printf("<tr class=\"sm_users\">");
        req->printf("<td class=\"sm_users\">\n");
        req->printf("<a href=\"%s/users/%s\">%s<a><br>",
                    MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                    urlEncode(u->username).c_str(), htmlEscape(u->username).c_str());
        req->printf("</td>");
        // capability
        if (u->superadmin) req->printf("<td class=\"sm_users\">%s</td>\n", _("superadmin"));
        else req->printf("<td class=\"sm_users\"> </td>\n");

        // print authentication parameters
        req->printf("<td class=\"sm_users\">\n");
        if (u->authHandler) {
            req->printf("%s", htmlEscape(u->authHandler->type).c_str());
        } else {
            req->printf("none");
        }
        req->printf("</td>\n");

        req->printf("</tr>\n");

    }
    req->printf("</table><br>\n");
    req->printf("<div class=\"sm_users_new\">"
                "<a href=\"%s/users/_\" class=\"sm_users_new\">%s</a></div><br>",
                MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                htmlEscape(_("New User")).c_str());

}

void RHtml::printUserPermissions(const RequestContext *req, const User &u)
{
    req->printf("<table class=\"sm_users\">\n");
    req->printf("<tr><th>%s</th><th>%s</th></tr>\n", _("Project"), _("Role"));
    std::map<std::string, enum Role>::const_iterator rop;
    FOREACH(rop, u.rolesOnProjects) {
        req->printf("<tr><td>%s</td><td>%s</td></tr>\n", htmlEscape(rop->first).c_str(),
                    roleToString(rop->second).c_str());
    }

    req->printf("</table>\n");
}

void RHtml::printPageUserList(const ContextParameters &ctx, const std::list<User> &users)
{
    VariableNavigator vn("users.html", ctx);
    vn.usersList = &users;
    vn.printPage();
}

void RHtml::printPageProjectList(const ContextParameters &ctx,
                                 const std::list<std::pair<std::string, std::string> > &pList,
                                 const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject)
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
std::string getNewSortingSpec(const RequestContext *req, const std::string property, bool exclusive)
{
    std::string qs = req->getQueryString();
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
    ctx.req->printf("<script>\n");

    // fulfill reserved properties first
    std::list<std::string> reserved = ProjectConfig::getReservedProperties();
    std::list<std::string>::iterator r;
    FOREACH(r, reserved) {
        std::string label = ctx.projectConfig.getLabelOfProperty(*r);
        ctx.req->printf("addProperty('%s', '%s', 'reserved', '');\n", enquoteJs(*r).c_str(),
                        enquoteJs(label).c_str());
    }

    // other properties
    const ProjectConfig &c = ctx.projectConfig;
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, c.properties) {
        std::string type = propertyTypeToStr(pspec->type);

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
        ctx.req->printf("addProperty('%s', '%s', '%s', '%s');\n", enquoteJs(pspec->name).c_str(),
                        enquoteJs(label).c_str(),
                        type.c_str(), options.c_str());
    }

    // add 3 more empty properties
    ctx.req->printf("addProperty('', '', '', '');\n");
    ctx.req->printf("addProperty('', '', '', '');\n");
    ctx.req->printf("addProperty('', '', '', '');\n");

    ctx.req->printf("replaceContentInContainer();\n");

    // add tags
    std::map<std::string, TagSpec>::const_iterator tagspec;
    FOREACH(tagspec, c.tags) {
        const TagSpec& tpsec = tagspec->second;
        ctx.req->printf("addTag('%s', '%s', %s);\n", enquoteJs(tpsec.id).c_str(),
                        enquoteJs(tpsec.label).c_str(),
                        tpsec.display?"true":"false");
    }

    ctx.req->printf("addTag('', '', '', '');\n");
    ctx.req->printf("addTag('', '', '', '');\n");


    ctx.req->printf("</script>\n");
}


void RHtml::printProjectConfig(const ContextParameters &ctx)
{
    VariableNavigator vn("project.html", ctx);
    vn.printPage();

    ctx.req->printf("<script>");
    if (!ctx.user.superadmin) {
        // hide what is reserved to superadmin
        ctx.req->printf("hideSuperadminZone();\n");
    } else {
        if (ctx.project) ctx.req->printf("setProjectName('%s');\n", enquoteJs(ctx.getProject().getName()).c_str());
    }
    ctx.req->printf("</script>");

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
    if (property == "p") return property; // enable grouping by project name

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
    if (!ctx.search.empty() || !ctx.filterin.empty() || !ctx.filterout.empty()) {
        ctx.req->printf("<div class=\"sm_issues_filters\">");
        if (!ctx.search.empty()) ctx.req->printf("search: %s<br>", htmlEscape(ctx.search).c_str());
        if (!ctx.filterin.empty()) ctx.req->printf("filterin: %s<br>", htmlEscape(toString(ctx.filterin)).c_str());
        if (!ctx.filterout.empty()) ctx.req->printf("filterout: %s<br>", htmlEscape(toString(ctx.filterout)).c_str());
        if (!ctx.sort.empty())  ctx.req->printf("sort: %s<br>", htmlEscape(ctx.sort).c_str());
        ctx.req->printf("</div>");
    }
}

void RHtml::printIssueListFullContents(const ContextParameters &ctx, std::vector<Issue> &issueList)
{
    ctx.req->printf("<div class=\"sm_issues\">\n");

    printFilters(ctx);
    // number of issues
    ctx.req->printf("<div class=\"sm_issues_count\">%s: <span class=\"sm_issues_count\">%lu</span></div>\n",
                    _("Issues found"), L(issueList.size()));


    std::vector<Issue>::const_iterator i;
    if (!ctx.project) {
        LOG_ERROR("Null project");
        return;
    }

    FOREACH (i, issueList) {
        Issue issue;
        int r = ctx.getProject().get(i->id, issue);
        if (r < 0) {
            // issue not found (or other error)
            LOG_INFO("Issue disappeared: %s", i->id.c_str());

        } else {

            // deactivate user role
            ContextParameters ctxCopy = ctx;
            ctxCopy.userRole = ROLE_RO;
            printIssueSummary(ctxCopy, issue);
            printIssue(ctxCopy, issue, "");

        }
    }
    ctx.req->printf("</div>\n");

}

/** concatenate a param to the query string (add ? or &)
  */
std::string queryStringAdd(const RequestContext *req, const char *param)
{

    std::string qs = req->getQueryString();

    std::string url = "?";
    if (qs.size() > 0) url = url + qs + '&' + param;
    else url += param;

    return url;
}

void RHtml::printIssueList(const ContextParameters &ctx, const std::vector<Issue> &issueList,
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

    std::string group = getPropertyForGrouping(ctx.projectConfig, ctx.sort);
    std::string currentGroup;

    ctx.req->printf("<table class=\"sm_issues\">\n");

    // print header of the table
    ctx.req->printf("<tr class=\"sm_issues\">\n");
    std::list<std::string>::const_iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {

        std::string label = ctx.projectConfig.getLabelOfProperty(*colname);
        std::string newQueryString = getNewSortingSpec(ctx.req, *colname, true);
        ctx.req->printf("<th class=\"sm_issues\"><a class=\"sm_issues_sort\" href=\"?%s\" title=\"Sort ascending\">%s</a>\n",
                        newQueryString.c_str(), htmlEscape(label).c_str());
        newQueryString = getNewSortingSpec(ctx.req, *colname, false);
        ctx.req->printf("\n<br><a href=\"?%s\" class=\"sm_issues_sort_cumulative\" ", newQueryString.c_str());
        ctx.req->printf("title=\"%s\">&gt;&gt;&gt;</a></th>\n",
                        _("Sort while preserving order of other columns\n(or invert current column if already sorted-by)"));

    }
    ctx.req->printf("</tr>\n");

    // print the rows of the issues
    std::vector<Issue>::const_iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        if (! group.empty() &&
                (i == issueList.begin() || i->getProperty(group) != currentGroup) ) {
            // insert group bar if relevant
            ctx.req->printf("<tr class=\"sm_issues_group\">\n");
            currentGroup = i->getProperty(group);
            ctx.req->printf("<td class=\"sm_group\" colspan=\"%lu\"><span class=\"sm_issues_group_label\">%s: </span>",
                            L(colspec.size()), htmlEscape(ctx.projectConfig.getLabelOfProperty(group)).c_str());
            ctx.req->printf("<span class=\"sm_issues_group\">%s</span></td>\n", htmlEscape(currentGroup).c_str());
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

void RHtml::printIssuesAccrossProjects(ContextParameters ctx,
                                       const std::vector<Issue> &issues,
                                       const std::list<std::string> &colspec)
{
    printIssueList(ctx, issues, colspec, false);
}


/** Print HTML page with the given issues and their full contents
  *
  */
void RHtml::printPageIssuesFullContents(const ContextParameters &ctx, std::vector<Issue> &issueList)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueListFullContents = &issueList;
    vn.printPage();
}

void RHtml::printPageIssueList(const ContextParameters &ctx,
                               std::vector<Issue> &issueList, std::list<std::string> colspec)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueList = &issueList;
    vn.colspec = &colspec;
    vn.printPage();
}
void RHtml::printPageIssueAccrossProjects(const ContextParameters &ctx,
                                          std::vector<Issue> &issues,
                                          std::list<std::string> colspec)
{
    VariableNavigator vn("issuesAccross.html", ctx);
    vn.issuesOfAllProjects = &issues;
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
void printAssociations(const ContextParameters &ctx, const std::string &associationName, const std::list<std::string> &issues, bool reverse)
{
    std::string label;
    if (reverse) {
        label = ctx.projectConfig.getReverseLabelOfProperty(associationName);
    } else {
        label = ctx.projectConfig.getLabelOfProperty(associationName);
    }

    ctx.req->printf("<td class=\"sm_issue_plabel\">%s: </td>", htmlEscape(label).c_str());
    std::list<std::string>::const_iterator otherIssue;
    ctx.req->printf("<td colspan=\"3\" class=\"sm_issue_asso\">");
    FOREACH(otherIssue, issues) {
        // separate by a line feed
        if (otherIssue != issues.begin()) ctx.req->printf("<br>\n");

        Issue associatedIssue;
        int r = ctx.getProject().get(*otherIssue, associatedIssue);
        if (r == 0) { // issue found. print id and summary
            ctx.req->printf("<a href=\"%s\"><span class=\"sm_issue_asso_id\">%s</span>"
                            " <span class=\"sm_issue_asso_summary\">%s</span></a>", urlEncode(associatedIssue.id).c_str(),
                            htmlEscape(associatedIssue.id).c_str(), htmlEscape(associatedIssue.getSummary()).c_str());

        } else { // not found. no such issue
            ctx.req->printf("<span class=\"sm_not_found\">%s</span>\n", htmlEscape(*otherIssue).c_str());
        }
    }
    ctx.req->printf("</td>");
}


void RHtml::printIssue(const ContextParameters &ctx, const Issue &issue, const std::string &entryToBeAmended)
{
    ctx.req->printf("<div class=\"sm_issue\">");

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

            printAssociations(ctx, pname, associatedIssues, false);

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
    std::map<std::string, std::set<std::string> > rAssociatedIssues = ctx.project->getReverseAssociations(issue.id);
    if (!rAssociatedIssues.empty()) {
        std::map<std::string, std::set<std::string> >::const_iterator ra;
        FOREACH(ra, rAssociatedIssues) {
            if (ra->second.empty()) continue;
            if (!ctx.projectConfig.isValidPropertyName(ra->first)) continue;

            std::list<std::string> issues(ra->second.begin(), ra->second.end()); // convert to list
            ctx.req->printf("<tr class=\"sm_issue_asso\">");

            printAssociations(ctx, ra->first, issues, true);
            ctx.req->printf("</tr>");
        }
    }

    ctx.req->printf("</table>\n");


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


    // entries
    // -------------------------------------------------
    Entry *e = issue.first;
    while (e) {
        Entry ee = *e;

        const char *styleBeingAmended = "";
        if (ee.id == entryToBeAmended) {
            // the page will display a form for editing this entry.
            // we want here a special display to help the user understand the link
            styleBeingAmended = "sm_entry_being_amended";
        }

        // look if class sm_no_contents is applicable
        // an entry has no contents if no message and no file
        const char* classNoContents = "";
        if (ee.getMessage().empty() || ee.isAmending()) {
            std::map<std::string, std::list<std::string> >::iterator files = ee.properties.find(K_FILE);
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
                            ctx.getProject().getUrlName().c_str(), enquoteJs(issue.id).c_str(),
                            enquoteJs(ee.id).c_str(), (DELETE_DELAY_S/60));
            ctx.req->printf("&#9998; %s", _("edit"));
            ctx.req->printf("</a>\n");
        }

        // link to raw entry
        ctx.req->printf("(<a href=\"%s/%s/" RESOURCE_FILES "/%s\" class=\"sm_entry_raw\">%s</a>",
                        MongooseServerContext::getInstance().getUrlRewritingRoot().c_str(),
                        ctx.getProject().getUrlName().c_str(),
                        urlEncode(ee.id).c_str(), _("raw"));
        // link to possible amendments
        int i = 1;
        std::map<std::string, std::list<std::string> >::const_iterator as = issue.amendments.find(ee.id);
        if (as != issue.amendments.end()) {
            std::list<std::string>::const_iterator a;
            FOREACH(a, as->second) {
                ctx.req->printf(", <a href=\"/%s/" RESOURCE_FILES "/%s\" class=\"sm_entry_raw\">%s%d</a>",
                                ctx.getProject().getUrlName().c_str(),
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
                                    ctx.getProject().getUrlName().c_str(), enquoteJs(ee.id).c_str(),
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
        std::map<std::string, std::list<std::string> >::iterator files = ee.properties.find(K_FILE);
        if (files != ee.properties.end() && files->second.size() > 0) {
            ctx.req->printf("<div class=\"sm_entry_files\">\n");
            std::list<std::string>::iterator f;
            FOREACH(f, files->second) {
                std::string objectId = popToken(*f, '/');
                std::string basename = *f;

                std::string href = RESOURCE_FILES "/" + urlEncode(objectId) + "/" + urlEncode(basename);
                ctx.req->printf("<div class=\"sm_entry_file\">\n");
                ctx.req->printf("<a href=\"../%s\" class=\"sm_entry_file\">", href.c_str());
                if (isImage(*f)) {
                    // do not escape slashes
                    ctx.req->printf("<img src=\"../%s\" class=\"sm_entry_file\"><br>", href.c_str());
                }
                ctx.req->printf("%s", htmlEscape(basename).c_str());
                // size of the file
                std::string path = ctx.project->getObjectsDir() + '/' + Object::getSubpath(objectId);
                std::string size = getFileSize(path);
                ctx.req->printf("<span> (%s)</span>", size.c_str());
                ctx.req->printf("</a>");
                ctx.req->printf("</div>\n"); // end file
            }
            ctx.req->printf("</div>\n"); // end files
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
            otherProperties << "<span class=\"sm_entry_pname\">" << htmlEscape(ctx.projectConfig.getLabelOfProperty(p->first))
                            << ": </span>";
            otherProperties << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";

        }

        if (otherProperties.str().size() > 0) {
            ctx.req->printf("<div class=\"sm_entry_other_properties\">\n");
            ctx.req->printf("%s", otherProperties.str().c_str());
            ctx.req->printf("</div>\n");
        }

        ctx.req->printf("</div>\n"); // end entry

        e = e->getNext();
    } // end of entries

    ctx.req->printf("</div>\n");

}

void RHtml::printPageIssue(const ContextParameters &ctx, const Issue &issue, const Entry *eToBeAmended)
{
    VariableNavigator vn("issue.html", ctx);
    vn.currentIssue = &issue;
    vn.entryToBeAmended = eToBeAmended;
    vn.printPage();

    // update the next/previous links

    // class name of the HTML nodes to be updated
    const char* SM_ISSUE_NEXT = "sm_issue_next";
    const char* SM_ISSUE_PREVIOUS = "sm_issue_previous";
    ctx.req->printf("<script>");
    if (eToBeAmended) {
        // give the focus to the message
        ctx.req->printf("document.getElementsByName('+message')[0].focus();");
    }
    if (ctx.originView == 0) {
        // disable the next/previous links
        ctx.req->printf("updateHref('%s', null);\n", SM_ISSUE_NEXT);
        ctx.req->printf("updateHref('%s', null);\n", SM_ISSUE_PREVIOUS);
    } else {
        // update NEXT
        std::string qs = ctx.originView;
        qs += "&" QS_GOTO_NEXT "=" + urlEncode(issue.id);
        ctx.req->printf("updateHref('%s', './?%s');\n", SM_ISSUE_NEXT, enquoteJs(qs).c_str());

        // update PREVIOUS
        qs = ctx.originView;
        qs += "&" QS_GOTO_PREVIOUS "=" + urlEncode(issue.id);
        ctx.req->printf("updateHref('%s', './?%s');\n", SM_ISSUE_PREVIOUS, enquoteJs(qs).c_str());
    }
    ctx.req->printf("</script>");
}



void RHtml::printPageNewIssue(const ContextParameters &ctx)
{
    VariableNavigator vn("newIssue.html", ctx);
    vn.printPage();
}

void RHtml::printFormMessage(const ContextParameters &ctx, const std::string &contents)
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

void RHtml::printEditMessage(const ContextParameters &ctx, const Issue *issue,
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
    ctx.req->printf("<form enctype=\"multipart/form-data\" method=\"post\"  class=\"sm_issue_form\">");
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
void RHtml::printIssueForm(const ContextParameters &ctx, const Issue *issue, bool autofocus)
{
    if (!issue) {
        LOG_ERROR("printIssueForm: Invalid null issue");
        return;
    }
    if (ctx.userRole != ROLE_ADMIN && ctx.userRole != ROLE_RW) {
        return;
    }

    const ProjectConfig &pconfig = ctx.projectConfig;

    ctx.req->printf("<form enctype=\"multipart/form-data\" method=\"post\"  class=\"sm_issue_form\">");

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

            std::set<std::string> users = UserBase::getUsersOfProject(ctx.getProject().getName());
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
