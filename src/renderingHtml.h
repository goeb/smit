#ifndef _renderingHtml_h
#define _renderingHtml_h

#include <list>
#include <string>
#include <stdint.h>

#include "mongoose.h"
#include "db.h"
#include "session.h"


/** SmitData is used to manage the values in the HTML
  * The HTML items marked with class "smit_data" will have
  * their value replaced by client-side scripting.
  *
  */
class ContextParameters {
public:
    ContextParameters(User u, const Project &p);
    ContextParameters(User u);
    const Project &getProject() const;

    std::string username;
    enum Role userRole;
    int numberOfIssues; // TODO check if used
    std::string search;
    std::string sort;
    std::list<std::string> filterin;
    std::list<std::string> filterout;
    const Project *project;
    std::list<std::pair<std::string, uint8_t> > htmlFieldDisplay;

};

class RHtml {
public:
    static void printHeader(struct mg_connection *conn, const std::string &projectPath);
    static void printFooter(struct mg_connection *conn, const std::string &projectPath);
    static void printGlobalNavigation(struct mg_connection *conn, const ContextParameters &ctx);
    static void printNavigationBar(struct mg_connection *conn, const ContextParameters &ctx, bool autofocus);

    static void printPageProjectList(struct mg_connection *conn, const ContextParameters &ctx, const std::list<std::pair<std::string, std::string> > &pList);
    static void printProjectConfig(struct mg_connection *conn, const ContextParameters &ctx);
    static void printPageIssueList(struct mg_connection *conn, const ContextParameters &ctx, std::list<Issue*> issueList, std::list<std::string> colspec);
    static void printPageIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries);
    static void printNewIssuePage(struct mg_connection *conn, const ContextParameters &ctx);

    static bool inList(const std::list<std::string> &listOfValues, const std::string &value);
    static void printIssueForm(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, bool autofocus);
    static void printSigninPage(struct mg_connection *conn, const char *redirect);


};



#endif
