/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include <openssl/sha.h>
#include <sstream>
#include <openssl/rand.h>
#include <openssl/err.h>

#include "session.h"
#include "utils/logging.h"
#include "utils/identifiers.h"
#include "utils/filesystem.h"
#include "utils/parseConfig.h"
#include "global.h"
#include "repository/db.h"
#include "fnmatch.h"

#ifdef KERBEROS_ENABLED
  #include "AuthKrb5.h"
#endif

#ifdef LDAP_ENABLED
  #include "AuthLdap.h"
#endif

#define DIR_NOTIFICATIONS  "users/notifications"


// static members
SessionBase SessionBase::SessionDb;
UserBase UserBase::UserDb;
std::string UserBase::Repository;
std::string UserBase::localInterfaceUsername = "";

const char *FILE_USERS = "users";

User::User()
{
    superadmin = false;
    authHandler = NULL;
}

User::User(const User &other)
{
    *this = other;
}
User& User::operator=(const User &rhs)
{
    username = rhs.username;
    if (rhs.authHandler) authHandler = rhs.authHandler->createCopy();
    else authHandler = NULL;
    rolesOnProjects = rhs.rolesOnProjects;
    superadmin = rhs.superadmin;
    permissions = rhs.permissions;
    notification = rhs.notification;
    return *this;
}
User::~User()
{
    if (authHandler) delete authHandler;
    authHandler = NULL;
}


std::string User::serializeAuth()
{
    std::string auth = "adduser " + serializeSimpleToken(username);

    if (authHandler) {
        auth += " -type ";
        std::string s = authHandler->serialize();
        auth += s;
    }
    auth += "\n";
    return auth;
}

std::string User::serializePermissions() const
{
    std::string result;

    if (superadmin) {
        result += "setperm " + serializeSimpleToken(username) + " superadmin\n";
    }

    std::map<std::string, enum Role>::const_iterator perm;
    FOREACH(perm, permissions) {
        std::string p = "setperm " + serializeSimpleToken(username);
        p += " " + roleToString(perm->second) + " ";
        p += serializeSimpleToken(perm->first) + "\n";
        result += p;
    }
    return result;
}

/** Load a authentication config for a user
  *
  * The tokens are those found after the verb "adduser".
  * The verb "adduser" must not be included.
  *
  * @return
  *     0 success
  *    -1 error
  */
int User::loadAuth(std::list<std::string> &tokens)
{
    username = popListToken(tokens);
    if (username.empty()) {
        LOG_ERROR("Incomplete 'addUser' (missing username)");
        return -1;
    }

    while (! tokens.empty()) {
        std::string token = popListToken(tokens);
        if (token == "-type") {
            token = popListToken(tokens);

            if (token == AUTH_SHA1) {
                Auth *ah = AuthSha1::deserialize(tokens);
                if (!ah) {
                    LOG_ERROR("Cannot load auth for user '%s'", username.c_str());
                }
                authHandler = ah;

#ifdef KERBEROS_ENABLED
            } else if (token == AUTH_KRB5) {
                authHandler = AuthKrb5::deserialize(tokens);
#endif
#ifdef LDAP_ENABLED
            } else if (token == AUTH_LDAP) {
                // single sign-on authentication via a ldap server
                authHandler = AuthLdap::deserialize(tokens);
#endif
            }
        } else {
            LOG_ERROR("Unexpected token '%s' for user '%s'", token.c_str(), username.c_str());
        }
    }
    if (authHandler) authHandler->username = username;
    return 0; // success
}

static std::string getRandom8bytes()
{
    const size_t SIZE8 = 8;
    unsigned char buf[SIZE8];
    int r = RAND_bytes(buf, SIZE8);
    if (r != 1) {
        LOG_ERROR("RAND_bytes failed (%lu). Using RAND_pseudo_bytes.", ERR_get_error());
        r = RAND_pseudo_bytes(buf, SIZE8);
        if (r == 0) {
            LOG_ERROR("Warning: RAND_pseudo_bytes not strong.");
        } else if (r == -1) {
            // Not supported by the current RAND method
            // Abort for security reason
            LOG_ERROR("RAND_pseudo_bytes not supported. Abort.");
            exit(1);
        }
    }

    return bin2hex(buf, SIZE8);
}

std::string getNewSalt()
{
    return getRandom8bytes();
}

