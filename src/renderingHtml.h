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
    ContextParameters(struct mg_connection *cnx, User u, const Project &p);
    ContextParameters(struct mg_connection *cnx, User u);
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
    struct mg_connection *conn;
};

class RHtml {
public:
    static void printNavigationGlobal(struct mg_connection *conn, const ContextParameters &ctx);
    static void printNavigationIssues(struct mg_connection *conn, const ContextParameters &ctx, bool autofocus);

    static void printPageProjectList(struct mg_connection *conn, const ContextParameters &ctx, const std::list<std::pair<std::string, std::string> > &pList);
    static void printProjectConfig(struct mg_connection *conn, const ContextParameters &ctx);
    static void printPageIssuesFullContents(const ContextParameters &ctx, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
    static void printPageIssueList(const ContextParameters &ctx, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
    static void printPageIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries);
    static void printPageNewIssue(struct mg_connection *conn, const ContextParameters &ctx);

    static bool inList(const std::list<std::string> &listOfValues, const std::string &value);
    static void printIssueForm(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, bool autofocus);
    static void printPageSignin(struct mg_connection *conn, const char *redirect);

    static void printPageView(struct mg_connection *conn, const ContextParameters &ctx, const PredefinedView &pv);
    static void printPageListOfViews(struct mg_connection *conn, const ContextParameters &ctx);
    static void printLinksToPredefinedViews(struct mg_connection *conn, const ContextParameters &ctx);
    static void printProjects(struct mg_connection *conn, const std::list<std::pair<std::string, std::string> > &pList);
    static void printScriptUpdateConfig(struct mg_connection *conn, const ContextParameters &ctx);
    static void printIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries);
    static void printIssueList(struct mg_connection *conn, const ContextParameters &ctx,
                        std::vector<struct Issue*> issueList, std::list<std::string> colspec);

};



#endif
