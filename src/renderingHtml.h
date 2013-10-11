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
    void printSmitData(struct mg_connection *conn);
    inline void setNumberOfIssues(int n) { numberOfIssues = n; }

    std::string username;
    enum Role userRole;
    int numberOfIssues; // TODO check if used
    std::string search;
    std::list<std::string> filterin;
    std::list<std::string> filterout;
    const Project &project;
    std::list<std::pair<std::string, uint8_t> > htmlFieldDisplay;

};

class RHtml {
public:
    static void printHeader(struct mg_connection *conn, const std::string &projectPath);
    static void printFooter(struct mg_connection *conn, const std::string &projectPath);

    static void printProjectList(struct mg_connection *conn, const std::list<std::string> &pList);
    static void printIssueList(struct mg_connection *conn, const ContextParameters &ctx, std::list<Issue*> issueList, std::list<std::string> colspec);
    static void printIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries);
    static void printNewIssuePage(struct mg_connection *conn, const ContextParameters &ctx);

    static bool inList(const std::list<std::string> &listOfValues, const std::string &value);
    static void printIssueForm(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue);
    static void printSigninPage(struct mg_connection *conn, const char *pathToRepository, const char *redirect);


};



#endif
