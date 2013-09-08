#include <stdlib.h>
#include <sstream>

#include "renderingHtml.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"


ContextParameters::ContextParameters(std::string p, std::string u, int n, std::list<std::pair<std::string, uint8_t> > h)
{
    project = p;
    username = u;
    numberOfIssues = n;
    htmlFieldDisplay = h;
}

void ContextParameters::printSmitData(struct mg_connection *conn)
{
    mg_printf(conn, "%s", "<script id=\"smit_data\" type=\"application/json\">\n{");
    mg_printf(conn, "\"smit_user\": \"%s\"", username.c_str());
    mg_printf(conn, ", \"smit_numberOfIssues\": \"%d\"", numberOfIssues);
    mg_printf(conn, "%s", "}\n</script>");
}


void RHtml::printHeader(struct mg_connection *conn, const char *project)
{
    std::string path;
    path = Database::Db.pathToRepository + "/" + project + "/html/header.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        free(data);
    } else {
        LOG_ERROR("Could not load header.html for project %s", project);
    }

}
void RHtml::printFooter(struct mg_connection *conn, const char *project)
{
    std::string path;
    path = Database::Db.pathToRepository + "/" + project + "/html/footer.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        free(data);
    } else {
        LOG_ERROR("Could not load footer.html for project %s", project);
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


void RHtml::printIssueList(struct mg_connection *conn, const char *project, std::list<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, project);

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
                std::map<std::string, std::string>::iterator p;
                std::map<std::string, std::string> & singleProperties = (*i)->singleProperties;

                p = singleProperties.find(column);
                if (p != singleProperties.end()) text << p->second.c_str();
                else {
                    // look if it is a multi property*
                    std::map<std::string, std::list<std::string> >::iterator mp;
                    std::map<std::string, std::list<std::string> > & multiProperties = (*i)->multiProperties;

                    mp = multiProperties.find(column);
                    if (mp != multiProperties.end()) {
                        std::list<std::string> values = mp->second;
                        std::list<std::string>::iterator v;
                        for (v=values.begin(); v!=values.end(); v++) {
                            if (v != values.begin()) text << ", ";
                            text << v->c_str();
                        }
                    }
                }
            }
            // add href if column is 'id' or 'title'
            std::string href_lhs = "";
            std::string href_rhs = "";
            if ( (column == "id") || (column == "title") ) {
                href_lhs = "<a href=\"";
                href_lhs = href_lhs + "/" + project + "/issues/";
                href_lhs = href_lhs + (char*)(*i)->id.c_str() + "\">";
                href_rhs = "</a>";
            }

            mg_printf(conn, "<td class=\"td_issues\">%s%s%s</td>\n", href_lhs.c_str(), text.str().c_str(), href_rhs.c_str());


        }
        mg_printf(conn, "</tr>\n");
    }
    mg_printf(conn, "</table>\n");
    mg_printf(conn, "%d issues\n", issueList.size());
    printFooter(conn, project);

}


void RHtml::printIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    LOG_DEBUG("printIssue...");

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.c_str());

    // print the fields of the issue in a two-column table
    mg_printf(conn, "<table class=\"smit_fields_summary\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;

    std::list<std::pair<std::string, uint8_t> >::const_iterator h;
    for (h=ctx.htmlFieldDisplay.begin(); h!=ctx.htmlFieldDisplay.end(); h++) {
        std::string key = h->first;
        uint8_t span = h->second;
        if (span > MAX_COLUMNS) {
            LOG_ERROR("span %d overflow. Use 2.", span);
            span = MAX_COLUMNS;
        }
        std::ostringstream value;

        if (key == "mtime") value << issue.mtime;
        else if (key == "id") value << issue.id;
        else {
            std::map<std::string, std::string>::const_iterator p = issue.singleProperties.find(key);
            std::map<std::string, std::list<std::string> >::const_iterator mp = issue.multiProperties.find(key);

            if (p != issue.singleProperties.end()) value << p->second;
            else if (mp != issue.multiProperties.end()) {
                std::list<std::string>::iterator v;
                std::list<std::string> values = mp->second;

                for (v = values.begin(); v != values.end(); v++) {
                    if (v != values.begin()) value << ", ";
                    value << v->c_str();
                }

            }
        }

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>");
        }
        mg_printf(conn, "<td span=\"%d\">", span);
        mg_printf(conn, "<span class=\"smit_field_label\">%s: </span>\n", key.c_str());
        mg_printf(conn, "<span class=\"smit_field_value\">%s</span>\n", value.str().c_str());
        mg_printf(conn, "</td>\n", span);

        workingColumn += span;
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
        mg_printf(conn, "<div class=\"smit_entry\">\n");

        mg_printf(conn, "<div class=\"smit_entryHeader\">\n");
        mg_printf(conn, "Author: <span class=\"smit_entryAuthor\">%s</span>", ee.author.c_str());
        mg_printf(conn, " / <span class=\"smit_entryCtime\">%d</span>\n", ee.ctime); // TODO display human-readable date
        mg_printf(conn, "</div>\n"); // end header

        mg_printf(conn, "<div class=\"smit_entryMessage\">\n");
        mg_printf(conn, "%s\n", ee.message.c_str());
        mg_printf(conn, "</div>\n"); // end message

        // other fields
        mg_printf(conn, "<div class=\"smit_entryOtherFields\">\n");
        mg_printf(conn, "");
        mg_printf(conn, "</div>\n"); // other fields

        mg_printf(conn, "</div>\n");

    }

    printFooter(conn, ctx.project.c_str());

}