/** Set authentication scheme SHA1 and set password
  */
void User::setPasswd(const std::string &password)
{
    std::string salt = getNewSalt();
    std::string hash = getSha1(password + salt);
    AuthSha1 *ah = new AuthSha1("", hash, salt);
    if (authHandler) delete authHandler;
    authHandler = ah;
}

/** Authenticate a user after his/her password
  * @return
  *     0 success
  *    -1 failure
  *    -2 failure, password expired
  */
int User::authenticate(char *passwd)
{
    if (authHandler) {
        return authHandler->authenticate(passwd);
    } else {
        LOG_ERROR("Unknown authentication scheme for user '%s'", username.c_str());
        return -1;
    }
}


/** Consolidate roles on projects after the permissions
  *
  * The for each known project, examine the permissions
  * of the users on this project.
  *
  * If several wildcard expressions match for a given project,
  * then the most restrictive permission is chosen.
  */
void User::consolidateRoles()
{
    rolesOnProjects.clear();

    //std::list<Permission> permissions;
    Project *p = Database::Db.getNextProject(0);
    while (p) {
        std::map<std::string, Role>::iterator perm;
        Role roleOnThisProject = ROLE_NONE;
        FOREACH(perm, permissions) {
            int r = fnmatch(perm->first.c_str(), p->getName().c_str(), 0);
            if (r == 0) {
                // match
                // keep the most restrictive permission if several wildcard match
                if (roleOnThisProject == ROLE_NONE) roleOnThisProject = perm->second;
                else if (perm->second > roleOnThisProject) roleOnThisProject = perm->second;
            }
        }
        if (roleOnThisProject != ROLE_NONE) {
            LOG_DIAG("Role of user '%s' on project '%s': %s", username.c_str(),
                     p->getName().c_str(), roleToString(roleOnThisProject).c_str());
            rolesOnProjects[p->getName()] = roleOnThisProject;
        }

        p = Database::Db.getNextProject(p);
    }
}

bool User::shouldBeNotified(const Entry *entry, const IssueCopy &oldIssue)
{
    if (notification.notificationPolicy.empty()) return false;
    if (notification.notificationPolicy == NOTIFY_POLICY_NONE) return false;
    if (notification.notificationPolicy == NOTIFY_POLICY_ALL) return true;

    if (notification.notificationPolicy == NOTIFY_POLICY_ME) {
        // if any property of the entry or oldIssue is the user name,
        // then return that the user should be notified
        if (hasPropertyValue(entry->properties, username)) return true;
        if (hasPropertyValue(oldIssue.properties, username)) return true;
    }

    return false;
}


RoleId roleToString(Role r)
{
    if (r == ROLE_ADMIN) return "admin";
    else if (r == ROLE_RW) return "rw";
    else if (r == ROLE_RO) return "ro";
    else if (r == ROLE_REFERENCED) return "ref";
    else return "none";
}

Role stringToRole(const RoleId &s)
{
    if (s == "ref") return ROLE_REFERENCED;
    else if (s == "ro") return ROLE_RO;
    else if (s == "rw") return ROLE_RW;
    else if (s == "admin") return ROLE_ADMIN;
    else return ROLE_NONE;
}


std::list<RoleId> getAvailableRoles()
{
    std::list<std::string> result;
    result.push_back(roleToString(ROLE_REFERENCED));
    result.push_back(roleToString(ROLE_RO));
    result.push_back(roleToString(ROLE_RW));
    result.push_back(roleToString(ROLE_ADMIN));
    return result;
}

/** Load the 'permissions' file
 */
