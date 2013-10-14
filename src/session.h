#ifndef _session_h
#define _session_h

#include <string>
#include <map>
#include <list>
#include <set>

#include "mutexTools.h"

#define SESSION_DURATION (60*60) // 60 minutes


enum Role {
    ROLE_ADMIN,
    ROLE_RW,     // read-write
    ROLE_RO,     // read-only
    ROLE_REFERENCED, // may be referenced (selectUser)
    ROLE_NONE
};

struct User {
    std::string username;
    std::string hashType;
    std::string hashValue;
    std::map<std::string, enum Role> rolesOnProjects;
    enum Role getRole(const std::string &project);
    std::list<std::pair<std::string, std::string> > getProjects();
    bool superadmin;
    User();

};

class UserBase {
public:
    static void load(const char *repository);
    static User* getUser(const std::string &username);
    static int addUser(User u);
    static void addUserByProject(std::string project, std::string username);
    static std::set<std::string> getUsersOfProject(std::string project);
private:
    static UserBase UserDb;
    std::map<std::string, User*> configuredUsers;
    std::map<std::string, std::set<std::string> > usersByProject; // for each project, indicate which users are at stake
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

    static User getLoggedInUser(const std::string &sessionId); // return user name
    static int destroySession(const std::string &sessionId);

private:
    static SessionBase SessionDb;
    std::string createSession(const std::string &username);
    std::map<std::string, Session> sessions;
    Locker locker;

};


#endif
