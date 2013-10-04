#include <openssl/sha.h>
#include <stdlib.h>
#include <sstream>

#include "session.h"
#include "logging.h"
#include "identifiers.h"
#include "parseConfig.h"

// static members
SessionBase SessionBase::SessionDb;
UserBase UserBase::UserDb;

const char *FILE_USERS = "users";

User::User()
{
    superadmin = false;
}

void UserBase::load(const char *repository)
{
    std::string file = repository;
    file += "/";
    file += FILE_USERS;
    char *data;
    int n = loadFile(file.c_str(), &data);
    if (n < 0) {
        LOG_ERROR("Could not load user file.");
        return;
    }

    std::list<std::list<std::string> > lines = parseConfig(data, n);

    free(data);

    std::list<std::list<std::string> >::iterator line;
    for (line = lines.begin(); line != lines.end(); line++) {
        std::string verb = popListToken(*line);
        if (verb == "addUser") {
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
                    if (role == "ref") u.rolesOnProjects[project] = ROLE_REFERENCED;
                    else if (role == "ro") u.rolesOnProjects[project] = ROLE_RO;
                    else if (role == "rw") u.rolesOnProjects[project] = ROLE_RW;
                    else if (role == "admin") u.rolesOnProjects[project] = ROLE_ADMIN;
                    else {
                        LOG_ERROR("Invalid role '%s' on project 'project '%s'", role.c_str(), project.c_str());
                        error = true;
                        break; // abort line
                    }
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
                UserBase::addUser(u);

            }
        }
    }
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

    // add in table usersByProject

    // fill the usersByProject table
    std::map<std::string, enum Role>::iterator r;
    r = newUser.rolesOnProjects.begin();
    for (r = newUser.rolesOnProjects.begin(); r != newUser.rolesOnProjects.end(); r++) {

//        std::map<std::string, std::list<std::string> >::iterator p;
//        p = UserDb.usersByProject.find(project);
//        if (p == UserBase::usersByProject.end()) {
//            // initiate the record for this project
//            UserDb.usersByProject[project] = std::set<std::string>();
//        }
        UserDb.usersByProject[r->first].insert(newUser.username);
    }
}
std::set<std::string> UserBase::getUsersOfProject(std::string project)
{
    ScopeLocker(UserDb.locker, LOCK_READ_ONLY);

    std::map<std::string, std::set<std::string> >::iterator users;
    users = UserDb.usersByProject.find(project);
    if (users == UserDb.usersByProject.end()) return std::set<std::string>();
    else return users->second;
}

enum Role User::getRole(const std::string &project)
{
    std::map<std::string, enum Role>::iterator r = rolesOnProjects.find(project);
    if (r == rolesOnProjects.end()) return ROLE_NONE;
    else return r->second;
}


// return session id
std::string SessionBase::requestSession(const std::string &username, const std::string &passwd)
{
    User *u = UserBase::getUser(username);
    if (!u) return "";

    if (u->hashType == "sha1") {
        std::string sha1 = getSha1(passwd);
        if (sha1 != u->hashValue) {
            LOG_DEBUG("Sha1 do not match %s <> %s", sha1.c_str(), u->hashValue.c_str());
            return "";
        }

        // authentication succeeded
        // create session
        std::string sessid = SessionDb.createSession(username.c_str());
        return sessid;
    }


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
    return 0; // TODO
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
