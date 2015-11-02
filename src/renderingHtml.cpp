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
#include "ContextParameters.h"
#include "renderingHtmlIssue.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "stringTools.h"
#include "dateTools.h"
#include "session.h"
#include "global.h"
#include "filesystem.h"
#include "restApi.h"
#include "jTools.h"

#ifdef KERBEROS_ENABLED
  #include "AuthKrb5.h"
#endif

#ifdef LDAP_ENABLED
  #include "AuthLdap.h"
#endif


#define K_SM_DIV_NAVIGATION_GLOBAL "SM_DIV_NAVIGATION_GLOBAL"
#define K_SM_DIV_NAVIGATION_ISSUES "SM_DIV_NAVIGATION_ISSUES"
#define K_SM_HTML_PROJECT "SM_HTML_PROJECT"
#define K_SM_URL_PROJECT "SM_URL_PROJECT" // including the SM_URL_ROOT
#define K_SM_RAW_ISSUE_ID "SM_RAW_ISSUE_ID"
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
#define K_SM_SCRIPT "SM_SCRIPT"
#define K_SM_DIV_ENTRIES "SM_DIV_ENTRIES"
#define K_SM_DATALIST_PROJECTS "SM_DATALIST_PROJECTS"

/** Load a page template of a specific project
  *
  * By default pages (typically HTML pages) are loaded from $REPO/public/ directory.
  * But the administrator may override this by pages located in $REPO/$PROJECT/html/ directory.
  *
  * The caller is responsible for calling 'free' on the returned pointer (if not null).
  */
