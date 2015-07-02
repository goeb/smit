#ifndef _restApi_h
#define _restApi_h

#include <string>

// resources under the project
// <p>/...
#define RSRC_SMIP ".smip"
#define RESOURCE_OBJECTS    RSRC_SMIP "/objects"
#define RSRC_PROJECT_CONFIG RSRC_SMIP "/refs/project"
#define RSRC_REF_VIEWS      RSRC_SMIP "/refs/views"
#define RSRC_REF_ISSUES     RSRC_SMIP "/refs/issues"
#define RESOURCE_FILES   "files"
#define RESOURCE_ISSUES  "issues"

// resources under the root
#define RSRC_USERS "users"

#define COOKIE_SESSID "sessid"
#define COOKIE_PREFIX "smit-"
inline std::string mangleCookieName(const std::string &name, const std::string &listeningPort)
{
    std::string result = COOKIE_PREFIX;
    result += listeningPort;
    result += "-";
    result += name;
    return result;
}


inline void unmangleCookieName(std::string &cookieName)
{
    size_t lenPrefix = strlen(COOKIE_PREFIX);

    if (cookieName.size() < lenPrefix) return;

    if (0 != cookieName.compare(0, lenPrefix, COOKIE_PREFIX)) {
        // not starting by COOKIE_PREFIX
        // return unchanged
        return;
    }

    // remove the prefix
    // find the second "-" after the prefix
    size_t offset = cookieName.find("-", lenPrefix);
    if (offset != std::string::npos) {
        cookieName = cookieName.substr(offset+1);
    } else {
        // "-" not found. simply remove the prefix
        cookieName = cookieName.substr(lenPrefix);
    }
}



#endif
