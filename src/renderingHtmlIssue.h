#ifndef _renderingHtmlIssue_h
#define _renderingHtmlIssue_h

#include <list>
#include <string>
#include <stdint.h>

#include "HttpContext.h"
#include "ContextParameters.h"
#include "db.h"

#define QS_GOTO_NEXT "next"
#define QS_GOTO_PREVIOUS "previous"

std::string getQsRemoveColumn(std::string qs, const std::string &property, const std::list<std::string> &defaultCols);
void printFilters(const ContextParameters &ctx);
std::string getQsSubSorting(std::string qs, const std::string &property, bool exclusive);


class RHtmlIssue {
public:

    static void printOtherProperties(const ContextParameters &ctx, const Entry &ee, bool printMessageHeading,
                                     const char *divStyle);
    static void printFormMessage(const ContextParameters &ctx, const std::string &contents);
    static void printEditMessage(const ContextParameters &ctx, const IssueCopy *issue,
                                 const Entry &eToBeAmended);
    static void printIssueForm(const ContextParameters &ctx, const IssueCopy *issue, bool autofocus);
    static std::string convertToRichText(const std::string &raw);
    static void printIssueSummary(const ContextParameters &ctx, const IssueCopy &issue);
    static void printIssue(const ContextParameters &ctx, const IssueCopy &issue, const std::string &entryToBeAmended);
    static void printIssueListFullContents(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList);
    static void printIssueList(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList,
                               const std::list<std::string> &colspec, bool showOtherFormats);
    static void printIssuesAccrossProjects(ContextParameters ctx,
                                           const std::vector<IssueCopy> &issues,
                                           const std::list<std::string> &colspec);
};



#endif
