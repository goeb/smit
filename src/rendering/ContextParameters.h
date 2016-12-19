#ifndef _ContextParameters_h
#define _ContextParameters_h

#include "user/session.h"
#include "project/Project.h"
#include "global.h"
#include "server/HttpContext.h"

class ContextParameters {
public:
    ContextParameters(const ResponseContext *req, const User &u, const ProjectParameters &pp);
    ContextParameters(const ResponseContext *req, const User &u);
    void init(const ResponseContext *request, const User &u);

    User user; // signed-in user
    enum Role userRole; // role of the signed-in user on the current project (if there is a current project)
    std::string search;
    std::string sort;
    std::map<std::string, std::list<std::string> > filterin;
    std::map<std::string, std::list<std::string> > filterout;

    // project parameters
    std::string projectPath; // empty if no project defined
    std::string projectName;
    ProjectConfig projectConfig;
    std::map<std::string, PredefinedView> projectViews;

    std::set<std::string> usersOfProject;
    std::list<std::pair<std::string, uint8_t> > htmlFieldDisplay;
    const ResponseContext *req;
    const char *originView; // query string format

    inline std::string getProjectUrlName() const { return Project::urlNameEncode(projectName); }
    inline bool hasProject() const { return !projectPath.empty(); }
};

#endif
