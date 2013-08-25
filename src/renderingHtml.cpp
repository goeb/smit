#include <stdlib.h>
#include <sstream>

#include "renderingHtml.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"


void RHtml::printHeader(struct mg_connection *conn, const char *project)
{
    std::string path;
    path = Database::Db.pathToRepository + "/" + project + "/html/header.html";
    unsigned char *data;
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
    unsigned char *data;
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


void RHtml::printIssueList(struct mg_connection *conn, const char *project, std::list<struct Issue*> issueList, std::list<ustring> colspec)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, project);

    // TODO use colspec
    // TODO sorting
    mg_printf(conn, "<table class=\"table_issues\">\n");

    // print header of the table
    mg_printf(conn, "<tr class=\"tr_issues\">\n");
    std::list<ustring>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        mg_printf(conn, "<th class=\"th_issues\">%s</th>\n", (*colname).c_str());

    }
    mg_printf(conn, "</tr>\n");

    std::list<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        mg_printf(conn, "<tr class=\"tr_issues\">\n");

        std::list<ustring>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::ostringstream text;
            ustring column = *c;

            if (column == (uint8_t*)"id") text << (*i)->id.c_str();
            else if (column == (uint8_t*)"ctime") text << (*i)->ctime;
            else if (column == (uint8_t*)"mtime") text << (*i)->mtime;
            else {
                // look if it is a single property
                std::map<ustring, ustring>::iterator p;
                std::map<ustring, ustring> & singleProperties = (*i)->singleProperties;

                p = singleProperties.find(column);
                if (p != singleProperties.end()) text << p->second.c_str();
                else {
                    // look if it is a multi property*
                    std::map<ustring, std::list<ustring> >::iterator mp;
                    std::map<ustring, std::list<ustring> > & multiProperties = (*i)->multiProperties;

                    mp = multiProperties.find(column);
                    if (mp != multiProperties.end()) {
                        std::list<ustring> values = mp->second;
                        std::list<ustring>::iterator v;
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
            if ( (column == (uint8_t*)"id") || (column == (uint8_t*)"title") ) {
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


void RHtml::printIssue(struct mg_connection *conn, const char *project, const Issue &issue, const std::list<Entry*> &Entries)
{
    LOG_DEBUG("printIssue...");
}
