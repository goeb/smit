#ifndef _ContextParameters_h
#define _ContextParameters_h

#include "session.h"
#include "Project.h"
#include "global.h"
#include "HttpContext.h"

/** SmitData is used to manage the values in the HTML
  * The HTML items marked with class "smit_data" will have
  * their value replaced by client-side scripting.
  *
  */
class ContextParameters {
public:
    ContextParameters(const RequestContext *req, const User &u, const Project &p);
    ContextParameters(const RequestContext *req, const User &u);
    void init(const RequestContext *request, const User &u);
    const Project &getProject() const;

    User user; // signed-in user
    enum Role userRole;
    std::string search;
    std::string sort;
    std::map<std::string, std::list<std::string> > filterin;
    std::map<std::string, std::list<std::string> > filterout;
    const Project *project;
    ProjectConfig projectConfig;
    std::map<std::string, PredefinedView> predefinedViews;
    std::set<std::string> usersOfProject;
    std::list<std::pair<std::string, uint8_t> > htmlFieldDisplay;
    const RequestContext *req;
    const char *originView; // query string format
};

#endif