int UserBase::loadPermissions(const std::string &path, std::map<std::string, User*> &users)
{
    std::string filePermissions = std::string(path) + "/" PATH_REPO "/" PATH_PERMISSIONS;
    std::string data;
    int n = loadFile(filePermissions.c_str(), data);
    if (n != 0) {
        LOG_ERROR("Could not load file '%s': %s", filePermissions.c_str(), strerror(errno));

    } else {
        std::list<std::list<std::string> > lines = parseConfigTokens(data.c_str(), data.size());
        std::list<std::list<std::string> >::iterator line;
        FOREACH(line, lines) {
            std::string verb = popListToken(*line);
            if (verb == "setperm") {
                std::string username = popListToken(*line);
                if (username.empty()) {
                    LOG_ERROR("Invalid 'setperm' with no username: %s", filePermissions.c_str());
                    continue;
                }
                std::map<std::string, User*>::iterator uit = users.find(username);

                if (uit == users.end()) {
                    LOG_ERROR("Unknown user '%s' referenced from '%s'", username.c_str(),
                              filePermissions.c_str());
                    continue;
                }

                std::string roleStr = popListToken(*line);
                if (roleStr == "superadmin") {
                    uit->second->superadmin = true;
                } else {
                    Role role = stringToRole(roleStr);
                    std::string projectWildcard = popListToken(*line);
                    uit->second->permissions[projectWildcard] = role;
                }

            } else {
                LOG_ERROR("Invalid token '%s' in %s", verb.c_str(), filePermissions.c_str());
                return -1;
            }
        }
    }

    // for each user, consolidate its roles after the wildcarded permissions
    std::map<std::string, User*>::iterator u;
    FOREACH(u, users) {
        u->second->consolidateRoles();
    }

    return 0;
}

static int initNotificationDir(const std::string &pathRepo)
{
    std::string path = std::string(pathRepo) + "/" PATH_REPO "/" + DIR_NOTIFICATIONS;
    if (isDir(path)) return 0;

    int ret = mkdir(path.c_str());
    if (ret != 0) {
        LOG_ERROR("Cannot init directory '%s': %s", path.c_str(), strerror(errno));
    }
    return ret;
}

/** Load user authentication parameters and permissions
  *
  * @param[out] users
  *
  * @return
  *    0 ok
  *   -1 configuration error
  */
int UserBase::load(const std::string &path, std::map<std::string, User*> &users)
{
    Repository = path;

    // load the 'auth' file
    std::string auth = std::string(path) + "/" PATH_REPO "/" PATH_AUTH;

    std::string data;
    int n = loadFile(auth.c_str(), data);
    if (n != 0) {
        LOG_ERROR("Could not load file '%s': %s", auth.c_str(), strerror(errno));

    } else {
        std::list<std::list<std::string> > lines = parseConfigTokens(data.c_str(), data.size());

        std::list<std::list<std::string> >::iterator line;
        FOREACH(line, lines) {
            std::string verb = popListToken(*line);
            if (verb == "adduser") {
                User u;
                int r = u.loadAuth(*line);
                if (r == 0) {
                    // add user in database
                    LOG_DIAG("Loaded user: '%s'", u.username.c_str());
                    users[u.username] = new User(u);
                }
            } else {
                LOG_ERROR("Invalid token '%s' in %s", verb.c_str(), auth.c_str());
                return -1;
            }
        }
    }

    int rc = loadPermissions(path, users);
    if (rc < 0) return rc;

    // init dir for notification configs
    int ret = initNotificationDir(path);
    if (ret != 0) return -1;

    loadNotifications(path, users);

    return rc;
}

std::string UserBase::getPathNotification(const std::string &pathRepo, const std::string &username)
{
    std::string mangled = urlEncode(username);
    std::string file = std::string(pathRepo) + "/" PATH_REPO "/" + DIR_NOTIFICATIONS + "/" + mangled;
    return file;
}

void UserBase::loadNotifications(const std::string &pathRepo, std::map<std::string, User*> &users)
{
    std::map<std::string, User*>::iterator user;
    FOREACH(user, users) {
        std::string file = getPathNotification(pathRepo, user->first);
        Notification::load(file, user->second->notification);
    }
}

/** Load the users of a repository
  */
int UserBase::init(const char *path)
{

    if (isLocalUserInterface()) {
        // Initiate the local user in the database
        // so that tha loadPermission will succeed (whereas the load 'auth' will fail)
        User u;
        u.username = localInterfaceUsername;
        addUserInArray(u);
    }
    return load(path, UserDb.configuredUsers);
}

void UserBase::setLocalInterfaceUser(const std::string &username)
{
    localInterfaceUsername = username;
}

/** Create the files of the user database
  *
  * The files are:
  * - 'auth'
  * - 'permissions'
  */
int UserBase::initUsersFile(const char *repository)
{
    // init file 'auth'
    std::string path = repository;
    path += "/" PATH_REPO "/" PATH_AUTH;
    mkdirs(getDirname(path));
    int r = writeToFile(path, "");
    if (r < 0) return r;

    // init file 'permissions'
    path = repository;
    path += "/" PATH_REPO "/" PATH_PERMISSIONS;
    mkdirs(getDirname(path));
    return writeToFile(path, "");
}

