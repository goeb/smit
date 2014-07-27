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
#include <stdlib.h>
#include <sstream>

#include "session.h"
#include "logging.h"
#include "identifiers.h"
#include "parseConfig.h"
#include "global.h"
#include "db.h"

// static members
SessionBase SessionBase::SessionDb;
UserBase UserBase::UserDb;
std::string UserBase::Repository;
bool UserBase::localUserInterface = false;

const char *FILE_USERS = "users";

User::User()
{
    superadmin = false;
}
std::string User::serialize()
{
    std::string result;

    result += "addUser ";
    result += serializeSimpleToken(username) + " ";
    if (superadmin) result += "superadmin \\\n";
    if (!hashType.empty()) {
        result += "    " + serializeSimpleToken(hashType) + " ";
        result += serializeSimpleToken(hashValue);
        if (!hashSalt.empty()) {
            result += " -salt " ;
            result += serializeSimpleToken(hashSalt);
        }
        result += " \\\n";
    }

    std::map<std::string, enum Role>::const_iterator role;
    FOREACH(role, rolesOnProjects) {
        std::string p = "    project " + serializeSimpleToken(role->first) + " ";
        p += roleToString(role->second);
        result += p + " \\\n";
    }
    result += "\n\n";
    return result;
}


std::string getNewSalt()
{
    std::stringstream randomStr;
    randomStr << std::hex << rand() << rand();
    return randomStr.str();
}

void User::setPasswd(const std::string &password)
{
    hashType = HASH_SHA1;
    hashSalt = getNewSalt();
    hashValue = getSha1(password + hashSalt);
}


std::string roleToString(Role r)
{
    if (r == ROLE_ADMIN) return "admin";
    else if (r == ROLE_RW) return "rw";
    else if (r == ROLE_RO) return "ro";
    else if (r == ROLE_REFERENCED) return "ref";
    else return "none";
}

Role stringToRole(const std::string &s)
{
    if (s == "ref") return ROLE_REFERENCED;
    else if (s == "ro") return ROLE_RO;
    else if (s == "rw") return ROLE_RW;
    else if (s == "admin") return ROLE_ADMIN;
    else return ROLE_NONE;
}


std::list<std::string> getAvailableRoles()
{
    std::list<std::string> result;
    result.push_back(roleToString(ROLE_REFERENCED));
    result.push_back(roleToString(ROLE_RO));
    result.push_back(roleToString(ROLE_RW));
    result.push_back(roleToString(ROLE_ADMIN));
    return result;
}

/** Load the users from file storage
  */
int UserBase::init(const char *path, bool checkProject)
{
    // init random seed
    srand(time(0));

    Repository = path;
    std::string file = Repository;
    file += "/";
    file += FILE_USERS;
    const char *data;
    int n = loadFile(file.c_str(), &data);
    if (n < 0) {
        LOG_ERROR("Could not load user file.");
        return -1;
    }

    std::list<std::list<std::string> > lines = parseConfigTokens(data, n);

    free((void*)data);

    std::list<std::list<std::string> >::iterator line;
    for (line = lines.begin(); line != lines.end(); line++) {
        std::string verb = popListToken(*line);
        if (verb == K_SMIT_VERSION) {
            std::string v = popListToken(*line);
            LOG_DEBUG("Smit version for user file: %s", v.c_str());

        } else if (verb == "addUser") {
            User u;
            u.username = popListToken(*line);
            if (u.username.empty()) {
                LOG_ERROR("Incomplete 'addUser'");
                continue;
            }

            while (! line->empty()) {
                std::string token = popListToken(*line);
                if (token == "project") {
                    // get project name and access right
                    std::string project = popListToken(*line);
                    std::string role = popListToken(*line);
                    if (project.empty() || role.empty()) {
                        LOG_ERROR("Incomplete project access %s/%s", project.c_str(), role.c_str());
                        continue;
                    }
                    // check if project exists
                    if (checkProject) {
                        Project *p = Database::getProject(project);
                        if (!p) {
                            LOG_ERROR("Invalid project name '%s' for user %s", project.c_str(), u.username.c_str());
                            continue;
                        }
                    }

                    Role r = stringToRole(role);
                    if (r == ROLE_NONE) {
                        LOG_ERROR("Invalid role '%s'", role.c_str());
                        continue;
                    }
                    u.rolesOnProjects[project] = r;

                } else if (token == "sha1") {
                    std::string hash = popListToken(*line);
                    if (hash.empty()) {
                        LOG_ERROR("Empty hash for user %s", u.username.c_str());
                        continue;
                    }
                    u.hashType = token;
                    u.hashValue = hash;
                } else if (token == "-salt") {
                    u.hashSalt = popListToken(*line);

                } else if (token == "superadmin") u.superadmin = true;
            }
            // add user in database
            LOG_DEBUG("Loaded user: %s on %zu projects", u.username.c_str(), u.rolesOnProjects.size());
            UserBase::addUserInArray(u);
        }
    }
    return 0;
}

