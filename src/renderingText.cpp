
#include "renderingText.h"
#include "db.h"
#include "logging.h"

void RText::printProjectList(struct mg_connection *conn, const std::list<std::string> &pList)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    std::list<std::string>::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        mg_printf(conn, "%s\n", p->c_str());

    }
}


void RText::printIssueList(struct mg_connection *conn, std::list<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    // TODO use colspec
    // TODO sorting

    std::list<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {
        mg_printf(conn, "%s, ", (*i)->id.c_str());
        mg_printf(conn, "%d, ", (*i)->ctime);
        mg_printf(conn, "%d, ", (*i)->mtime);

        std::map<std::string, std::list<std::string> >::iterator p;
        for (p=(*i)->properties.begin(); p!=(*i)->properties.end(); p++) {
            std::list<std::string> values = p->second;
            std::list<std::string>::iterator v;
            for (v=values.begin(); v!=values.end(); v++) {
                mg_printf(conn, "%s+", v->c_str());
            }
            mg_printf(conn, ", ");
        }
        mg_printf(conn, "\n");
    }

    mg_printf(conn, "%u issues\n", issueList.size());
}

void RText::printIssue(struct mg_connection *conn, const Issue &issue, const std::list<Entry*> &Entries)
{
    LOG_DEBUG("RText::printIssue...");
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

}
