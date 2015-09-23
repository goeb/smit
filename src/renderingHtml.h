#ifndef _renderingHtml_h
#define _renderingHtml_h

#include <list>
#include <string>
#include <stdint.h>

#include "HttpContext.h"
#include "ContextParameters.h"
#include "db.h"
#include "session.h"


#define QS_GOTO_NEXT "next"
#define QS_GOTO_PREVIOUS "previous"


class RHtml {
public:
    static void printNavigationGlobal(const ContextParameters &ctx);
    static void printNavigationIssues(const ContextParameters &ctx, bool autofocus);

    static void printPageUserList(const ContextParameters &ctx, const std::list<User> &users);

    static void printPageProjectList(const ContextParameters &ctx,
                                     const std::list<std::pair<std::string, std::string> > &pList,
                                     const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject);

    static void printProjectConfig(const ContextParameters &ctx);
    static void printPageIssuesFullContents(const ContextParameters &ctx, const std::vector<Issue> &issueList);
    static void printPageIssueList(const ContextParameters &ctx, const std::vector<Issue> &issueList,
                                   std::list<std::string> colspec);
    static void printPageIssueAccrossProjects(const ContextParameters &ctx,
                                             std::vector<Issue> &issues,
											 std::list<std::string> colspec);
    static void printPageIssue(const ContextParameters &ctx, const Issue &issue,
                               const Entry *eTobeAmended);

    static void printPageNewIssue(const ContextParameters &ctx);

    static void printFormMessage(const ContextParameters &ctx, const std::string &contents);
    static void printEditMessage(const ContextParameters &ctx, const Issue *issue,
                                 const Entry &eToBeAmended);
    static void printIssueForm(const ContextParameters &ctx, const Issue *issue, bool autofocus);
    static void printPageSignin(const ContextParameters &ctx, const char *redirect);

    static void printPageStat(const ContextParameters &ctx, const User &u);
    static void printPageUser(const ContextParameters &ctx, const User *u);

    static void printPageView(const ContextParameters &ctx, const PredefinedView &pv);
    static void printPageListOfViews(const ContextParameters &ctx);
    static void printLinksToPredefinedViews(const ContextParameters &ctx);
    static void printProjects(const ContextParameters &ctx,
                              const std::list<std::pair<std::string, std::string> > &pList,
                              const std::map<std::string, std::map<Role, std::set<std::string> > > *userRolesByProject);
    static void printUsers(const RequestContext *req, const std::list<User> &usersList);
    static void printUserPermissions(const RequestContext *req, const User &u);
    static std::string getScriptProjectConfig(const ContextParameters &ctx);

    static std::string convertToRichText(const std::string &raw);
    static void printIssueSummary(const ContextParameters &ctx, const Issue &issue);
    static void printIssue(const ContextParameters &ctx, const Issue &issue, const std::string &entryToBeAmended);
    static void printIssueListFullContents(const ContextParameters &ctx, const std::vector<Issue> &issueList);
    static void printIssueList(const ContextParameters &ctx, const std::vector<Issue> &issueList,
                               const std::list<std::string> &colspec, bool showOtherFormats);
    static void printIssuesAccrossProjects(ContextParameters ctx,
                                           const std::vector<Issue> &issues,
                                           const std::list<std::string> &colspec);

};



#endif
