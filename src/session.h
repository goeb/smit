#ifndef _session_h
#define _session_h

#include <string>
#include <map>
#include <list>
#include <set>

#include "mutexTools.h"
#include "Auth.h"

#define SESSION_DURATION (60*60*24) // 1 day
#define COOKIE_VIEW_DURATION (60*60*24) // 1 day
// authentication schemes

enum Role {
    ROLE_ADMIN,
    ROLE_RW,     // read-write
    ROLE_RO,     // read-only
    ROLE_REFERENCED, // may be referenced (selectUser)
    ROLE_NONE
};

struct Permission {
    std::string projectWildcard;
    Role role;
};

std::string roleToString(Role r);
Role stringToRole(const std::string &s);
std::list<std::string> getAvailableRoles();

struct User {
    std::string username;
    Auth *authHandler;
    std::map<std::string, enum Role> rolesOnProjects;
    enum Role getRole(const std::string &project) const;
    std::list<std::pair<std::string, std::string> >  getProjects() const;
    bool superadmin;
    User();
    std::string serialize();
    int load(std::list<std::string> &tokens, bool checkProject);
    void setPasswd(const std::string &passwd);
    int authenticate(char *passwd);
    std::list<Permission> permissions;
    void consolidateRoles();

};

class UserBase {
public:
    static int init(const char *repository, bool checkProject = true);
    static void setLocalUserInterface();
    static int store(const std::string &repository);
    static int initUsersFile(const char *repository);
    static User* getUser(const std::string &username);
    static int addUser(User u);
    static std::set<std::string> getUsersOfProject(const std::string &project);
    static std::map<std::string, Role> getUsersRolesOfProject(const std::string &project);
    static std::map<Role, std::set<std::string> > getUsersByRole(const std::string &project);
    static int updateUser(const std::string &username, User newConfig);
    static int updatePassword(const std::string &username, const std::string &password);
    static std::list<User> getAllUsers();
    static inline bool isLocalUserInterface() {return localUserInterface;}

private:
    static UserBase UserDb;
    std::map<std::string, User*> configuredUsers;
    Locker locker; // mutex for configuredUsers
    static std::string Repository;
    static void addUserInArray(User u);

    // localUserInterface enables anonymous read access
    // used for browsing a local clone of a smit repository
    static bool localUserInterface;
};

struct Session {
    std::string id;
    std::string username;
    time_t ctime;
    int duration; // seconds
};

class SessionBase {
public:
    static std::string requestSession(const std::string &username, char *passwd); // return session id

    static User getLoggedInUser(const std::string &sessionId); // return user name
    static int destroySession(const std::string &sessionId);

private:
    static SessionBase SessionDb;
    std::string createSession(const std::string &username);
    std::map<std::string, Session> sessions;
    Locker locker;

};


#endif
