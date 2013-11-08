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


ContextParameters::ContextParameters(User u, const Project &p) : project(&p)
{
    username = u.username;
    userRole = u.getRole(p.getName());
}

ContextParameters::ContextParameters(User u)
{
    username = u.username;
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
#define K_SM_RAW_PROJECT_NAME "SM_RAW_PROJECT_NAME"
#define K_SM_RAW_ISSUE_ID "SM_RAW_ISSUE_ID"
#define K_SM_SCRIPT_UPDATE_CONFIG "SM_SCRIPT_UPDATE_CONFIG"
#define K_SM_DIV_PREDEFINED_VIEWS "SM_DIV_PREDEFINED_VIEWS"
#define K_SM_DIV_PROJECTS "SM_DIV_PROJECTS"
#define K_SM_DIV_ISSUES "SM_DIV_ISSUES"
#define K_SM_DIV_ISSUE "SM_DIV_ISSUE"

class VariableNavigator {
public:
    VariableNavigator(const char *data, size_t len) {
        buffer = data;
        size = len;
        dumpStart = buffer;
        dumpEnd = buffer;
        searchFromHere = buffer;
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

private:
    const char *buffer;
    size_t size;
    const char * dumpStart;
    const char * dumpEnd;
    const char * searchFromHere;

};

void RHtml::printPageSignin(struct mg_connection *conn, const char *redirect)
{

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::string path = Database::Db.getRootDir() + "/public/signin.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        // add javascript for updating the redirect URL
        mg_printf(conn, "<script>document.getElementById(\"redirect\").value = \"%s\"</script>", redirect);
        free(data);
    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
}


std::string htmlEscape(const std::string &value)
{
    std::string result = replaceAll(value, '&', "&amp;");
    result = replaceAll(result, '"', "&quot;");
    result = replaceAll(result, '<', "&lt;");
    result = replaceAll(result, '>', "&gt;");
    result = replaceAll(result, '\'', "&apos;");
    return result;
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
void RHtml::printNavigationGlobal(struct mg_connection *conn, const ContextParameters &ctx)
{
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
        linkToModify.addAttribute("href", "/%s/config", htmlEscape(ctx.project->getName()).c_str());
        linkToModify.addContents("%s", _("Project configuration"));
        div.addContents(" ");
        div.addContents(linkToModify);
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
void RHtml::printNavigationIssues(struct mg_connection *conn, const ContextParameters &ctx, bool autofocus)
{
    HtmlNode div("div");
    div.addAttribute("class", "sm_navigation_project");
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/new", htmlEscape(ctx.project->getName()).c_str());
        a.addAttribute("class", "sm_link_new_issue");
        a.addContents("%s", _("Create new issue"));
        div.addContents(a);
    }

    std::map<std::string, PredefinedView>::iterator pv;
    ProjectConfig config = ctx.project->getConfig();
    FOREACH (pv, config.predefinedViews) {
        HtmlNode a("a");
        a.addAttribute("href", "/%s/issues/?%s", htmlEscape(ctx.project->getName()).c_str(),
                       makeQueryString(pv->second).c_str());
        a.addAttribute("class", "sm_predefined_view");
        a.addContents("%s", pv->first.c_str());
        div.addContents(a);
    }

    HtmlNode form("form");
    form.addAttribute("class", "sm_searchbox");
    form.addAttribute("action", "/%s/issues", htmlEscape(ctx.project->getName()).c_str());
    form.addAttribute("method", "get");

    HtmlNode input("input");
    input.addAttribute("class", "sm_searchbox");
    input.addAttribute("type", "text");
    input.addAttribute("name", "search");
    if (autofocus) input.addAttribute("autofocus", "autofocus");
    form.addContents(input);
    div.addContents(form);
    div.print(conn);
}


void printProjects(struct mg_connection *conn, const std::list<std::pair<std::string, std::string> > &pList)
{
    std::list<std::pair<std::string, std::string> >::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        std::string pname = htmlEscape(p->first.c_str());
        mg_printf(conn, "<div class=\"sm_link_project\"><a href=\"/%s/issues/\">%s</a> (%s)",
                  pname.c_str(), pname.c_str(), _(p->second.c_str()));
        if (p->second == "admin") mg_printf(conn, " <a href=\"%s/config\">edit</a>", pname.c_str());
        mg_printf(conn, "</div>\n");
    }
}

void RHtml::printPageProjectList(struct mg_connection *conn, const ContextParameters &ctx, const std::list<std::pair<std::string, std::string> > &pList)
{

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::string path = Database::Db.getRootDir() + "/public/projects.html";
    char *data;
    int n = loadFile(path.c_str(), &data);
    if (n >= 0) {
        VariableNavigator vn(data, n);
        while (1) {
            std::string varname = vn.getNextVariable();
            vn.dumpPrevious(conn);
            if (varname.empty()) break;

            if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                printNavigationGlobal(conn, ctx);

            } else if (varname == K_SM_DIV_PROJECTS) {
                printProjects(conn, pList);

            } else {
                // unknown variable name
                mg_printf(conn, "%s", varname.c_str());
            }
        }

    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
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

void printLinksToPredefinedViews(struct mg_connection *conn, const ContextParameters &ctx)
{
    ProjectConfig c = ctx.getProject().getConfig();
    std::map<std::string, PredefinedView>::iterator pv;
    FOREACH(pv, c.predefinedViews) {
        mg_printf(conn, "<a href=\"views\">%s</a><br>\n", htmlEscape(pv->first).c_str());
    }
}

void printScriptUpdateConfig(struct mg_connection *conn, const ContextParameters &ctx)
{
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
    std::map<std::string, PropertySpec>::iterator p;
    FOREACH(p, c.properties) {
        PropertySpec pspec = p->second;
        const char *type = "";
        switch (pspec.type) {
        case F_TEXT: type = "text"; break;
        case F_SELECT: type = "select"; break;
        case F_MULTISELECT: type = "multiselect"; break;
        case F_SELECT_USER: type = "selectUser"; break;
        }

        std::string property = replaceAll(ctx.getProject().getLabelOfProperty(p->first), '\'', "\\\'");
        std::string value = replaceAll(toString(pspec.selectOptions, "\\n"), '\'', "\\\'");

        mg_printf(conn, "addProperty('%s', '%s', '%s', '%s');\n", p->first.c_str(),
                  property.c_str(),
                  type, value.c_str());

    }

    // add 3 more empty properties
    mg_printf(conn, "addProperty('', '', '', '');\n");
    mg_printf(conn, "addProperty('', '', '', '');\n");
    mg_printf(conn, "addProperty('', '', '', '');\n");

    mg_printf(conn, "replaceContentInContainer();\n");
    mg_printf(conn, "</script>\n");
}


void RHtml::printProjectConfig(struct mg_connection *conn, const ContextParameters &ctx)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::string path = Database::Db.getRootDir() + "/public/pconfig.html";
    char *data;
    int n = loadFile(path.c_str(), &data);
    if (n >= 0) {
        VariableNavigator vn(data, n);
        while (1) {
            std::string varname = vn.getNextVariable();
            vn.dumpPrevious(conn);
            if (varname.empty()) break;

            if (varname == K_SM_RAW_PROJECT_NAME) {
                mg_printf(conn, "%s", htmlEscape(ctx.project->getName()).c_str());

            } else if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                printNavigationGlobal(conn, ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES) {
                printNavigationIssues(conn, ctx, false);

            } else if (varname == K_SM_SCRIPT_UPDATE_CONFIG) {
                printScriptUpdateConfig(conn, ctx);

            } else if (varname == K_SM_DIV_PREDEFINED_VIEWS) {
                printLinksToPredefinedViews(conn, ctx);
            } else {
                // unknown variable name
                mg_printf(conn, "%s", varname.c_str());
            }
        }

    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
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

void printIssueList(struct mg_connection *conn, const ContextParameters &ctx,
                    std::vector<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "<div class=\"sm_issues\">\n");

    // print chosen filters and search parameters
    if (!ctx.search.empty() || !ctx.filterin.empty() || !ctx.filterout.empty()) {
        mg_printf(conn, "<div class=\"sm_view_summary\">");
        if (!ctx.search.empty()) mg_printf(conn, "search: %s<br>", ctx.search.c_str());
        if (!ctx.filterin.empty()) mg_printf(conn, "filterin: %s<br>", toString(ctx.filterin).c_str());
        if (!ctx.filterout.empty()) mg_printf(conn, "filterout: %s", toString(ctx.filterout).c_str());
        mg_printf(conn, "</div>");
    }
    mg_printf(conn, "<div class=\"sm_issues_count\">%s: <span class=\"sm_number_of_issues\">%d</span></div>\n",
              _("Issues found"), issueList.size());


    std::string group = getPropertyForGrouping(ctx.project->getConfig(), ctx.sort);
    std::string currentGroup;

    mg_printf(conn, "<table class=\"sm_issues_table\">\n");

    // print header of the table
    mg_printf(conn, "<tr class=\"sm_issues_tr\">\n");
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {

        std::string label = ctx.getProject().getLabelOfProperty(*colname);
        std::string newQueryString = getNewSortingSpec(conn, *colname, true);
        mg_printf(conn, "<th class=\"sm_issues_th\"><a class=\"sm_sort_exclusive\" href=\"?%s\" title=\"Sort ascending\">%s</a>\n",
                  newQueryString.c_str(), label.c_str());
        newQueryString = getNewSortingSpec(conn, *colname, false);
        mg_printf(conn, "\n<br><a href=\"?%s\" class=\"sm_sort_accumulate\" ", newQueryString.c_str());
        mg_printf(conn, "title=\"Sort while preserving order of other columns\n(or invert current column if already sorted-by)\">&gt;&gt;&gt;</a></th>\n");
    }
    mg_printf(conn, "</tr>\n");

    std::vector<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        if (! group.empty() &&
            (i == issueList.begin() || getProperty((*i)->properties, group) != currentGroup) ) {
                // insert group bar if relevant
            mg_printf(conn, "<tr class=\"sm_group\">\n");
            currentGroup = getProperty((*i)->properties, group);
            mg_printf(conn, "<td class=\"sm_group\" colspan=\"%d\">%s</td>\n", colspec.size(), htmlEscape(currentGroup).c_str());
            mg_printf(conn, "</tr>\n");
        }

        mg_printf(conn, "<tr class=\"tr_issues\">\n");

        std::list<std::string>::iterator c;
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
                href_lhs = href_lhs + "/" + ctx.getProject().getName() + "/issues/";
                href_lhs = href_lhs + (char*)(*i)->id.c_str() + "\">";
                href_rhs = "</a>";
            }

            mg_printf(conn, "<td class=\"sm_issues_td\">%s%s%s</td>\n", href_lhs.c_str(), text.str().c_str(), href_rhs.c_str());


        }
        mg_printf(conn, "</tr>\n");
    }
    mg_printf(conn, "</table>\n");
    mg_printf(conn, "</div\n");

}

