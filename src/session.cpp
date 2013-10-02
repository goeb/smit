#include <openssl/sha.h>
#include <stdlib.h>
#include <sstream>

#include "session.h"
#include "logging.h"

// static members
SessionBase SessionBase::SessionDb;
UserBase UserBase::UserDb;

void UserBase::load(const char *filename)
{
    // TODO load file
    User *u = new User;
    u->username = "fred";
    u->hashType = "sha1";
    u->hashValue = "31017a722665e4afce586950f42944a6d331dabf";
    u->rolesOnProjects["myproject"] = ROLE_ADMIN;
    u->rolesOnProjects["smit"] = ROLE_ADMIN;
    UserDb.configuredUsers[u->username] = u;
}

User* UserBase::getUser(const std::string &username)
{
    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);
    std::map<std::string, User*>::iterator u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return 0;
    else return u->second;
}

int UserBase::addUser(User newUser)
{
    ScopeLocker(UserDb.locker, LOCK_READ_WRITE);
    User *u = new User;
    *u = newUser;
    UserDb.configuredUsers[u->username] = u;
}


// return session id
std::string SessionBase::requestSession(const std::string &username, const std::string &passwd)
{

}
// return user name
std::string SessionBase::getLoggedInUser(const std::string &sessionId)
{
    LOG_DEBUG("getLoggedInUser(%s)...", sessionId.c_str());
    ScopeLocker(SessionDb.locker, LOCK_READ_ONLY);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);

    if (i == SessionDb.sessions.end()) return "";
    else {
        Session s = i->second;
        if (time(0) - s.ctime > s.duration) {
            // session expired
            LOG_DEBUG("getLoggedInUser: session expired: %s", sessionId.c_str());
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

    std::stringstream randomStr;
    randomStr << rand() << rand() << rand();
    Session s;
    s.ctime = time(0);
    s.id = randomStr.str();
    s.username = username;
    s.duration = SESSION_DURATION;
    SessionDb.sessions[s.id] = s;
};
