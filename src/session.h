#ifndef _session_h
#define _session_h

#include <string>
#include <map>

#include "mutexTools.h"

#define SESSION_DURATION (60*15) // 15 minutes


enum Role {
    ROLE_ADMIN,
    ROLE_RW,     // read-write
    ROLE_RO,     // read-only
    ROLE_NONE
};

struct User {
    std::string username;
    std::string hashType;
    std::string hashValue;
    std::map<std::string, enum Role> rolesOnProjects;
    enum Role getRole(const std::string &project);
};

class UserBase {
public:
    static void load(const char *filename);
    static User* getUser(const std::string &username);
    static int addUser(User u);

private:
    static UserBase UserDb;
    std::map<std::string, User*> configuredUsers;
    Locker locker;
};

struct Session {
    std::string id;
    std::string username;
    time_t ctime;
    int duration; // seconds
};

class SessionBase {
public:
    static std::string requestSession(const std::string &username, const std::string &passwd); // return session id

    static std::string getLoggedInUser(const std::string &sessionId); // return user name
    int destroySession(const std::string &sessionId);

private:
    static SessionBase SessionDb;
    std::string createSession(const std::string &username);
    std::map<std::string, Session> sessions;
    Locker locker;

};


#endif
