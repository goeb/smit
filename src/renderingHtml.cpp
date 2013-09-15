#include <stdlib.h>
#include <sstream>

#include "renderingHtml.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"


ContextParameters::ContextParameters(std::string u, int n, const Project &p) : project(p)
{
    username = u;
    numberOfIssues = n;
}

void ContextParameters::printSmitData(struct mg_connection *conn)
{
    mg_printf(conn, "%s", "<script id=\"sm_data\" type=\"application/json\">\n{");
    mg_printf(conn, "\"sm_user\": \"%s\"", username.c_str());
    mg_printf(conn, ", \"sm_numberOfIssues\": \"%d\"", numberOfIssues);
    mg_printf(conn, "%s", "}\n</script>");
}


void RHtml::printHeader(struct mg_connection *conn, const std::string &projectPath)
{
    std::string path = projectPath + "/html/header.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        free(data);
    } else {
        LOG_ERROR("Could not load header.html for project %s", projectPath.c_str());
    }

}
void RHtml::printFooter(struct mg_connection *conn, const std::string &projectPath)
{
    std::string path = projectPath + "/html/footer.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        free(data);
    } else {
        LOG_ERROR("Could not load footer.html for project %s", projectPath.c_str());
    }
}


void RHtml::printProjectList(struct mg_connection *conn, const std::list<std::string> &pList)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::list<std::string>::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        mg_printf(conn, "%s\n", p->c_str());

    }
}


void RHtml::printIssueList(struct mg_connection *conn, const ContextParameters &ctx,
                           std::list<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.getPath());

    // TODO use colspec
    // TODO sorting
    mg_printf(conn, "<table class=\"table_issues\">\n");

    // print header of the table
    mg_printf(conn, "<tr class=\"tr_issues\">\n");
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        mg_printf(conn, "<th class=\"th_issues\">%s</th>\n", (*colname).c_str());

    }
    mg_printf(conn, "</tr>\n");

    std::list<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        mg_printf(conn, "<tr class=\"tr_issues\">\n");

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::ostringstream text;
            std::string column = *c;

            if (column == "id") text << (*i)->id.c_str();
            else if (column == "ctime") text << (*i)->ctime;
            else if (column == "mtime") text << (*i)->mtime;
            else {
                // look if it is a single property
                std::map<std::string, std::list<std::string> >::iterator p;
                std::map<std::string, std::list<std::string> > & properties = (*i)->properties;

                p = properties.find(column);
                if (p != properties.end()) text << toString(p->second);
            }
            // add href if column is 'id' or 'title'
            std::string href_lhs = "";
            std::string href_rhs = "";
            if ( (column == "id") || (column == "title") ) {
                href_lhs = "<a href=\"";
                href_lhs = href_lhs + "/" + ctx.project.getName() + "/issues/";
                href_lhs = href_lhs + (char*)(*i)->id.c_str() + "\">";
                href_rhs = "</a>";
            }

            mg_printf(conn, "<td class=\"td_issues\">%s%s%s</td>\n", href_lhs.c_str(), text.str().c_str(), href_rhs.c_str());


        }
        mg_printf(conn, "</tr>\n");
    }
    mg_printf(conn, "</table>\n");
    mg_printf(conn, "%d issues\n", issueList.size());
    printFooter(conn, ctx.project.getName().c_str());

}

/** Convert to a string
  */
std::string RHtml::toString(const std::list<std::string> &values)
{
    std::ostringstream text;
    std::list<std::string>::const_iterator v;
    for (v=values.begin(); v!=values.end(); v++) {
        if (v != values.begin()) text << ", ";
        text << v->c_str();
    }
    return text.str();
}

bool RHtml::inList(const std::list<std::string> &listOfValues, const std::string &value)
{
    std::list<std::string>::const_iterator v;
    for (v=listOfValues.begin(); v!=listOfValues.end(); v++) if (*v == value) return true;

    return false;

}


