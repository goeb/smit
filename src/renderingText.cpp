
#include "renderingText.h"
#include "db.h"


void RText::printProjectList(struct mg_connection *conn, const std::list<std::string> &pList)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    std::list<std::string>::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        mg_printf(conn, "%s\n", p->c_str());

    }
}


void RText::printIssueList(struct mg_connection *conn, std::list<struct Issue*> issueList, std::list<ustring> colspec)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    // TODO use colspec
    // TODO sorting

    std::list<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {
        mg_printf(conn, "%s, ", (*i)->id.c_str());
        mg_printf(conn, "%d, ", (*i)->ctime);
        mg_printf(conn, "%d, ", (*i)->mtime);

        std::map<ustring, ustring>::iterator p;
        for (p=(*i)->singleProperties.begin(); p!=(*i)->singleProperties.end(); p++) {
            mg_printf(conn, "%s, ", p->second.c_str());
        }
        std::map<ustring, std::list<ustring> >::iterator mp;
        for (mp=(*i)->multiProperties.begin(); mp!=(*i)->multiProperties.end(); mp++) {
            std::list<ustring> values = mp->second;
            std::list<ustring>::iterator v;
            for (v=values.begin(); v!=values.end(); v++) {
                mg_printf(conn, "%s+", v->c_str());
            }
            mg_printf(conn, ", ");
        }
        mg_printf(conn, "\n");
    }

    mg_printf(conn, "%d issues\n", issueList.size());
}
