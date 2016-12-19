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
#include "renderingHtmlUtil.h"
#include "repository/db.h"
#include "utils/parseConfig.h"
#include "utils/logging.h"
#include "utils/stringTools.h"
#include "utils/dateTools.h"
#include "user/session.h"
#include "global.h"
#include "utils/filesystem.h"
#include "restApi.h"
#include "utils/jTools.h"

#ifdef KERBEROS_ENABLED
  #include "AuthKrb5.h"
#endif

#ifdef LDAP_ENABLED
  #include "AuthLdap.h"
#endif


// Basic items
#define K_SM_URL_ROOT           "SM_URL_ROOT"
#define K_SM_HTML_PROJECT       "SM_HTML_PROJECT"  // Project name
#define K_SM_URL_PROJECT        "SM_URL_PROJECT"   // URL to project, including the SM_URL_ROOT
#define K_SM_URL_USER           "SM_URL_USER"      // User name, in url-encoded format
#define K_SM_HTML_USER          "SM_HTML_USER"     // User name, in html-escaped format
#define K_SM_RAW_ISSUE_ID       "SM_RAW_ISSUE_ID"  // Issue ID, no formatting
#define K_SM_HTML_ISSUE_SUMMARY "SM_HTML_ISSUE_SUMMARY" // Summary of an issue, html-escaped
#define K_SM_DATALIST_PROJECTS  "SM_DATALIST_PROJECTS" // <datalist> of projects names

// Whole blocks of page
// ---- in the scope of a project
#define K_SM_SPAN_VIEWS_MENU       "SM_SPAN_VIEWS_MENU" // Menu of views
#define K_SM_DIV_PREDEFINED_VIEWS  "SM_DIV_PREDEFINED_VIEWS" // page for editing views
#define K_SM_DIV_PROJECTS          "SM_DIV_PROJECTS"
#define K_SM_DIV_ISSUES            "SM_DIV_ISSUES"
#define K_SM_DIV_ISSUE             "SM_DIV_ISSUE"
#define K_SM_DIV_ISSUE_FORM        "SM_DIV_ISSUE_FORM"
#define K_SM_DIV_ISSUE_MSG_PREVIEW "SM_DIV_ISSUE_MSG_PREVIEW"
#define K_SM_DIV_ENTRIES           "SM_DIV_ENTRIES"
// ---- not related to any specific project
#define K_SM_DIV_USERS              "SM_DIV_USERS"
#define K_SM_TABLE_USER_PERMISSIONS "SM_TABLE_USER_PERMISSIONS"

// Technical SM variables
#define K_SM_INCLUDE "SM_INCLUDE"
#define K_SM_SCRIPT  "SM_SCRIPT"

// Obsolete SM variables
#define K_SM_DIV_NAVIGATION_GLOBAL "SM_DIV_NAVIGATION_GLOBAL"
#define K_SM_DIV_NAVIGATION_ISSUES "SM_DIV_NAVIGATION_ISSUES"


/** Load a page template of a specific project
  *
  * By default pages (typically HTML pages) are loaded from $REPO/.smit/templates/ directory.
  * But the administrator may override this by pages located in $REPO/$PROJECT/.smip/templates/ directory.
  *
  * The caller is responsible for calling 'free' on the returned pointer (if not null).
  */