// Example: replaceAll(in, '"', "&quot;")
// Replace all " by &quot;
std::string replaceHtmlEntity(const std::string &in, char c, const char *replaceBy)
{
    std::string out = in;
    size_t pos = 0;
    while ((pos = out.find(c)) != std::string::npos) {
        out = out.replace(pos, 1, replaceBy);
    }
    return out;
}

std::string htmlEscape(const std::string &value)
{
    std::string result = replaceHtmlEntity(value, '&', "&amp;");
    result = replaceHtmlEntity(result, '"', "&quot;");
    result = replaceHtmlEntity(result, '<', "&lt;");
    result = replaceHtmlEntity(result, '>', "&gt;");
    result = replaceHtmlEntity(result, '\'', "&apos;");
    return result;
}


void RHtml::printIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    LOG_DEBUG("printIssue...");

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.getPath().c_str());

    mg_printf(conn, "<div class=\"sm_issue\">");

    // issue header
    // print id and title
    mg_printf(conn, "<div class=\"sm_issue_header\">\n");
    mg_printf(conn, "<span class=\"sm_issue_id\">%s</span>\n", issue.id.c_str());
    mg_printf(conn, "<span class=\"sm_issue_title\">%s</span>\n", htmlEscape(issue.getTitle()).c_str());
    mg_printf(conn, "</div>\n");

    // issue summary
    // print the fields of the issue in a two-column table
    mg_printf(conn, "<table class=\"sm_fields_summary\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;

    std::list<std::string> orderedFields = ctx.project.getConfig().orderedFields;

    std::list<std::string>::const_iterator f;
    for (f=orderedFields.begin(); f!=orderedFields.end(); f++) {
        std::string fname = *f;
        std::string value;
        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(fname);

        if (p != issue.properties.end()) value = toString(p->second);

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }
        mg_printf(conn, "<td class=\"sm_flabel sm_flabel_%s\">%s: </td>\n", fname.c_str(), fname.c_str());
        mg_printf(conn, "<td class=\"sm_fvalue sm_fvalue_%s\">%s</td>\n", fname.c_str(), htmlEscape(value).c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }
    mg_printf(conn, "</table>\n");


    // print entries
    std::list<Entry*>::const_iterator e;
    for (e = entries.begin(); e != entries.end(); e++) {
        Entry ee = *(*e);
        mg_printf(conn, "<div class=\"sm_entry\">\n");

        mg_printf(conn, "<div class=\"sm_entry_header\">\n");
        mg_printf(conn, "Author: <span class=\"sm_entry_author\">%s</span>", htmlEscape(ee.author).c_str());
        struct tm *tmp;
        tmp = localtime(&ee.ctime);
        char datetime[100+1];
        strftime(datetime, 100, "%Y-%m-%d %H:%M:%S", tmp);
        mg_printf(conn, " / <span class=\"sm_entry_ctime\">%s</span>\n", datetime);
        // conversion date en javascript
        // document.write(new Date(%d)).toString());
        mg_printf(conn, "</div>\n"); // end header

        mg_printf(conn, "<div class=\"sm_entry_message\">\n");
        std::map<std::string, std::list<std::string> >::iterator m = ee.properties.find("message");
        if (m != ee.properties.end()) {
            if (m->second.size() != 0) {
                mg_printf(conn, "%s\n", htmlEscape(m->second.front()).c_str());
            }
        }
        mg_printf(conn, "</div>\n"); // end message

        // other fields
        mg_printf(conn, "<div class=\"sm_entry_other_fields\">\n");
        mg_printf(conn, "");
        mg_printf(conn, "</div>\n"); // other fields

        mg_printf(conn, "</div>\n");

    }


    printIssueForm(conn, ctx, issue);
    printFooter(conn, ctx.project.getPath().c_str());
}


void RHtml::printNewIssuePage(struct mg_connection *conn, const ContextParameters &ctx)
{
    LOG_DEBUG("printNewPage...");

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.getPath().c_str());

    mg_printf(conn, "<div class=\"sm_issue\">");

    Issue issue;
    printIssueForm(conn, ctx, issue);
    printFooter(conn, ctx.project.getPath().c_str());
}


