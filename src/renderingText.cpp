
#include "renderingText.h"
#include "db.h"
#include "logging.h"
#include "stringTools.h"

void RText::printProjectList(struct mg_connection *conn, const std::list<std::pair<std::string, std::string> > &pList)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    std::list<std::pair<std::string, std::string> >::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        mg_printf(conn, "%s (%s)\n", p->first.c_str(), p->second.c_str());

    }
}


void RText::printIssueList(struct mg_connection *conn, std::list<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    // print names of columns
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        mg_printf(conn, "%s,\t", colname->c_str());
   }
    mg_printf(conn, "\n");

    // list of issues
    std::list<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::string text;
            std::string column = *c;

            if (column == "id") text = (*i)->id;
            else if (column == "ctime") text = epochToString((*i)->ctime);
            else if (column == "mtime") text = epochToString((*i)->mtime);
            else {
                std::map<std::string, std::list<std::string> >::iterator p;
                std::map<std::string, std::list<std::string> > & properties = (*i)->properties;

                p = properties.find(column);
                if (p != properties.end()) text = toString(p->second);
            }

            mg_printf(conn, "%s,\t", text.c_str());
        }
        mg_printf(conn, "\n");
    }

}

void RText::printIssue(struct mg_connection *conn, const Issue &issue, const std::list<Entry*> &Entries)
{
    LOG_DEBUG("RText::printIssue...");
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

}