int loadProjectPage(const ResponseContext *req, const std::string &projectPath, const std::string &page, const char **data)
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

    const ContextParameters &ctx;

    const std::vector<IssueCopy> *issueList;
    const std::vector<IssueCopy> *issuesOfAllProjects;
    const std::vector<IssueCopy> *issueListFullContents;
    const std::list<std::string> *colspec;
    const std::list<ProjectSummary> *projectList;
    const std::list<User> *usersList;
    const IssueCopy *currentIssue;
    const User *concernedUser;
    const Entry *entryToBeAmended;
    const std::map<std::string, std::map<Role, std::set<std::string> > > *userRolesByProject;
    std::string script; // javascript to be inserted in SM_SCRIPT
    const std::vector<Entry> *entries;

    VariableNavigator(const std::string &basename, const ContextParameters &context) : ctx(context) {
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
        searchFromHere = 0;
        dumpStart = 0;
        dumpEnd = 0;
        size = 0;
        filename = basename;
    }

    ~VariableNavigator() {
        if (buffer) {
            LOG_DEBUG("Freeing buffer %p", buffer);
            free((void*)buffer); // the buffer was allocated in loadFile or in loadProjectPage
            buffer = 0;
        }
    }

    /** Look for the next SM_ variable
      *
      * A SM_ variable is syntactically like this:
      *    "SM_" followed by any character letter, digit or '_'
      */
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

    /** Return the filename after a SM_INCLUDE
      *
      * When this function is called, the next expected characters are: '(' <filename> ')'.
      * On invalid syntax, an empty string is returned, and offset are kept unchanged.
      */
    std::string getInclude() {
        if (searchFromHere >= (buffer+size)) return "";
        if (*searchFromHere != '(') {
            LOG_ERROR("getInclude: missing '('");
            return "";
        }

        const char *p0 = searchFromHere + 1; // offset of the start of he filename

        // go to the next ')'
        const char *p = p0;
        while ( (p < buffer+size) && (*p != ')') ) p++;

        if (p >= buffer+size) {
            LOG_ERROR("getInclude: missing ')'");
            return "";
        }

        // from p0 to p-1
        std::string filename(p0, p-p0);

        // Do some checks, and keep only the basename
        std::string filenameBase = getBasename(filename);
        if (filenameBase != filename || filename.empty()) {
            LOG_ERROR("getInclude: malformed file name '%s'. It should not contain sub-directories, etc.",
                      filename.c_str());
            return "";
        }
        // ok, accept the filename
        // update the offsets
        searchFromHere = p+1;
        dumpStart = searchFromHere;
        dumpEnd = dumpStart;

        return filename;
    }

    void dumpPrevious(const ResponseContext *req) {
        if (dumpEnd == dumpStart) {
            LOG_ERROR("dumpPrevious: dumpEnd == dumpStart");
            return;
        }
        req->write(dumpStart, dumpEnd-dumpStart);
        dumpStart = searchFromHere;
        dumpEnd = dumpStart;
    }

    void printPage() {

        ctx.req->printf("Content-Type: text/html\r\n\r\n");

        printPageContents(0);
    }

    void printPageContents(int includeLevel) {

        const int MAX_INCLUDE_LEVEL = 5;
        if (includeLevel > MAX_INCLUDE_LEVEL) {
            // Basic mechanism to protect against circular includes
            LOG_ERROR("printPageContents: max include level reached (%d)", MAX_INCLUDE_LEVEL);
            return;
        }

        int n = loadProjectPage(ctx.req, ctx.projectPath, filename, &buffer);

        if (n <= 0) return;

        size = n;
        dumpStart = buffer;
        dumpEnd = buffer;
        searchFromHere = buffer;

        while (1) {
            std::string varname = getNextVariable();
            dumpPrevious(ctx.req);
            if (varname.empty()) break;

            if (varname == K_SM_HTML_PROJECT) {
                if (!ctx.hasProject()) ctx.req->printf("(new project)");
                else ctx.req->printf("%s", htmlEscape(ctx.projectName).c_str());

            } else if (varname == K_SM_URL_PROJECT && ctx.hasProject()) {
                ctx.req->printf("%s/%s", ctx.req->getUrlRewritingRoot().c_str(),
                                ctx.getProjectUrlName().c_str());

            } else if (varname == K_SM_URL_USER) {
                ctx.req->printf("%s", urlEncode(ctx.user.username).c_str());

            } else if (varname == K_SM_HTML_USER) {
                ctx.req->printf("%s", htmlEscape(ctx.user.username).c_str());

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

            } else if (varname == K_SM_SPAN_VIEWS_MENU && ctx.hasProject()) {
                ctx.req->printf("%s", RHtml::getMenuViews(ctx).c_str());

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
                ctx.req->printf("%s", ctx.req->getUrlRewritingRoot().c_str());

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

            } else if (varname == K_SM_INCLUDE) {
                // Expected syntax: SM_INCLUDE(filename.html)
                std::string filename = getInclude();
                VariableNavigator vn(filename, ctx);
                vn.printPageContents(includeLevel+1);

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
    std::string filename;

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
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}

void RHtml::printPageUser(const ContextParameters &ctx, const User *u)
{
    VariableNavigator vn("user.html", ctx);
    vn.concernedUser = u;

    // add javascript for updating the form fields

    vn.script = jsSetUserCapAndRole(ctx);

    if (ctx.user.superadmin) {
        if (u) vn.script += "setName('" + enquoteJs(u->username) + "');\n";

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

    if (u) {
        vn.script += "setFormValue('sm_email', '" + enquoteJs(u->notification.email) + "');\n";
        vn.script += "setFormValue('sm_gpg_key', '" + enquoteJs(u->notification.gpgPublicKey) + "');\n";
        vn.script += "setFormValue('sm_notif_policy', '" + enquoteJs(u->notification.notificationPolicy) + "');\n";
    }
    vn.printPage();

}

void RHtml::printPageView(const ContextParameters &ctx, const PredefinedView &pv)
{
    VariableNavigator vn("view.html", ctx);
    vn.script = jsSetUserCapAndRole(ctx);

    // add javascript for updating the inputs

    if (ctx.userRole == ROLE_ADMIN) {
        vn.script += "setName('" + enquoteJs(pv.name) + "');\n";
    }

    if (pv.isDefault) vn.script += "setDefaultCheckbox();\n";

    std::list<std::string> properties = ctx.projectConfig.getPropertiesNames();
    vn.script += "Properties = " + toJavascriptArray(properties) + ";\n";

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
            vn.script += "addFilterDatalist('filterin', 'datalist_" + enquoteJs(propname) + "', "
                    + toJavascriptArray(proposedValues) + ");\n";
        }
    }

    vn.script += "setSearch('" + enquoteJs(pv.search) + "');\n";
    vn.script += "setUrl('" +
            ctx.req->getUrlRewritingRoot() + "/" +
            ctx.getProjectUrlName() + "/issues/?" +
            pv.generateQueryString() + "');\n";

    // add datalists for all types select, multiselect and selectuser

    // filter in and out
    std::map<std::string, std::list<std::string> >::const_iterator f;
    std::list<std::string>::const_iterator v;
    FOREACH(f, pv.filterin) {
        FOREACH(v, f->second) {
            vn.script += "addFilter('filterin', '" + enquoteJs(f->first) + "', '" + enquoteJs(*v) + "');\n";
        }
    }
    vn.script += "addFilter('filterin', '', '');\n";

    FOREACH(f, pv.filterout) {
        FOREACH(v, f->second) {
            vn.script += "addFilter('filterout', '" + enquoteJs(f->first) + "', '" + enquoteJs(*v) + "');\n";
        }
    }
    vn.script += "addFilter('filterout', '', '');\n";

    // Colums specification
    if (!pv.colspec.empty()) {
        std::list<std::string> items = split(pv.colspec, " +");
        std::list<std::string>::iterator i;
        FOREACH(i, items) {
            vn.script += "addColspec('" + enquoteJs(*i) + "');\n";
        }
    }
    vn.script += "addColspec('');\n";

    // sort
    std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(pv.sort.c_str());
    std::list<std::pair<bool, std::string> >::iterator s;
    FOREACH(s, sSpec) {
        std::string direction = PredefinedView::getDirectionName(s->first);
        vn.script += "addSort('" + enquoteJs(direction) + "', '" + enquoteJs(s->second) + "');\n";
    }
    vn.script += "addSort('', '');\n";

    vn.printPage();
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
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}

#define LOCAL_SIZE 512
class HtmlNode {
public:
    HtmlNode(const ContextParameters &c, const std::string &_nodeName) : ctx(c) {
        nodeName = _nodeName;
    }
    /** Constructor for text contents */
    HtmlNode(const ContextParameters &c) : ctx(c) { }

    std::string nodeName;// empty if text content only
    std::map<std::string, std::string> attributes;
    std::string text;
    const ContextParameters &ctx;
    std::list<HtmlNode> contents;

    /** the values must be html-escaped */
    void addAttribute(const char *name, const char* format, ...) {
        va_list list;
        va_start(list, format);
        char value[LOCAL_SIZE];
        int n = vsnprintf(value, LOCAL_SIZE, format, list);
        va_end(list);
        if (n >= LOCAL_SIZE || n < 0) {
            LOG_ERROR("addAttribute error: vsnprintf n=%d", n);
        } else {
            std::string val = value;
            std::string sname = name;
            // do url rewriting if needed
            if ( ( (sname == "href") || (sname == "src") || (sname == "action") )
                 && value[0] == '/') {
                val = ctx.req->getUrlRewritingRoot() + val;
            }

            attributes[name] = val;
        }
    }

    std::string getHtml() {
        if (nodeName.empty()) {
             // text contents
            return text;
        }

        std::string html = "";
        html += "<" +  nodeName + " ";

        std::map<std::string, std::string>::iterator i;
        FOREACH(i, attributes) {
            html += i->first + "=\"" + i->second + "\" ";
        }
        html += ">\n";

        if (nodeName == "input") return html; // no closing tag nor any contents

        std::list<HtmlNode>::iterator c;
        FOREACH(c, contents) {
            html += c->getHtml();
        }
        // close HTML node
        html += "</" +  nodeName + ">";

        return html;
    }

    void print(const ResponseContext *req) {

        req->printf("%s", getHtml().c_str());

    }

    void addContents(const char* format, ...) {
        va_list list;
        va_start(list, format);
        char buffer[LOCAL_SIZE];
        int n = vsnprintf(buffer, LOCAL_SIZE, format, list);
        va_end(list);
        if (n >= LOCAL_SIZE || n < 0) {
            LOG_ERROR("addText error: vsnprintf n=%d", n);
        } else {
            HtmlNode node(ctx);
            node.text = htmlEscape(buffer);
            contents.push_back(node);
        }
    }

    void addRawContents(const std::string &rawText) {
        HtmlNode node(ctx);
        node.text = rawText;
        contents.push_back(node);
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
    HtmlNode div(ctx, "div");
    div.addAttribute("class", "sm_navigation_global");
    HtmlNode linkToProjects(ctx, "a");
    linkToProjects.addAttribute("class", "sm_link_projects");
    linkToProjects.addAttribute("href", "/");
    linkToProjects.addContents("%s", _("Projects"));
    div.addContents(linkToProjects);

    if (ctx.user.superadmin) {
        // link to all users
        HtmlNode allUsers(ctx, "a");
        allUsers.addAttribute("class", "sm_link_users");
        allUsers.addAttribute("href", "/users");
        allUsers.addContents("%s", _("Users"));
        div.addContents(" ");
        div.addContents(allUsers);
    }

    if (ctx.hasProject() && (ctx.userRole == ROLE_ADMIN || ctx.user.superadmin) ) {
        // link for modifying project structure
        HtmlNode linkToModify(ctx, "a");
        linkToModify.addAttribute("class", "sm_link_modify_project");
        linkToModify.addAttribute("href", "/%s/config", ctx.getProjectUrlName().c_str());
        linkToModify.addContents("%s", _("Project config"));
        div.addContents(" ");
        div.addContents(linkToModify);

        // link to config of predefined views
        HtmlNode linkToViews(ctx, "a");
        linkToViews.addAttribute("class", "sm_link_views");
        linkToViews.addAttribute("href", "/%s/views/", ctx.getProjectUrlName().c_str());
        linkToViews.addContents("%s", _("Views"));
        div.addContents(" ");
        div.addContents(linkToViews);
    }

    if (ctx.hasProject()) {
        HtmlNode linkToStat(ctx, "a");
        linkToStat.addAttribute("class", "sm_link_stat");
        linkToStat.addAttribute("href", "/%s/stat", ctx.getProjectUrlName().c_str());
        linkToStat.addContents("%s", _("Stat"));
        div.addContents(" ");
        div.addContents(linkToStat);
    }

    // signed-in
    HtmlNode userinfo(ctx, "span");
    userinfo.addAttribute("class", "sm_userinfo");
    userinfo.addContents("%s", _("Logged in as: "));
    HtmlNode username(ctx, "span");
    username.addAttribute("class", "sm_username");
    username.addContents("%s", ctx.user.username.c_str());
    userinfo.addContents(username);
    div.addContents(userinfo);
    div.addContents(" - ");

    // form to sign-out
    HtmlNode signout(ctx, "form");
    signout.addAttribute("action", "/signout");
    signout.addAttribute("method", "post");
    signout.addAttribute("id", "sm_signout");
    HtmlNode linkSignout(ctx, "a");
    linkSignout.addAttribute("href", "javascript:;");
    linkSignout.addAttribute("onclick", "document.getElementById('sm_signout').submit();");
    linkSignout.addContents("%s", _("Sign out"));
    signout.addContents(linkSignout);
    div.addContents(signout);

    div.addContents(" - ");

    // link to user profile
    HtmlNode linkToProfile(ctx, "a");
    linkToProfile.addAttribute("href", "/users/%s", urlEncode(ctx.user.username).c_str());
    linkToProfile.addContents(_("Profile"));
    div.addContents(linkToProfile);

    div.print(ctx.req);

}

std::string RHtml::getMenuViews(const ContextParameters &ctx)
{
    HtmlNode span(ctx, "span");

    std::map<std::string, PredefinedView>::const_iterator pv;
    FOREACH (pv, ctx.projectViews) {
        HtmlNode a(ctx, "a");
        a.addAttribute("href", "/%s/issues/?%s", ctx.getProjectUrlName().c_str(),
                       pv->second.generateQueryString().c_str());
        a.addAttribute("class", "sm_predefined_view");
        a.addContents("%s", pv->first.c_str());
        span.addContents(a);
    }

    return span.getHtml();
}

/** Print links for navigating through issues;
  * - "create new issue"
  * - predefined views
  * - quick search form
  */
void RHtml::printNavigationIssues(const ContextParameters &ctx, bool autofocus)
{
    HtmlNode div(ctx, "div");
    div.addAttribute("class", "sm_navigation_project");
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW || ctx.user.superadmin) {
        HtmlNode a(ctx, "a");
        a.addAttribute("href", "/%s/issues/new", ctx.getProjectUrlName().c_str());
        a.addAttribute("class", "sm_link_new_issue");
        a.addContents("%s", _("New issue"));
        div.addContents(a);
    }

    div.addRawContents(getMenuViews(ctx));

    HtmlNode form(ctx, "form");
    form.addAttribute("class", "sm_searchbox");
    form.addAttribute("action", "/%s/issues/", ctx.getProjectUrlName().c_str());
    form.addAttribute("method", "get");

    HtmlNode input(ctx, "input");
    input.addAttribute("class", "sm_searchbox");
    input.addAttribute("placeholder", htmlEscape(_("Search text...")).c_str());
    input.addAttribute("type", "text");
    input.addAttribute("name", "search");
    if (autofocus) input.addAttribute("autofocus", "autofocus");
    form.addContents(input);
    div.addContents(form);

    // advanced search
    HtmlNode a(ctx, "a");
    a.addAttribute("href", "/%s/views/_", ctx.getProjectUrlName().c_str());
    a.addAttribute("class", "sm_advanced_search");
    a.addContents(_("Advanced Search"));
    div.addContents(a);


    div.print(ctx.req);
}

void RHtml::printDatalistProjects(const ContextParameters &ctx,
                                  const std::list<ProjectSummary> &pList)
{
    std::list<ProjectSummary>::const_iterator p;

    ctx.req->printf("<datalist id=\"sm_projects\">\n");
    FOREACH(p, pList) {
        ctx.req->printf("<option value=\"%s\">\n", htmlEscape(p->name).c_str());
    }
    ctx.req->printf("</datalist>\n");
}

void RHtml::printProjects(const ContextParameters &ctx,
                          const std::list<ProjectSummary> &pList,
                          const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject)
{
    std::list<ProjectSummary>::const_iterator p;
    ctx.req->printf("<table class=\"sm_projects\">\n");
    ctx.req->printf("<tr>"
                    "<th>%s</th>"
                    "<th>%s</th>"
                    "<th>%s</th>"
                    "<th>%s</th>", _("Projects"), _("Trigger"), _("# Issues"), _("Last Modified"));
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
        std::string pname = p->name;
        ctx.req->printf("<tr>\n");

        ctx.req->printf("<td class=\"sm_projects_link\">");
        ctx.req->printf("<a href=\"%s/%s/issues/?defaultView=1\">%s</a></td>\n",
                        ctx.req->getUrlRewritingRoot().c_str(),
                        Project::urlNameEncode(pname).c_str(), htmlEscape(pname).c_str());
        // trigger
        ctx.req->printf("<td>");
        if (!p->triggerCmdline.empty()) {
            ctx.req->printf("<span title=\"%s\">T</span>\n",
                            htmlAttributeEscape(p->triggerCmdline).c_str());
        }
        ctx.req->printf("</td>\n");


        // # issues
        ctx.req->printf("<td>%d</td>\n", _(p->nIssues));

        // last modified
        ctx.req->printf("<td>");
        if (p->lastModified < 0) ctx.req->printf("-");
        else ctx.req->printf("%s", htmlEscape(epochToStringDelta(p->lastModified)).c_str());
        ctx.req->printf("</td>\n");

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
                        const char *extra = "";
                        if ( (*user) == ctx.user.username) extra = "sm_projects_stakeholder_me";

                        ctx.req->printf("<span class=\"sm_projects_stakeholder %s\">%s</span>",
                                        extra, htmlEscape(*user).c_str());
                    }
                    ctx.req->printf("</td>");
                }
            }
        }
        ctx.req->printf("</tr>\n"); // end row for this project
    }
    ctx.req->printf("</table>");
}