/** Store the user configuration to the filesystem
  *
  */
int UserBase::store(const std::string &repository)
{
    std::string permissions; // this will be stored into file 'permissions'
    std::string auth; // and this will be stored into file 'auth'

    std::map<std::string, User*>::iterator uit;
    FOREACH(uit, UserDb.configuredUsers) {
        permissions += uit->second->serializePermissions();
        auth += uit->second->serializeAuth();
    }
    std::string pathPermissions = repository + "/" PATH_REPO "/" PATH_PERMISSIONS;
    int r = writeToFile(pathPermissions, permissions);
    if (r < 0) return r;

    std::string pathAuth = repository + "/" PATH_REPO "/" PATH_AUTH;
    r = writeToFile(pathAuth, auth);
    if (r < 0) return r;

    return r;
}


User* UserBase::getUser(const std::string &username)
{
    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_ONLY);
    std::map<std::string, User*>::iterator u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return 0;
    else return u->second;
}

/** Add a new user in database
  */
User *UserBase::addUserInArray(User newUser)
{
    User *u = new User;
    *u = newUser;

    // delete old user with same name, if any
    std::map<std::string, User*>::iterator uit = UserDb.configuredUsers.find(u->username);
    if (uit != UserDb.configuredUsers.end()) delete uit->second;

    UserDb.configuredUsers[u->username] = u;
    return u;
}

/** Add a new user in database and store it.
  */
int UserBase::addUser(User newUser)
{
    if (newUser.username.empty()) return -1;

    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_WRITE);

    // check if user already exists

    std::map<std::string, User*>::iterator uit = UserDb.configuredUsers.find(newUser.username);
    if (uit != UserDb.configuredUsers.end()) {
        return -2;
    }

    User *u = addUserInArray(newUser);
    int r = store(Repository);
    if (r < 0) return r;

    u->consolidateRoles();

    // store notification (dedicated file)
    std::string pathNotification = getPathNotification(Repository, newUser.username);
    r = newUser.notification.store(pathNotification);
    if (r < 0) return r;

    return 0;
}

int UserBase::deleteUser(const std::string &username)
{
    if (username.empty()) return -1;

    LOCK_SCOPE(UserDb.locker, LOCK_READ_WRITE);

    std::map<std::string, User*>::iterator uit = UserDb.configuredUsers.find(username);

    if (uit == UserDb.configuredUsers.end()) {
        LOG_ERROR("Cannot delete user '%s': no such user", username.c_str());
        return -1;
    }

    UserDb.configuredUsers.erase(uit);

    // store
    int r = store(Repository);
    if (r < 0) {
        LOG_ERROR("Cannot store deletion of user '%s'", username.c_str());
    }

    std::string pathNotification = getPathNotification(Repository, username);
    Notification::deleteStorageFile(pathNotification);

    return r;

}

/** Reload the user database (authentication parameters and permissions)
  */
int UserBase::hotReload()
{
    LOG_INFO("Hot reload of users");

    std::map<std::string, User*> newUsers;
    int r = load(UserDb.Repository, newUsers);
    if (r != 0) {
        // delete the allocated objects
        std::map<std::string, User*>::iterator u;
        FOREACH(u, newUsers) {
            delete u->second;
        }
        return -1;
    }

    // Ok, the files were loaded successfully.

    LOCK_SCOPE(UserDb.locker, LOCK_READ_WRITE);

    // free the old objects
    std::map<std::string, User*>::iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        delete u->second;
    }

    UserDb.configuredUsers = newUsers;

    return 0;
}

/** Compute the permissions of all users
  *
  * Based on the permissions wildcards, this computes the roles
  * of all the users on all the projects
  */
void UserBase::computePermissions()
{
    LOCK_SCOPE(UserDb.locker, LOCK_READ_WRITE);

    // for each user, consolidate its roles after the wildcarded permissions
    std::map<std::string, User*>::iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        u->second->consolidateRoles();
    }
}


/** Get the list of users that are at stake in the given project
  */
std::set<std::string> UserBase::getUsersOfProject(const std::string &project)
{
    std::set<std::string> result;

    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        User *user = u->second;
        if (user->getRole(project) != ROLE_NONE) result.insert(u->first);
    }

    return result;
}