void RHtml::printPageIssueList(struct mg_connection *conn, const ContextParameters &ctx,
                           std::vector<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");


    std::string path = Database::Db.getRootDir() + "/public/issues.html";
    char *data;
    int n = loadFile(path.c_str(), &data);
    if (n >= 0) {
        VariableNavigator vn(data, n);
        while (1) {
            std::string varname = vn.getNextVariable();
            vn.dumpPrevious(conn);
            if (varname.empty()) break;

            if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                printNavigationGlobal(conn, ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES) {
                printNavigationIssues(conn, ctx, true);

            } else if (varname == K_SM_DIV_ISSUES) {
                printIssueList(conn, ctx, issueList, colspec);

            } else if (varname == K_SM_RAW_PROJECT_NAME) {
                mg_printf(conn, "%s", htmlEscape(ctx.project->getName()).c_str());
            } else {
                // unknown variable name
                mg_printf(conn, "%s", varname.c_str());
            }
        }

    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
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
                i += sizeEnd-1;
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
        } else if ( (i <= len-sizeBeginning) && (0 == strncmp(begin, in.c_str()+i, sizeBeginning)) ) {
            // beginning of new block
            insideBlock = true;
            block = i;

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

void printIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    mg_printf(conn, "<div class=\"sm_issue\">");

    // issue header
    // -------------------------------------------------
    // print id and summary
    mg_printf(conn, "<div class=\"sm_issue_header\">\n");
    mg_printf(conn, "<span class=\"sm_issue_id\">%s</span>\n", issue.id.c_str());
    mg_printf(conn, "<span class=\"sm_issue_summary\">%s</span>\n", htmlEscape(issue.getSummary()).c_str());
    mg_printf(conn, "</div>\n");

    // issue summary
    // -------------------------------------------------
    // print the fields of the issue in a two-column table
    mg_printf(conn, "<table class=\"sm_properties\">");
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
        mg_printf(conn, "<td class=\"sm_plabel sm_plabel_%s\">%s: </td>\n", pname.c_str(), label.c_str());
        mg_printf(conn, "<td class=\"sm_pvalue sm_pvalue_%s\">%s</td>\n", pname.c_str(), htmlEscape(value).c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }
    mg_printf(conn, "</table>\n");

    // add a link to edit form if role enables it
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
        mg_printf(conn, "<div class=\"sm_link_edit_form\"><a href=\"#edit_form\">%s</a></div>", _("Add message / Edit properties"));
    }

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
            mg_printf(conn, "<a href=\"#\" class=\"sm_delete\" title=\"Delete this entry (at most %d minutes after posting)\" ", (DELETE_DELAY_S/60));
            mg_printf(conn, " onclick=\"deleteEntry('/%s/entries', '%s');return false;\">\n", ctx.getProject().getName().c_str(), ee.id.c_str());
            mg_printf(conn, "&#10008; delete");
            mg_printf(conn, "</a>\n");
        }

        // link to raw entry
        mg_printf(conn, "(<a href=\"/%s/entries/%s/%s\" class=\"sm_raw_entry\">%s</a>)\n",
                  htmlEscape(ctx.getProject().getName()).c_str(),
                  issue.id.c_str(), ee.id.c_str(), _("raw"));


        mg_printf(conn, "</div>\n"); // end header

        mg_printf(conn, "<div class=\"sm_entry_message\">");
        std::string m = ee.getMessage();
        if (! m.empty()) {
            mg_printf(conn, "%s\n", convertToRichText(htmlEscape(m)).c_str());
        }
        mg_printf(conn, "</div>\n"); // end message


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

        FOREACH(f, orderedProperties) {
            std::string pname = *f;
//            if (pname == K_MESSAGE) continue; // already processed

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
            mg_printf(conn, "<div class=\"sm_other_properties\">\n");
            mg_printf(conn, "%s", otherProperties.str().c_str());
            mg_printf(conn, "</div>\n");
        }

        mg_printf(conn, "</div>\n"); // end entry

    } // end of entries

    // print the form
    // -------------------------------------------------
    if (ctx.userRole == ROLE_ADMIN || ctx.userRole == ROLE_RW) {
        RHtml::printIssueForm(conn, ctx, issue, false);
    }
}

