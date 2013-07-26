
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
    if (r >= 0) mg_printf(conn, "%s", data);
    else {
        LOG_ERROR("Could not load header.html for project %s", project);
    }

}
void RHtml::printFooter(struct mg_connection *conn, const char *project)
{
    std::string path;
    path = Database::Db.pathToRepository + "/" + project + "/html/footer.html";
    unsigned char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) mg_printf(conn, "%s", data);
    else {
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


void RHtml::printIssueList(struct mg_connection *conn, const char *project, std::list<struct Issue*> issueList, const char *colspec)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, project);

    // TODO use colspec
    // TODO sorting
    mg_printf(conn, "<pre>");

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
    mg_printf(conn, "</pre>");
    printFooter(conn, project);

}