/** Get the list of users that are at stake in the given project
  */
std::map<std::string, Role> UserBase::getUsersRolesOfProject(const std::string &project)
{
    std::map<std::string, Role> result;

    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        enum Role r = u->second->getRole(project);
        if (r == ROLE_NONE) continue;

        result[u->first] = r;
    }
    return result;
}

/** Get the list of users of a given project (grouped by roles)
  */
std::map<Role, std::set<std::string> > UserBase::getUsersByRole(const std::string &project)
{
    std::map<Role, std::set<std::string> > result;

    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        enum Role r = u->second->getRole(project);
        if (r == ROLE_NONE) continue;

        result[r].insert(u->first);
    }
    return result;
}


/** Update a users's configuration
  *
  * @param username
  * @param newConfig
  *
  * In case of renaming, username is the old name, and
  * newConfig.name is the new name.
  */
int UserBase::updateUser(const std::string &username, User newConfig)
{
    if (username.empty() || newConfig.username.empty()) return -1;

    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_WRITE);

    std::map<std::string, User*>::iterator existingUser;

    if (newConfig.username != username) {
        // Case of a renaming
        // Check that the new name does not exist
        existingUser = UserDb.configuredUsers.find(newConfig.username);
        if (existingUser != UserDb.configuredUsers.end()) return -2;
    }

    // check that modified user exists
    existingUser = UserDb.configuredUsers.find(username);
    if (existingUser == UserDb.configuredUsers.end()) return -3;

    *(existingUser->second) = newConfig;

    // change the key in the map if name of user was modified
    if (username != newConfig.username) {
        LOG_INFO("User renamed: %s -> %s", username.c_str(), newConfig.username.c_str());
        UserDb.configuredUsers[newConfig.username] = existingUser->second;
        UserDb.configuredUsers.erase(username);
    }

    int r = store(Repository);
    if (r < 0) return r;

    existingUser->second->consolidateRoles();

    // store notification (dedicated file)
    std::string pathNotification = getPathNotification(Repository, newConfig.username);
    r = newConfig.notification.store(pathNotification);
    if (r < 0) return r;

    return 0;
}

int UserBase::updatePassword(const std::string &username, const AuthSha1 *authSha1)
{
    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_WRITE);
    std::map<std::string, User*>::iterator u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return -1;

    LOG_DIAG("updatePassword for %s", username.c_str());
    u->second->authHandler = authSha1->createCopy();
    return store(Repository);
}

std::list<User> UserBase::getAllUsers()
{
    std::list<User> result;

    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_ONLY);
    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        result.push_back(*(u->second));
    }
    return result;
}

std::list<Recipient> UserBase::getRecipients(const std::string &projectName,
                                             const Entry *entry,
                                             const IssueCopy &oldIssue)
{
    ScopeLocker scopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::list<Recipient> recipients;

    std::map<std::string, User*>::const_iterator uit;
    FOREACH(uit, UserDb.configuredUsers) {
        User *u = uit->second;
        if (u->shouldBeNotified(entry, oldIssue)) {
            // Ok, this user should be notified.

            // Double check if user has read-access on project...
            if (u->getRole(projectName) <= ROLE_RO) {
                Recipient recipient;
                recipient.email = u->notification.email;
                recipient.gpgPubKey = u->notification.gpgPublicKey;
                recipients.push_back(recipient);
            } else {
                // internal error, as we should not get here
                LOG_ERROR("Notification attempt to unauthorized user");
            }
        }
    }
    return recipients;
}



static UserBase UserDb;
std::map<std::string, User*> configuredUsers;


/** Get the role of the user for the given project
  */
enum Role User::getRole(const std::string &project) const
{
    std::map<std::string, enum Role>::const_iterator r = rolesOnProjects.find(project);
    if (r == rolesOnProjects.end()) return ROLE_NONE;
    else return r->second;
}

/** Get the projects where the user has access (read or write)
  * List of pairs (project, role)
  */
std::list<std::pair<std::string, RoleId> > User::getProjects() const
{
    std::list<std::pair<std::string, std::string> > result;
    std::map<std::string, enum Role>::const_iterator r;
    for (r = rolesOnProjects.begin(); r != rolesOnProjects.end(); r++) {
        if (r->second <= ROLE_RO) {
            result.push_back(std::make_pair(r->first, roleToString(r->second)));
        }
    }
    return result;
}
/** Get the projects names where the user has access (read or write)
  */
