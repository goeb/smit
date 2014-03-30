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
    ContextParameters(struct mg_connection *cnx, User u, Project &p);
    ContextParameters(struct mg_connection *cnx, User u);
    const Project &getProject() const;

    User user;
    enum Role userRole;
    std::string search;
    std::string sort;
    std::list<std::string> filterin;
    std::list<std::string> filterout;
    Project *project;
    ProjectConfig projectConfig;
    std::list<std::pair<std::string, uint8_t> > htmlFieldDisplay;
    struct mg_connection *conn;
};

class RHtml {
public:
    static void printNavigationGlobal(const ContextParameters &ctx);
    static void printNavigationIssues(const ContextParameters &ctx, bool autofocus);

    static void printPageProjectList(const ContextParameters &ctx,
                                     const std::list<std::pair<std::string, std::string> > &pList,
                                     const std::map<std::string, std::map<std::string, Role> > &userRolesByProject,
                                     const std::list<User> &users);
    static void printProjectConfig(const ContextParameters &ctx);
    static void printPageIssuesFullContents(const ContextParameters &ctx, std::vector<struct Issue*> issueList);
    static void printPageIssueList(const ContextParameters &ctx, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
    static void printPageIssue(const ContextParameters &ctx, const Issue &issue);
    static void printPageNewIssue(const ContextParameters &ctx);

    static bool inList(const std::list<std::string> &listOfValues, const std::string &value);
    static void printIssueForm(const ContextParameters &ctx, const Issue *issue, bool autofocus);
    static void printPageSignin(struct mg_connection *conn, const char *redirect);

    static void printPageUser(const ContextParameters &ctx, const User *u);

    static void printPageView(const ContextParameters &ctx, const PredefinedView &pv);
    static void printPageListOfViews(const ContextParameters &ctx);
    static void printLinksToPredefinedViews(const ContextParameters &ctx);
    static void printProjects(const ContextParameters &ctx,
                              const std::list<std::pair<std::string, std::string> > &pList,
                              const std::map<std::string, std::map<std::string, Role> > *userRolesByProject);
    static void printUsers(struct mg_connection *conn, const std::list<User> &usersList);
    static void printScriptUpdateConfig(const ContextParameters &ctx);
    static std::string convertToRichText(const std::string &raw);
    static void printIssueSummary(const ContextParameters &ctx, const Issue &issue);
    static void printIssue(const ContextParameters &ctx, const Issue &issue);
    static void printIssueListFullContents(const ContextParameters &ctx, std::vector<struct Issue*> issueList);
    static void printIssueList(const ContextParameters &ctx,
                        const std::vector<struct Issue*> &issueList, const std::list<std::string> &colspec);

};



#endif
