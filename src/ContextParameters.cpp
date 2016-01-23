
#include "config.h"

#include <stdlib.h>
#include <sstream>
#include <string.h>
#include <set>
#include <stdarg.h>

#include "ContextParameters.h"
#include "db.h"
#include "session.h"
#include "global.h"

#ifdef KERBEROS_ENABLED
  #include "AuthKrb5.h"
#endif

#ifdef LDAP_ENABLED
  #include "AuthLdap.h"
#endif



/** Build a context for a user and project
  *
  * ContextParameters::projectConfig should be user rather than ContextParameters::project.getConfig()
  * as getConfig locks a mutex.
  * ContextParameters::projectConfig gets the config once at initilisation,
  * and afterwards one can work with the copy (without locking).
  */
ContextParameters::ContextParameters(const RequestContext *req, const User &u, const ProjectParameters &pp)
{
    init(req, u);
    projectName = pp.projectName;
    projectPath = pp.projectPath;
    projectConfig = pp.pconfig;
    projectViews = pp.views;
    userRole = u.getRole(projectName);
}

ContextParameters::ContextParameters(const RequestContext *request, const User &u)
{
    init(request, u);
}

void ContextParameters::init(const RequestContext *request, const User &u)
{
    user = u;
    req = request;
    originView = 0;
    userRole = ROLE_NONE;
}