std::list<std::string> User::getProjectsNames() const
{
    std::list<std::string> result;
    std::map<std::string, enum Role>::const_iterator r;
    for (r = rolesOnProjects.begin(); r != rolesOnProjects.end(); r++) {
        if (r->second <= ROLE_RO) {
            result.push_back(r->first);
        }
    }
    return result;
}

/** Check user credentials and initiate a session
  *
  * @return
  *    The session id on success, otherwise, an empty string.
  */
std::string SessionBase::requestSession(const std::string &username, char *passwd)
{
    if (UserBase::isLocalUserInterface()) return "local";

    LOG_DEBUG("Requesting session: username=%s, password=****", username.c_str());
    std::string sessid = ""; // empty session id indicates that no session is on
    User *u = UserBase::getUser(username);

    if (u) {
        int r = u->authenticate(passwd);
        if (r == 0) {
            // authentication succeeded, create session
            sessid = SessionDb.createSession(username.c_str());
            LOG_DEBUG("Session created for '%s': %s", username.c_str(), sessid.c_str());
        } else {
            sessid = "";
        }
    } else {
        LOG_INFO("Session request for unknown user '%s'", username.c_str());
    }
    return sessid;
}

/** Return a user object
  *
  * @return
  *     If no valid user if found, then the returned object has an empty username.
  */
User SessionBase::getLoggedInUser(const std::string &sessionId)
{
    if (UserBase::isLocalUserInterface()) {
        // case of a command 'smit ui'
        User *u = UserBase::getUser(UserBase::getLocalInterfaceUser());

        if (!u) {
            LOG_ERROR("getLoggedInUser: Cannot get local user");
            User u;
            return u;
        }
        return *(u);
    }

    LOG_DEBUG("getLoggedInUser(%s)...", sessionId.c_str());
    ScopeLocker scopeLocker(SessionDb.locker, LOCK_READ_ONLY);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);

    if (i != SessionDb.sessions.end()) {
        // session found
        Session s = i->second;
        if (s.isExpired()) {
            // session expired
            LOG_DEBUG("getLoggedInUser: session expired: %s", sessionId.c_str());
        } else {
            User *u = UserBase::getUser(s.username);
            if (u) {
                LOG_DEBUG("valid logged-in user: %s", u->username.c_str());
                return *u;
            }
            // else session found, but related user no longer exists
        }
    }

    LOG_DEBUG("no valid logged-in user");
    return User();
}

int SessionBase::destroySession(const std::string &sessionId)
{
    ScopeLocker scopeLocker(SessionDb.locker, LOCK_READ_WRITE);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);
    if (i != SessionDb.sessions.end()) {
        LOG_DEBUG("Destroying session %s", sessionId.c_str());
        SessionDb.sessions.erase(i);
    } else {
        LOG_DEBUG("Destroying session: no such session '%s'", sessionId.c_str());
    }

    return 0;
}

/** Delete expired sessions
  */
void SessionBase::garbageCollect()
{
    std::map<std::string, Session>::iterator s = SessionDb.sessions.begin();
    std::map<std::string, Session>::iterator toBeErased;
    int count = 0;
    while(s != SessionDb.sessions.end()) {
        toBeErased = s;
        s ++;
        if (toBeErased->second.isExpired()) {
            SessionDb.sessions.erase(toBeErased);
            count ++;
        }
    }
    if (count > 0) LOG_INFO("Sessions garbage-collected: %d, remaining: %ld", count, L(SessionDb.sessions.size()));
}

std::string SessionBase::createSession(const std::string &username)
{
    LOCK_SCOPE(locker, LOCK_READ_WRITE);

    garbageCollect();

    Session s;
    s.ctime = time(0);
    // use sha1 in order to make difficult to predict session-id from previous session-ids
    // (an attacker cannot know the randoms, and thus cannot predict the next randoms)
    s.id = getSha1(getRandom8bytes());
    LOG_DEBUG("session-id: %s", s.id.c_str());
    s.username = username;
    s.duration = SESSION_DURATION;
    SessionDb.sessions[s.id] = s;
    return s.id;
}
