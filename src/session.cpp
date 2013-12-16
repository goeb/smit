/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
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
        result += serializeSimpleToken(hashType) + " ";
        result += serializeSimpleToken(hashValue) + " \\\n";
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

void User::setPasswd(const std::string &password)
{
    hashType = HASH_SHA1;
    hashValue = getSha1(password);
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
int UserBase::load(const char *path)
{
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

            bool error = false;
            while (! line->empty()) {
                std::string token = popListToken(*line);
                if (token == "project") {
                    // get project name and access right
                    std::string project = popListToken(*line);
                    std::string role = popListToken(*line);
                    if (project.empty() || role.empty()) {
                        LOG_ERROR("Incomplete project access %s/%s", project.c_str(), role.c_str());
                        error = true;
                        break; // abort line
                    }
                    // check if project exists
                    Project *p = Database::getProject(project);
                    if (!p) {
                        LOG_ERROR("Invalid project name '%s' for user %s", project.c_str(), u.username.c_str());
                        error = true;
                        break; // abort line
                    }

                    Role r = stringToRole(role);
                    if (r == ROLE_NONE) {
                        LOG_ERROR("Invalid role '%s'", role.c_str());
                        error = true;
                        break; // abort line
                    }
                    u.rolesOnProjects[project] = r;

                } else if (token == "sha1") {
                    std::string hash = popListToken(*line);
                    if (hash.empty()) {
                        LOG_ERROR("Empty hash");
                        error = true;
                        break; // abort line
                    }
                    u.hashType = token;
                    u.hashValue = hash;
                } else if (token == "superadmin") u.superadmin = true;
            }
            if (!error) {
                // add user in database
                LOG_DEBUG("Loaded user: %s on %d projects", u.username.c_str(), u.rolesOnProjects.size());
                UserBase::addUserInArray(u);
            }
        }
    }
    return 0;
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

    // add in table usersByProject

    // fill the usersByProject table
    std::map<std::string, enum Role>::iterator r;
    FOREACH(r, newUser.rolesOnProjects) {
        UserDb.usersByProject[r->first].insert(newUser.username);
    }
}

/** Add a new user in database and store it.
  */
int UserBase::addUser(User newUser)
{
    ScopeLocker(UserDb.locker, LOCK_READ_WRITE);

    addUserInArray(newUser);
    return store(Repository);
}

/** Get the list of users that are at stake in the given project
  */
std::set<std::string> UserBase::getUsersOfProject(const std::string &project)
{
    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, std::set<std::string> >::iterator users;
    users = UserDb.usersByProject.find(project);
    if (users == UserDb.usersByProject.end()) return std::set<std::string>();
    else return users->second;
    }

/** Get the list of users that are at stake in the given project
  */
std::map<std::string, Role> UserBase::getUsersRolesOfProject(const std::string &project)
{
    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, Role> result;
    std::map<std::string, User*>::const_iterator u;
    FOREACH(u, UserDb.configuredUsers) {
        enum Role r = u->second->getRole(project);
        if (r == ROLE_NONE) continue;

        result[u->first] = r;
    }
    return result;
}

int UserBase::updateUser(const std::string &username, User newConfig)
{
    ScopeLocker(UserDb.locker, LOCK_READ_WRITE);

    std::map<std::string, User*>::iterator u = UserDb.configuredUsers.find(username);
    if (u == UserDb.configuredUsers.end()) return -1;

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
    std::map<std::string, enum Role>::iterator r = rolesOnProjects.find(project);
    if (r == rolesOnProjects.end()) return ROLE_NONE;
    else return r->second;
}

/** Get the projects where the user has access (read or write)
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
    LOG_DEBUG("Requesting session: username=%s, password=%s", username.c_str(), passwd.c_str());
    std::string sessid = ""; // empty session id indicates that no session is on
    User *u = UserBase::getUser(username);

    if (u && u->hashType == HASH_SHA1) {
        std::string sha1 = getSha1(passwd);
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
    LOG_DEBUG("getLoggedInUser(%s)...", sessionId.c_str());
    ScopeLocker(SessionDb.locker, LOCK_READ_ONLY);

    std::map<std::string, Session>::iterator i = SessionDb.sessions.find(sessionId);

    if (i == SessionDb.sessions.end()) return User();
    else {
        Session s = i->second;
        if (time(0) - s.ctime > s.duration) {
            // session expired
            LOG_DEBUG("getLoggedInUser: session expired: %s", sessionId.c_str());
            return User();
        } else {
            User *u = UserBase::getUser(s.username);
            if (u) return *u;
            else return User();
        }
    }
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
    randomStr << rand() << rand() << rand();
    Session s;
    s.ctime = time(0);
    s.id = randomStr.str();
    s.username = username;
    s.duration = SESSION_DURATION;
    SessionDb.sessions[s.id] = s;
    return s.id;
};