void RHtml::printPageIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    std::string path = Database::Db.getRootDir() + "/public/issue.html";
    char *data;
    int n = loadFile(path.c_str(), &data);
    if (n >= 0) {
        VariableNavigator vn(data, n);
        while (1) {
            std::string varname = vn.getNextVariable();

            vn.dumpPrevious(conn);

            if (varname.empty()) break;

            if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                printNavigationGlobal(conn, ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES) {
                printNavigationIssues(conn, ctx, false);

            } else if (varname == K_SM_RAW_ISSUE_ID) {
                mg_printf(conn, "%s", issue.id.c_str());

            } else if (varname == K_SM_DIV_ISSUE) {
                printIssue(conn, ctx, issue, entries);

            } else if (varname == K_SM_RAW_PROJECT_NAME) {
                mg_printf(conn, "%s", htmlEscape(ctx.project->getName()).c_str());

            } else {
                // unknown variable name
                mg_printf(conn, "%s", varname.c_str());
            }
        }

    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
}



void RHtml::printPageNewIssue(struct mg_connection *conn, const ContextParameters &ctx)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    std::string path = Database::Db.getRootDir() + "/public/newIssue.html";
    char *data;
    int n = loadFile(path.c_str(), &data);
    if (n >= 0) {
        VariableNavigator vn(data, n);
        while (1) {
            std::string varname = vn.getNextVariable();

            vn.dumpPrevious(conn);

            if (varname.empty()) break;

            if (varname == K_SM_DIV_NAVIGATION_GLOBAL) {
                printNavigationGlobal(conn, ctx);

            } else if (varname == K_SM_DIV_NAVIGATION_ISSUES) {
                printNavigationIssues(conn, ctx, false);

            } else if (varname == K_SM_DIV_ISSUE) {
                Issue issue;
                printIssueForm(conn, ctx, issue, true);

            } else if (varname == K_SM_RAW_PROJECT_NAME) {
                mg_printf(conn, "%s", htmlEscape(ctx.project->getName()).c_str());

            } else {
                // unknown variable name
                mg_printf(conn, "%s", varname.c_str());
            }
        }

    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
}


