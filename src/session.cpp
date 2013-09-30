#include <openssl/sha.h>
#include <stdlib.h>
#include <sstream>

#include "session.h"

// static members
SessionBase SessionBase::SessionDb;


// return session id
std::string SessionBase::requestSession(const std::string &username, const std::string &passwd)
{

}
// return user name
std::string SessionBase::getLoggedInUser(const std::string &sessionId)
{
    ScopeLocker(SessionDb.locker, LOCK_READ_ONLY);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);

    if (i == SessionDb.sessions.end()) return "";
    else {
        Session s = i->second;
        if (time(0) - s.ctime > s.duration) {
            // session expired
            return "";
        } else return s.username;
    }
}

int SessionBase::destroySession(const std::string &sessionId)
{
    return 0; // TODO
}

int SessionBase::createSession(const std::string &username)
{
    ScopeLocker(locker, LOCK_READ_WRITE);

    int r = rand(); // TODO improve random id
    std::stringstream randomStr;
    randomStr << r;
    Session s;
    s.ctime = time(0);
    s.id = randomStr.str();
    s.username = username;
    s.duration = 60*15; // 15 minutes

    SessionDb.sessions[s.id] = s;
};
