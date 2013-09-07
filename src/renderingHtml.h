#ifndef _renderingHtml_h
#define _renderingHtml_h

#include <list>
#include <string>

#include "mongoose.h"
#include "db.h"

class RHtml {
public:
    static void printHeader(struct mg_connection *conn, const char *project);
    static void printFooter(struct mg_connection *conn, const char *project);

    static void printProjectList(struct mg_connection *conn, const std::list<std::string> &pList);
    static void printIssueList(struct mg_connection *conn, const char *project, std::list<Issue*> issueList, std::list<std::string> colspec);
    static void printIssue(struct mg_connection *conn, const char *project, const Issue &issue, const std::list<Entry*> &entries);

};

/** SmitData is used to manage the values in the HTML
  * The HTML items marked with class "smit_data" will have
  * their value replaced by client-side scripting.
  *
  */
class ContextParameters {
public:
    ContextParameters(std::string _project, std::string _username, int _numberOfIssues);
    void printSmitData(struct mg_connection *conn);
private:
    std::string username;
    int numberOfIssues;
    std::string project;

};


#endif
