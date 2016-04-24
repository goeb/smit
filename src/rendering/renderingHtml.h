#ifndef _renderingHtml_h
#define _renderingHtml_h

#include <list>
#include <string>
#include <stdint.h>

#include "server/HttpContext.h"
#include "ContextParameters.h"
#include "user/session.h"
#include "repository/db.h"


#define QS_GOTO_NEXT "next"
#define QS_GOTO_PREVIOUS "previous"

class RHtml {
public:
    static void printNavigationGlobal(const ContextParameters &ctx);
    static void printNavigationIssues(const ContextParameters &ctx, bool autofocus);

    static void printPageUserList(const ContextParameters &ctx, const std::list<User> &users);

    static void printPageProjectList(const ContextParameters &ctx,
                                     const std::list<ProjectSummary> &pList,
                                     const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject);

    static void printProjectConfig(const ContextParameters &ctx, const std::list<ProjectSummary> &pList,
                                   const ProjectConfig *alternateConfig = NULL);
    static void printPageIssuesFullContents(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList);
    static void printPageIssueList(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList,
                                   const std::list<std::string> &colspec);
    static void printPageIssueAccrossProjects(const ContextParameters &ctx,
                                             const std::vector<IssueCopy> &issues,
                                             const std::list<std::string> &colspec);
    static void printPageIssue(const ContextParameters &ctx, const IssueCopy &issue,
                               const Entry *eTobeAmended);

    static void printPageNewIssue(const ContextParameters &ctx);

    static void printPageSignin(const ContextParameters &ctx, const char *redirect);

    static void printPageStat(const ContextParameters &ctx, const User &u);
    static void printPageUser(const ContextParameters &ctx, const User *u);

    static void printPageView(const ContextParameters &ctx, const PredefinedView &pv);
    static void printPageListOfViews(const ContextParameters &ctx);
    static void printLinksToPredefinedViews(const ContextParameters &ctx);
    static void printDatalistProjects(const ContextParameters &ctx,
                                      const std::list<ProjectSummary> &pList);
    static void printProjects(const ContextParameters &ctx,
                              const std::list<ProjectSummary> &pList,
                              const std::map<std::string, std::map<Role, std::set<std::string> > > &userRolesByProject);
    static void printUsers(const RequestContext *req, const std::list<User> &usersList);
    static void printUserPermissions(const RequestContext *req, const User &u);
    static std::string getScriptProjectConfig(const ContextParameters &ctx, const ProjectConfig *alternateConfig = 0);

    static void printPageEntries(const ContextParameters &ctx, const std::vector<Entry> &entries);
    static void printEntries(const ContextParameters &ctx, const std::vector<Entry> &entries);

};



#endif
