#ifndef _renderingHtml_h
#define _renderingHtml_h

#include <list>
#include <string>
#include <stdint.h>

#include "HttpContext.h"
#include "db.h"
#include "session.h"


#define QS_GOTO_NEXT "next"
#define QS_GOTO_PREVIOUS "previous"
#define QS_ORIGIN_VIEW "view"


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
    const Project *project;
    ProjectConfig projectConfig;
    std::set<std::string> usersOfProject;
    std::list<std::pair<std::string, uint8_t> > htmlFieldDisplay;
    struct mg_connection *conn;
    std::string originView; // query string format
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
    static void printPageIssueList(const ContextParameters &ctx, std::vector<struct Issue*> issueList,
                                   std::list<std::string> colspec);
    static void printPageIssueAccrossProjects(const ContextParameters &ctx,
                                             std::map<std::string, std::vector<Issue*> > issues,
											 std::list<std::string> colspec);
    static void printPageIssue(const ContextParameters &ctx, const Issue &issue);
    static void printPageNewIssue(const ContextParameters &ctx);

    static bool inList(const std::list<std::string> &listOfValues, const std::string &value);
    static void printIssueForm(const ContextParameters &ctx, const Issue *issue, bool autofocus);
    static void printPageSignin(MongooseRequestContext *request, const char *redirect);

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
    static void printIssueNext(const ContextParameters &ctx, const Issue &issue);
    static void printIssuePrevious(const ContextParameters &ctx, const Issue &issue);
    static void printIssue(const ContextParameters &ctx, const Issue &issue);
    static void printIssueListFullContents(const ContextParameters &ctx, std::vector<struct Issue*> issueList);
    static void printIssueList(const ContextParameters &ctx, const std::vector<struct Issue*> &issueList,
                               const std::list<std::string> &colspec, bool showOtherFormats);
    static void printIssuesAccrossProjects(ContextParameters ctx,
                                           const std::map<std::string, std::vector<struct Issue*> >&issues,
                                           const std::list<std::string> &colspec);

};



#endif
