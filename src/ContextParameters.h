#ifndef _ContextParameters_h
#define _ContextParameters_h

#include "session.h"
#include "Project.h"
#include "global.h"
#include "HttpContext.h"

class ContextParameters {
public:
    ContextParameters(const RequestContext *req, const User &u, const ProjectParameters &pp);
    ContextParameters(const RequestContext *req, const User &u);
    void init(const RequestContext *request, const User &u);

    User user; // signed-in user
    enum Role userRole;
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
    const RequestContext *req;
    const char *originView; // query string format

    inline std::string getProjectUrlName() const { return Project::urlNameEncode(projectName); }
    inline bool hasProject() const { return !projectPath.empty(); }
};

#endif