/** print form for adding a message / modifying the issue
  */
void RHtml::printIssueForm(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue)
{
    // TODO if access rights granted

    // enctype=\"multipart/form-data\"
    mg_printf(conn, "<form method=\"post\"  class=\"sm_issue_form\">");
    // print the fields of the issue in a two-column table

    // title
    mg_printf(conn, "<span class=\"sm_flabel sm_flabel_title\">title:</span>");
    mg_printf(conn, "<input class=\"sm_finput_title\" type=\"text\" name=\"title\" value=\"%s\">",
              htmlEscape(issue.getTitle()).c_str());

    mg_printf(conn, "<table class=\"sm_fields_summary\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;
    std::list<std::string>::const_iterator f;

    // debug
    std::list<std::string> orderedFields = ctx.project.getConfig().orderedFields;
    std::list<std::string>::iterator i;
    for (i=orderedFields.begin(); i!=orderedFields.end(); i++) {
        LOG_DEBUG("orderedFields(2): %s", i->c_str());
    }

    std::map<std::string, FieldSpec> fields = ctx.project.getConfig().fields;


    for (f=orderedFields.begin(); f!=orderedFields.end(); f++) {
        std::string fname = *f;

        std::map<std::string, FieldSpec>::const_iterator fieldSpec = fields.find(fname);
        if (fieldSpec == fields.end()) {
            LOG_ERROR("Field '%s' (of setHtmlFieldDisplay) not found in addField options", fname.c_str());
            continue;
        }

        FieldSpec fspec = fieldSpec->second;

        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(fname);
        std::list<std::string> propertyValues;
        if (p!=issue.properties.end()) propertyValues = p->second;

        std::ostringstream input;
        std::string value;

        if (fspec.type == F_TEXT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<input class=\"sm_finput_" << fname << "\" type=\"text\" name=\""
                  << fname << "\" value=\"" << htmlEscape(value) << "\">\n";

        } else if (fspec.type == F_SELECT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_finput_" << fname << "\" name=\"" << fname << "\">";

            for (so = fspec.selectOptions.begin(); so != fspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (value == *so) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (fspec.type == F_MULTISELECT) {
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_finput_" << fname << "\" name=\"" << fname << "\"";
            if (fspec.type == F_MULTISELECT) input << " multiple=\"multiple\"";
            input << ">";

            for (so = fspec.selectOptions.begin(); so != fspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (inList(propertyValues, *so)) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (fspec.type == F_SELECT_USER) {
            if (propertyValues.size()>0) value = propertyValues.front();
            std::list<std::string>::iterator u;
            input << "<select class=\"sm_finput_" << fname << "\" name=\"" << fname << "\">";

            // TODO
            std::list<std::string> users;
            users.push_back("John");
            users.push_back("Fred");
            users.push_back("Alice");
            users.push_back("David G. Smith");
            for (u = users.begin(); u != users.end(); u++) {
                input << "<option" ;
                if (value == *u) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*u) << "</option>";
            }

            input << "</select>";


        } else {
            LOG_ERROR("invalid fieldSpec->type=%d", fspec.type);
            continue;
        }

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }
        mg_printf(conn, "<td class=\"sm_flabel sm_flabel_%s\">%s: </td>\n", fname.c_str(), fname.c_str());
        mg_printf(conn, "<td class=\"sm_finput\">%s</td>\n", input.str().c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }

    mg_printf(conn, "<td class=\"sm_flabel sm_flabel_message\" >message: </td>\n");
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<textarea class=\"sm_finput sm_finput_message\" placeholder=\"%s\" name=\"message\">\n", "Enter a message");
    mg_printf(conn, "</textarea>\n");
    mg_printf(conn, "</td>\n");
    mg_printf(conn, "</table>\n");

    mg_printf(conn, "<input type=\"submit\" value=\"%s\">\n", "Add Message");

    mg_printf(conn, "</form>");

    mg_printf(conn, "</div>");
}