void UserBase::setLocalUserInterface()
{
    localUserInterface = true;
}

int UserBase::initUsersFile(const char *repository)
{
    std::string path = repository;
    path += "/";
    path += FILE_USERS;

    return writeToFile(path.c_str(), "");
}


int UserBase::store(const std::string &repository)
{
    std::string result;
    result = K_SMIT_VERSION " " VERSION "\n";

    std::map<std::string, User*>::iterator uit;
    FOREACH(uit, UserDb.configuredUsers) {
        result += uit->second->serialize();
    }
    std::string path = repository;
    path += "/";
    path += FILE_USERS;
    return writeToFile(path.c_str(), result);
}


User* UserBase::getUser(const std::string &username)
{
    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);
    std::map<std::string, User*>::iterator u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return 0;
    else return u->second;
}

/** Add a new user in database
  */
void UserBase::addUserInArray(User newUser)
{
    User *u = new User;
    *u = newUser;

    // delete old user with same name, if any
    std::map<std::string, User*>::iterator uit = UserDb.configuredUsers.find(u->username);
    if (uit != UserDb.configuredUsers.end()) delete uit->second;

    UserDb.configuredUsers[u->username] = u;
}

/** Add a new user in database and store it.
  */
int UserBase::addUser(User newUser)
{
    if (newUser.username.empty()) return -1;

    // check if user already exists

    std::map<std::string, User*>::iterator uit = UserDb.configuredUsers.find(newUser.username);
    if (uit != UserDb.configuredUsers.end()) {
        return -2;
    }

    ScopeLocker(UserDb.locker, LOCK_READ_WRITE);
    addUserInArray(newUser);
    return store(Repository);
}

/** Get the list of users that are at stake in the given project
  */
std::set<std::string> UserBase::getUsersOfProject(const std::string &project)
{
    std::set<std::string> result;
    if (localUserInterface) return result;

    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);

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
    if (localUserInterface) return result;

    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        enum Role r = u->second->getRole(project);
        if (r == ROLE_NONE) continue;

        result[u->first] = r;
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

    ScopeLocker(UserDb.locker, LOCK_READ_WRITE);

    std::map<std::string, User*>::iterator u;

    // if modified name, check that the new name does not exist
    if (newConfig.username != username) {
        u = UserDb.configuredUsers.find(newConfig.username);
        if (u != UserDb.configuredUsers.end()) return -2;
    }

    // check that modified user exists
    u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return -3;

    if (newConfig.hashValue.empty()) {
        // keep the same as before
        newConfig.hashType = u->second->hashType;
        newConfig.hashValue = u->second->hashValue;
    }
    *(u->second) = newConfig;

    // change the key in the map if name of user was modified
    if (username != newConfig.username) {
        UserDb.configuredUsers[newConfig.username] = u->second;
        UserDb.configuredUsers.erase(username);
    }

    return store(Repository);
}

