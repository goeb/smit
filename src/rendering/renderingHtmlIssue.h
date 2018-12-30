#ifndef _renderingHtmlIssue_h
#define _renderingHtmlIssue_h

#include <list>
#include <string>
#include <stdint.h>

#include "server/HttpContext.h"
#include "ContextParameters.h"

#define QS_GOTO_NEXT "next"
#define QS_GOTO_PREVIOUS "previous"

std::string getQsRemoveColumn(std::string qs, const std::string &property, const std::list<std::string> &defaultCols);
std::string getQsSubSorting(std::string qs, const std::string &property, bool exclusive);

#define FLAG_ENTRY_NOMINAL       0
#define FLAG_ENTRY_BEING_AMENDED (1 << 0) // The displayed entry is currently being amended
#define FLAG_ENTRY_OFFLINE       (1 << 1) // The displayed entry is intended for offline reading

class RHtmlIssue {
public:

    static std::string printOtherProperties(const ContextParameters &ctx, const Entry &ee, bool printMessageHeading,
                                     const char *divStyle);
    static void printFormMessage(const ContextParameters &ctx, const std::string &contents);
    static void printEditMessage(const ContextParameters &ctx, const IssueCopy *issue,
                                 uint32_t eIndexToBeAmended);
    static void printIssueForm(const ContextParameters &ctx, const IssueCopy *issue, bool autofocus);
    static std::string convertToRichText(const std::string &raw);
    static std::string renderPropertiesTable(const ContextParameters &ctx, const IssueCopy &issue, bool offline);
    static std::string renderTags(const ContextParameters &ctx, const IssueCopy &issue);
    static std::string renderEntry(const ContextParameters &ctx, const IssueCopy &issue, uint32_t entryIndex, int flags);
    static void printIssue(const ContextParameters &ctx, const IssueCopy &issue, const uint32_t *entryIdxToBeAmended);
    static void printIssueListFullContents(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList);
    static void printIssueList(const ContextParameters &ctx, const std::vector<IssueCopy> &issueList,
                               const std::list<std::string> &colspec, bool showOtherFormats);
    static void printIssuesAccrossProjects(const ContextParameters &ctx,
                                           const std::vector<IssueCopy> &issues,
                                           const std::list<std::string> &colspec);
    static std::string getEntryExtraStyles(const ProjectConfig &pconfig, const IssueCopy &issue,
                                           uint32_t entryIndex, bool beingAmended);

};



#endif