void RHtml::printUsers(const ResponseContext *req, const std::list<User> &usersList)
{
    std::list<User>::const_iterator u;

    if (usersList.empty()) return;

    req->printf("<table class=\"sm_users\">\n");
    req->printf("<tr class=\"sm_users\">");
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Users"));
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Capabilities"));
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Authentication"));
    req->printf("<th class=\"sm_users\">%s</th>\n", _("Notification"));
    req->printf("</tr>");

    FOREACH(u, usersList) {
        req->printf("<tr class=\"sm_users\">");
        req->printf("<td class=\"sm_users\">\n");
        req->printf("<a href=\"%s/users/%s\">%s<a><br>",
                    req->getUrlRewritingRoot().c_str(),
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

        // notification
        req->printf("<td class=\"sm_users\">\n");
        req->printf("%s", u->notification.toString().c_str());
        req->printf("</tr>\n");

    }
    req->printf("</table><br>\n");
}

void RHtml::printUserPermissions(const ResponseContext *req, const User &u)
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
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}

void RHtml::printPageProjectList(const ContextParameters &ctx,
                                 const std::list<ProjectSummary> &pList,
                                 const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject)
{
    VariableNavigator vn("projects.html", ctx);
    vn.projectList = &pList;
    vn.userRolesByProject = &userRolesByProject;
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}