int UserBase::updatePassword(const std::string &username, const std::string &password)
{
    ScopeLocker(UserDb.locker, LOCK_READ_WRITE);
    std::map<std::string, User*>::iterator u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return -1;

    u->second->setPasswd(password);
    return store(Repository);
}

std::list<User> UserBase::getAllUsers()
{
    std::list<User> result;

    if (localUserInterface) return result;

    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);
    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        result.push_back(*(u->second));
    }
    return result;
}


static UserBase UserDb;
std::map<std::string, User*> configuredUsers;


/** Get the role of the user for the given project
  */
enum Role User::getRole(const std::string &project)
{
    if (UserBase::isLocalUserInterface()) return ROLE_RO;

    std::map<std::string, enum Role>::iterator r = rolesOnProjects.find(project);
    if (r == rolesOnProjects.end()) return ROLE_NONE;
    else return r->second;
}

/** Get the projects where the user has access (read or write)
  * List of pairs (project, role)
  */
std::list<std::pair<std::string, std::string> > User::getProjects()
{
    std::list<std::pair<std::string, std::string> > result;
    std::map<std::string, enum Role>::iterator r;
    for (r = rolesOnProjects.begin(); r != rolesOnProjects.end(); r++) {
        result.push_back(std::make_pair(r->first, roleToString(r->second)));
    }
    return result;
}


/** Check user credentials and initiate a session
  *
  */
std::string SessionBase::requestSession(const std::string &username, const std::string &passwd)
{
    if (UserBase::isLocalUserInterface()) return "local";

    LOG_DEBUG("Requesting session: username=%s, password=%s", username.c_str(), passwd.c_str());
    std::string sessid = ""; // empty session id indicates that no session is on
    User *u = UserBase::getUser(username);

    if (u && u->hashType == HASH_SHA1) {
        LOG_DEBUG("salt=%s", u->hashSalt.c_str());
        std::string sha1 = getSha1(passwd + u->hashSalt);
        if (sha1 != u->hashValue) {
            LOG_DEBUG("Sha1 do not match %s <> %s", sha1.c_str(), u->hashValue.c_str());
            sessid = "";

        } else {
            // authentication succeeded, create session
            sessid = SessionDb.createSession(username.c_str());
            LOG_DEBUG("Session created for '%s': %s", username.c_str(), sessid.c_str());
        }
    }
    return sessid;
}

/** Return a user object
  *
  * If no valid user if found, then the returned object has an empty username.
  */
User SessionBase::getLoggedInUser(const std::string &sessionId)
{
    if (UserBase::isLocalUserInterface()) {
        User u;
        u.username = "local user";
        return u;
    }

    LOG_DEBUG("getLoggedInUser(%s)...", sessionId.c_str());
    ScopeLocker(SessionDb.locker, LOCK_READ_ONLY);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);

    if (i != SessionDb.sessions.end()) {
        // session found
        Session s = i->second;
        if (time(0) - s.ctime > s.duration) {
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
    ScopeLocker(SessionDb.locker, LOCK_READ_WRITE);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);
    if (i != SessionDb.sessions.end()) {
        LOG_DEBUG("Destroying session %s", sessionId.c_str());
        SessionDb.sessions.erase(i);
    } else {
        LOG_DEBUG("Destroying session: no such session '%s'", sessionId.c_str());
    }

    return 0;
}

std::string SessionBase::createSession(const std::string &username)
{
    ScopeLocker(locker, LOCK_READ_WRITE);

    std::stringstream randomStr;
    randomStr << std::hex << rand() << rand() << rand();
    Session s;
    s.ctime = time(0);
    s.id = randomStr.str();
    LOG_DEBUG("session-id: %s", s.id.c_str());
    s.username = username;
    s.duration = SESSION_DURATION;
    SessionDb.sessions[s.id] = s;
    return s.id;
};
