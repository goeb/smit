#ifndef _session_h
#define _session_h

#include <string>
#include <map>
#include <list>
#include <set>

#include "utils/mutexTools.h"
#include "Auth.h"
#include "AuthSha1.h"
#include "notification.h"
#include "Recipient.h"

#define SESSION_DURATION (60*60*36) // 1.5 day
#define COOKIE_VIEW_DURATION (60*60*24) // 1 day
#define PATH_AUTH  "/users/auth"
#define P_USERS       "users"
#define P_PERMISSIONS "permissions"
#define PATH_PERMISSIONS "/" P_USERS "/" P_PERMISSIONS

// authentication schemes

enum Role {
    ROLE_ADMIN,
    ROLE_RW,     // read-write
    ROLE_RO,     // read-only
    ROLE_REFERENCED, // may be referenced (selectUser)
    ROLE_NONE
};

typedef std::string RoleId;

RoleId roleToString(Role r);
Role stringToRole(const RoleId &s);
std::list<RoleId> getAvailableRoles();

class User {
public:
    std::string username;
    Auth *authHandler; // instance owned by the currect User. Deleted on User destruction.
    std::map<std::string, enum Role> rolesOnProjects;
    bool superadmin;
    std::map<std::string, Role> permissions; // map of projectWildcard => role
    Notification notification;

    User();
    User(const User &other);
    User& operator=(const User &rhs);
    ~User();
    enum Role getRole(const std::string &project) const;
    std::list<std::pair<std::string, RoleId> >  getProjects() const;
    std::list<std::string>  getProjectsNames() const;
    std::string serializePermissions() const;
    std::string serializeAuth();
    int loadAuth(std::list<std::string> &tokens);
    void setPasswd(const std::string &passwd);
    int authenticate(char *passwd);
    void consolidateRoles();
    bool shouldBeNotified(const Entry *entry, const IssueCopy &oldIssue);
};

class UserBase {
public:
    static int init(const char *repository);
    static int loadPermissions(const std::string &path, std::map<std::string, User*> &users);
    static int load(const std::string &repository, std::map<std::string, User*> &users);
    static void loadNotifications(const std::string &pathRepo, std::map<std::string, User*> &users);
    static void setLocalInterfaceUser(const std::string &username);
    static int store(const std::string &repository);
    static int initUsersFile(const char *repository);
    static User* getUser(const std::string &username);
    static int addUser(const User &u);
    static int deleteUser(const std::string &username);
    static int hotReload();
    static void computePermissions();

    static std::set<std::string> getUsersOfProject(const std::string &project);
    static std::map<std::string, Role> getUsersRolesOfProject(const std::string &project);
    static std::map<Role, std::set<std::string> > getUsersByRole(const std::string &project);
    static int updateUser(const std::string &username, const User &newConfig);
    static int updatePassword(const std::string &username, const AuthSha1 *authSha1);
    static std::list<User> getAllUsers();
    static inline bool isLocalUserInterface() {return !localInterfaceUsername.empty(); }
    static const std::string getLocalInterfaceUser() { return localInterfaceUsername; }
    static std::list<Recipient> getRecipients(const std::string &projectName,
                                              const Entry *entry, const IssueCopy &oldIssue);

private:
    static UserBase UserDb;
    std::map<std::string, User*> configuredUsers;
    Locker locker; // mutex for configuredUsers
    static std::string Repository;
    static User *addUserInArray(const User &u);

    // Local Interface refers to "smit ui command": browsing a local clone of a smit repository
    static std::string localInterfaceUsername;

    static std::string getPathNotification(const std::string &topdir, const std::string &username);

};

struct Session {
    std::string id;
    std::string username;
    time_t ctime;
    int duration; // seconds
    inline bool isExpired() { if (time(0) - ctime > duration) return true; else return false; }
};

class SessionBase {
public:
    static std::string requestSession(const std::string &username, char *passwd); // return session id

    static User getLoggedInUser(const std::string &sessionId); // return user name
    static int destroySession(const std::string &sessionId);

private:
    static SessionBase SessionDb;
    void garbageCollect();
    std::string createSession(const std::string &username);
    std::map<std::string, Session> sessions;
    Locker locker;

};


#endif