/** print form for adding a message / modifying the issue
  */
void RHtml::printIssueForm(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, bool autofocus)
{
    // TODO if access rights granted

    // enctype=\"multipart/form-data\"
    mg_printf(conn, "<form method=\"post\"  class=\"sm_issue_form\" id=\"edit_form\">");
    // print the fields of the issue in a two-column table

    // The form is made over a table with 4 columns.
    // each row is made of 1 label, 1 input, 1 label, 1 input (4 columns)
    // except for the summary.
    // summary
    mg_printf(conn, "<table class=\"sm_properties\">");
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_plabel sm_plabel_summary\">%s: </td>\n", ctx.getProject().getLabelOfProperty("summary").c_str());
    mg_printf(conn, "<td class=\"sm_pinput\" colspan=\"3\">");

    mg_printf(conn, "<input class=\"sm_pinput_summary\" required=\"required\" type=\"text\" name=\"summary\" value=\"%s\"",
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
            input << "<select class=\"sm_pinput_" << pname << "\" name=\"" << pname << "\">";

            for (so = pspec.selectOptions.begin(); so != pspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (value == *so) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (pspec.type == F_MULTISELECT) {
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_pinput_" << pname << "\" name=\"" << pname << "\"";
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
            input << "<select class=\"sm_pinput_" << pname << "\" name=\"" << pname << "\">";

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
        mg_printf(conn, "<td class=\"sm_plabel sm_plabel_%s\">%s: </td>\n", pname.c_str(), label.c_str());
        mg_printf(conn, "<td class=\"sm_pinput\">%s</td>\n", input.str().c_str());

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
    mg_printf(conn, "<td class=\"sm_plabel sm_plabel_message\" >%s: </td>\n", ctx.getProject().getLabelOfProperty(K_MESSAGE).c_str());
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<textarea class=\"sm_pinput sm_pinput_message\" placeholder=\"%s\" name=\"%s\" wrap=\"hard\" cols=\"80\">\n",
              "Enter a message", K_MESSAGE); // remove the '+' of K_MESSAGE
    mg_printf(conn, "</textarea>\n");
    mg_printf(conn, "</td></tr>\n");

    // check box "enable long lines"
    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td class=\"sm_longlines\" colspan=\"3\">\n");
    mg_printf(conn, "<label><input type=\"checkbox\" onclick=\"changeWrapping();\">\n");
    mg_printf(conn, "%s\n", ctx.getProject().getLabelOfProperty("long-line-break-message").c_str());
    mg_printf(conn, "</label></td></tr>\n");

    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<input type=\"submit\" value=\"%s\">\n", ctx.getProject().getLabelOfProperty("Add-Message").c_str());
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "</table>\n");

    mg_printf(conn, "</form>");

    mg_printf(conn, "</div>");
}