int loadProjectPage(const RequestContext *req, const std::string &projectPath, const std::string &page, const char **data)
{
    std::string path;
    if (!projectPath.empty()) {
        // first look in the templates of the project
        path = projectPath + "/" PATH_TEMPLATES "/" + page;
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
    const std::vector<IssueCopy> *issueList;
    const std::vector<IssueCopy> *issuesOfAllProjects;
    const std::vector<IssueCopy> *issueListFullContents;
    const std::list<std::string> *colspec;
    const ContextParameters &ctx;
    const std::list<std::pair<std::string, std::string> > *projectList;
    const std::list<User> *usersList;
    const IssueCopy *currentIssue;
    const User *concernedUser;
    const Entry *entryToBeAmended;
    const std::map<std::string, std::map<Role, std::set<std::string> > > *userRolesByProject;
    std::string script; // javascript to be inserted in SM_SCRIPT
    const std::vector<Entry> *entries;

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
        entries = 0;

        int n = loadProjectPage(ctx.req, ctx.projectPath, basename, &buffer);

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

            if (varname == K_SM_HTML_PROJECT) {
                if (!ctx.hasProject()) ctx.req->printf("(new project)");
                else ctx.req->printf("%s", htmlEscape(ctx.projectName).c_str());

            } else if (varname == K_SM_URL_PROJECT && ctx.hasProject()) {
                MongooseServerContext &mc = MongooseServerContext::getInstance();
                ctx.req->printf("%s/%s", mc.getUrlRewritingRoot().c_str(),
                                ctx.getProjectUrlName().c_str());

            } else if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                RHtml::printNavigationGlobal(ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES) {
                // do not print this in case a a new project
                if (ctx.hasProject()) RHtml::printNavigationIssues(ctx, false);

            } else if (varname == K_SM_DIV_PROJECTS && projectList && userRolesByProject) {
                RHtml::printProjects(ctx, *projectList, *userRolesByProject);

            } else if (varname == K_SM_DATALIST_PROJECTS && projectList) {
                RHtml::printDatalistProjects(ctx, *projectList);

            } else if (varname == K_SM_DIV_USERS && usersList) {
                RHtml::printUsers(ctx.req, *usersList);

            } else if (varname == K_SM_TABLE_USER_PERMISSIONS) {
                if (concernedUser) RHtml::printUserPermissions(ctx.req, *concernedUser);
                // else, dont print the table

            } else if (varname == K_SM_RAW_ISSUE_ID && currentIssue) {
                ctx.req->printf("%s", htmlEscape(currentIssue->id).c_str());

            } else if (varname == K_SM_HTML_ISSUE_SUMMARY && currentIssue) {
                ctx.req->printf("%s", htmlEscape(currentIssue->getSummary()).c_str());

            } else if (varname == K_SM_DIV_ISSUES && issueList && colspec) {
                RHtmlIssue::printIssueList(ctx, *issueList, *colspec, true);

            } else if (varname == K_SM_DIV_ISSUES && issuesOfAllProjects && colspec) {
                RHtmlIssue::printIssuesAccrossProjects(ctx, *issuesOfAllProjects, *colspec);

            } else if (varname == K_SM_DIV_ISSUES && issueListFullContents) {
                RHtmlIssue::printIssueListFullContents(ctx, *issueListFullContents);

            } else if (varname == K_SM_DIV_ISSUE && currentIssue) {
                std::string eAmended;
                if (entryToBeAmended) eAmended = entryToBeAmended->id;
                RHtmlIssue::printIssue(ctx, *currentIssue, eAmended);

            } else if (varname == K_SM_DIV_ISSUE_FORM) {
                IssueCopy issue;
                if (!currentIssue) currentIssue = &issue; // set an empty issue

                if (entryToBeAmended) RHtmlIssue::printEditMessage(ctx, currentIssue, *entryToBeAmended);
                else RHtmlIssue::printIssueForm(ctx, currentIssue, false);

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

            } else if (varname == K_SM_SCRIPT) {
                if (!script.empty()) {
                    ctx.req->printf("<script type=\"text/javascript\">%s</script>\n", script.c_str());
                }
            } else if (varname == K_SM_DIV_ENTRIES && entries) {
                RHtml::printEntries(ctx, *entries);
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

void RHtml::printPageStat(const ContextParameters &ctx, const User &u)
{
    VariableNavigator vn("stat.html", ctx);
    vn.printPage();
}

void RHtml::printPageUser(const ContextParameters &ctx, const User *u)
{
    VariableNavigator vn("user.html", ctx);
    vn.concernedUser = u;

    // add javascript for updating the form fields
    vn.script = "";

    if (ctx.user.superadmin) {
        vn.script += "showOrHideClasses('sm_zone_superadmin', true);\n";
        vn.script += "showOrHideClasses('sm_zone_non_superadmin', false);\n";
        if (u) vn.script += "setName('" + enquoteJs(u->username) + "');\n";

    } else {
        // hide what is reserved to superadmin
        vn.script += "showOrHideClasses('sm_zone_superadmin', false);\n";
        vn.script += "showOrHideClasses('sm_zone_non_superadmin', true);\n";
    }

    if (u && u->superadmin) {
        vn.script += "setSuperadminCheckbox();\n";
    }

    if (ctx.user.superadmin) {
        // the signed-in user has permission to modify the permissions of the user

        std::list<std::string> projects = Database::getProjects();
        vn.script += "Projects = " + toJavascriptArray(projects) + ";\n";
        std::list<std::string> roleList = getAvailableRoles();
        vn.script += "Roles = " + toJavascriptArray(roleList) + ";\n";
        vn.script += "addProjectDatalist('sm_permissions');\n";

        if (u) {
            std::map<std::string, enum Role>::const_iterator rop;
            FOREACH(rop, u->permissions) {
                vn.script += "addPermission('sm_permissions', '" + enquoteJs(rop->first)
                        + "', '" + enquoteJs(roleToString(rop->second)) + "');\n";
            }
        }

        vn.script +=  "addPermission('sm_permissions', '', '');\n";
        vn.script +=  "addPermission('sm_permissions', '', '');\n";
        if (u && u->authHandler) {
            if (u->authHandler->type == "sha1") {
                vn.script += "setAuthSha1();\n";
#ifdef KERBEROS_ENABLED
            } else if (u->authHandler->type == "krb5") {
                AuthKrb5 *ah = dynamic_cast<AuthKrb5*>(u->authHandler);
                vn.script += "setAuthKrb5('" + enquoteJs(ah->alternateUsername) + "', '" + enquoteJs(ah->realm) + "');\n";
#endif
#ifdef LDAP_ENABLED
            } else if (u->authHandler->type == "ldap") {
                AuthLdap *ah = dynamic_cast<AuthLdap*>(u->authHandler);
                vn.script += "setAuthLdap('" + enquoteJs(ah->uri) + "', '" + enquoteJs(ah->dname) + "');\n";
#endif
            }
            // else, it may be empty, if no auth type is assigned
        }
    }

    vn.printPage();

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
            std::set<std::string> users = UserBase::getUsersOfProject(ctx.projectName);
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
                    ctx.getProjectUrlName().c_str(),
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
    FOREACH(pv, ctx.projectViews) {
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

    if (ctx.hasProject() && (ctx.userRole == ROLE_ADMIN || ctx.user.superadmin) ) {
        // link for modifying project structure
        HtmlNode linkToModify("a");
        linkToModify.addAttribute("class", "sm_link_modify_project");
        linkToModify.addAttribute("href", "/%s/config", ctx.getProjectUrlName().c_str());
        linkToModify.addContents("%s", _("Project config"));
        div.addContents(" ");
        div.addContents(linkToModify);

        // link to config of predefined views
        HtmlNode linkToViews("a");
        linkToViews.addAttribute("class", "sm_link_views");
        linkToViews.addAttribute("href", "/%s/views/", ctx.getProjectUrlName().c_str());
        linkToViews.addContents("%s", _("Views"));
        div.addContents(" ");
        div.addContents(linkToViews);
    }

    if (ctx.hasProject()) {
        HtmlNode linkToStat("a");
        linkToStat.addAttribute("class", "sm_link_stat");
        linkToStat.addAttribute("href", "/%s/stat", ctx.getProjectUrlName().c_str());
        linkToStat.addContents("%s", _("Stat"));
        div.addContents(" ");
        div.addContents(linkToStat);
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
        a.addAttribute("href", "/%s/issues/new", ctx.getProjectUrlName().c_str());
        a.addAttribute("class", "sm_link_new_issue");
        a.addContents("%s", _("New issue"));
        div.addContents(a);
    }

    std::map<std::string, PredefinedView>::const_iterator pv;
    FOREACH (pv, ctx.projectViews) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/?%s", ctx.getProjectUrlName().c_str(),
                       pv->second.generateQueryString().c_str());
        a.addAttribute("class", "sm_predefined_view");
        a.addContents("%s", pv->first.c_str());
        div.addContents(a);
    }

    HtmlNode form("form");
    form.addAttribute("class", "sm_searchbox");
    form.addAttribute("action", "/%s/issues/", ctx.getProjectUrlName().c_str());
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
    a.addAttribute("href", "/%s/views/_", ctx.getProjectUrlName().c_str());
    a.addAttribute("class", "sm_advanced_search");
    a.addContents(_("Advanced Search"));
    div.addContents(a);


    div.print(ctx.req);
}

void RHtml::printDatalistProjects(const ContextParameters &ctx,
                                  const std::list<std::pair<std::string, std::string> > &pList)
{
    std::list<std::pair<std::string, std::string> >::const_iterator p;

    ctx.req->printf("<datalist id=\"sm_projects\">\n");
    FOREACH(p, pList) {
        ctx.req->printf("<option value=\"%s\">\n", htmlEscape(p->first).c_str());
    }
    ctx.req->printf("</datalist>\n");
}

void RHtml::printProjects(const ContextParameters &ctx,
                          const std::list<std::pair<std::string, std::string> > &pList,
                          const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject)
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
        urit = userRolesByProject.find(pname);
        if (urit != userRolesByProject.end()) {

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

/** Generate javascript that will fulfill the inputs of the project configuration
  */
std::string RHtml::getScriptProjectConfig(const ContextParameters &ctx)
{
    std::string script;

    // fulfill reserved properties first
    std::list<std::string> reserved = ProjectConfig::getReservedProperties();
    std::list<std::string>::iterator r;
    FOREACH(r, reserved) {
        std::string label = ctx.projectConfig.getLabelOfProperty(*r);
        script += "addProperty('" +
                enquoteJs(*r) + "', '" +
                enquoteJs(label) + "', 'reserved', '');\n";
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
        script +=  "addProperty('" + enquoteJs(pspec->name) +
                "', '" + enquoteJs(label) +
                "', '" + type +
                "', '" + options + "');\n";
    }

    // add 3 more empty properties
    script += "addProperty('', '', '', '');\n";
    script += "addProperty('', '', '', '');\n";
    script += "addProperty('', '', '', '');\n";

    script += "replaceContentInContainer();\n";

    // add tags
    std::map<std::string, TagSpec>::const_iterator tagspec;
    FOREACH(tagspec, c.tags) {
        const TagSpec& tpsec = tagspec->second;
        script += "addTag('" + enquoteJs(tpsec.id) +
                "', '" + enquoteJs(tpsec.label) + "', ";
        if (tpsec.display) script += "true";
        else script += "false";
        script += ");\n";
    }

    script += "addTag('', '', '', '');\n";
    script += "addTag('', '', '', '');\n";

    // manage issue numbering policy
    if (ctx.projectConfig.numberIssueAcrossProjects) {
        script += "setIssueNumberingPolicy(true);\n";
    }
    return script;
}


void RHtml::printProjectConfig(const ContextParameters &ctx,
                               const std::list<std::pair<std::string, std::string> > &pList)
{
    VariableNavigator vn("project.html", ctx);
    vn.projectList = &pList;
    vn.script = getScriptProjectConfig(ctx);

    if (ctx.user.superadmin) {
        vn.script += "showOrHideClasses('sm_zone_superadmin', true);\n";
        if (ctx.hasProject()) {
            vn.script += "setProjectName('" + enquoteJs(ctx.projectName) + "');\n";
        }

    } else {
        // hide what is reserved to superadmin
        vn.script += "showOrHideClasses('sm_zone_superadmin', false);\n";
    }

    vn.printPage();
}


/** Print HTML page with the given issues and their full contents
  *
  */
void RHtml::printPageIssuesFullContents(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueListFullContents = &issueList;
    vn.printPage();
}

void RHtml::printPageIssueList(const ContextParameters &ctx,
                               const std::vector<IssueCopy> &issueList, const std::list<std::string> &colspec)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueList = &issueList;
    vn.colspec = &colspec;
    vn.printPage();
}
void RHtml::printPageIssueAccrossProjects(const ContextParameters &ctx,
                                          const std::vector<IssueCopy> &issues,
                                          const std::list<std::string> &colspec)
{
    VariableNavigator vn("issuesAccross.html", ctx);
    vn.issuesOfAllProjects = &issues;
    vn.colspec = &colspec;
    vn.printPage();
}


void RHtml::printPageIssue(const ContextParameters &ctx, const IssueCopy &issue, const Entry *eToBeAmended)
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

    // check display options
    std::string display = getFirstParamFromQueryString(ctx.req->getQueryString(), "display");
    if (display == "properties_changes") {
        ctx.req->printf("showPropertiesChanges();\n");
    }
    ctx.req->printf("</script>");
}

void RHtml::printPageNewIssue(const ContextParameters &ctx)
{
    VariableNavigator vn("newIssue.html", ctx);
    vn.printPage();
}

void RHtml::printPageEntries(const ContextParameters &ctx,
                             const std::vector<Entry> &entries)
{
    VariableNavigator vn("entries.html", ctx);
    vn.entries = &entries;
    vn.script = "showPropertiesChanges();"; // because by default they are hidden
    vn.printPage();
}

void RHtml::printEntries(const ContextParameters &ctx, const std::vector<Entry> &entries)
{
    ctx.req->printf("<div class=\"sm_entries\">\n");

    printFilters(ctx);

    // number of entries
    ctx.req->printf("<div class=\"sm_entries_count\">%s: <span class=\"sm_entries_count\">%lu</span></div>\n",
                    _("Entries found"), L(entries.size()));

    ctx.req->printf("<table class=\"sm_entries\">\n");

    // print header of the table
    // Columns: issue-id | ctime | author | fields modified by entry
    ctx.req->printf("<tr class=\"sm_entries\">\n");

    // id
    std::string label = ctx.projectConfig.getLabelOfProperty("id");
    ctx.req->printf("<th class=\"sm_entries\">%s\n", htmlEscape(label).c_str());

    // ctime
    label = _("Date");
    ctx.req->printf("<th class=\"sm_entries\">%s\n", htmlEscape(label).c_str());

    // author
    label = _("Author");
    ctx.req->printf("<th class=\"sm_entries\">%s\n", htmlEscape(label).c_str());

    // summary
    label = ctx.projectConfig.getLabelOfProperty(K_SUMMARY);
    ctx.req->printf("<th class=\"sm_entries\">%s\n", htmlEscape(label).c_str());

    // fields modified by entry
    label = _("Modification");
    ctx.req->printf("<th class=\"sm_entries\">%s\n", htmlEscape(label).c_str());

    ctx.req->printf("</tr>\n");

    // print the rows (one for each entry)
    std::vector<Entry>::const_iterator e;
    FOREACH(e, entries) {

        if (!e->issue) {
            LOG_ERROR("null issue for entry %s", e->id.c_str());
            continue;
        }

        ctx.req->printf("<tr class=\"sm_entries\">\n");

        // id of the issue (not id of the entry)
        std::string href = MongooseServerContext::getInstance().getUrlRewritingRoot() + "/";
        href += Project::urlNameEncode(e->issue->project) + "/issues/";
        href += urlEncode(e->issue->id);
        href += "?display=properties_changes#" + urlEncode(e->id);

        ctx.req->printf("<td class=\"sm_entries\"><a href=\"%s\">%s", href.c_str(), htmlEscape(e->issue->id).c_str());
        // print if the issue is newly created by this entry
        if (e->issue->first && e->issue->first->id == e->id) ctx.req->printf("*"); // '*' to denote new issue

        ctx.req->printf("</a></td>\n"); // end of issue id

        // ctime
        ctx.req->printf("<td class=\"sm_entries\">%s</td>\n", epochToStringDelta(e->ctime).c_str());

        // author
        ctx.req->printf("<td class=\"sm_entries\">%s</td>\n", htmlEscape(e->author).c_str());

        // summary
        ctx.req->printf("<td class=\"sm_entries\"><a href=\"%s\">%s</a></td>\n",
                        href.c_str(), htmlEscape(e->issue->getSummary()).c_str());

        // changed properties
        ctx.req->printf("<td class=\"sm_entries\">");
        RHtmlIssue::printOtherProperties(ctx, *e, true, 0);
        ctx.req->printf("</td>");

        ctx.req->printf("</tr>\n");
    }
    ctx.req->printf("</table>\n");
    ctx.req->printf("</div>\n");
}