/** Generate javascript that will fulfill the inputs of the project configuration
  */
std::string RHtml::getScriptProjectConfig(const ContextParameters &ctx, const ProjectConfig *alternateConfig)
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
    const ProjectConfig *c;
    if (alternateConfig) c = alternateConfig;
    else c = &(ctx.projectConfig);

    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, c->properties) {
        std::string type = propertyTypeToStr(pspec->type);

        std::string label = c->getLabelOfProperty(pspec->name);
        std::list<std::string>::const_iterator i;
        std::string options;
        if (pspec->type == F_SELECT || pspec->type == F_MULTISELECT) {
            FOREACH (i, pspec->selectOptions) {
                if (i != pspec->selectOptions.begin()) options += "\\n";
                options += enquoteJs(*i);
            }
        } else if (pspec->type == F_ASSOCIATION) {
            options = pspec->reverseLabel;
            options = enquoteJs(options);

        } else if (pspec->type == F_TEXTAREA2) {
            options = pspec->ta2Template;
            options = enquoteJs(options);
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

    // add tags
    std::map<std::string, TagSpec>::const_iterator tagspec;
    FOREACH(tagspec, c->tags) {
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
    if (c->numberIssueAcrossProjects) {
        script += "setIssueNumberingPolicy(true);\n";
    }
    return script;
}


void RHtml::printProjectConfig(const ContextParameters &ctx,
                               const std::list<ProjectSummary> &pList,
                               const ProjectConfig *alternateConfig)
{
    VariableNavigator vn("project.html", ctx);
    vn.projectList = &pList;
    vn.script = jsSetUserCapAndRole(ctx);
    vn.script += getScriptProjectConfig(ctx, alternateConfig);

    if (ctx.user.superadmin && ctx.hasProject()) {
        vn.script += "setProjectName('" + enquoteJs(ctx.projectName) + "');\n";
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
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}

void RHtml::printPageIssueList(const ContextParameters &ctx,
                               const std::vector<IssueCopy> &issueList, const std::list<std::string> &colspec)
{
    VariableNavigator vn("issues.html", ctx);
    vn.issueList = &issueList;
    vn.colspec = &colspec;
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}
void RHtml::printPageIssueAccrossProjects(const ContextParameters &ctx,
                                          const std::vector<IssueCopy> &issues,
                                          const std::list<std::string> &colspec)
{
    VariableNavigator vn("issuesAccross.html", ctx);
    vn.issuesOfAllProjects = &issues;
    vn.colspec = &colspec;
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}


void RHtml::printPageIssue(const ContextParameters &ctx, const IssueCopy &issue, const Entry *eToBeAmended)
{
    VariableNavigator vn("issue.html", ctx);
    vn.currentIssue = &issue;
    vn.entryToBeAmended = eToBeAmended;
    vn.script = jsSetUserCapAndRole(ctx);
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
    vn.script = jsSetUserCapAndRole(ctx);
    vn.printPage();
}

void RHtml::printPageEntries(const ContextParameters &ctx,
                             const std::vector<Entry> &entries)
{
    VariableNavigator vn("entries.html", ctx);
    vn.entries = &entries;
    vn.script = jsSetUserCapAndRole(ctx);
    vn.script += "showPropertiesChanges();"; // because by default they are hidden
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
        std::string href =ctx.req->getUrlRewritingRoot() + "/";
        href += Project::urlNameEncode(e->issue->project) + "/issues/";
        href += urlEncode(e->issue->id);
        href += "?display=properties_changes#" + urlEncode(e->id);

        ctx.req->printf("<td class=\"sm_entries\"><a href=\"%s\">%s", href.c_str(), htmlEscape(e->issue->id).c_str());
        // print if the issue is newly created by this entry
        if (e->issue->first && e->issue->first->id == e->id) ctx.req->printf("*"); // '*' to denote new issue

        ctx.req->printf("</a></td>\n"); // end of issue id

        // ctime
        ctx.req->printf("<td class=\"sm_entries\">%s</td>\n", htmlEscape(epochToStringDelta(e->ctime)).c_str());

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
