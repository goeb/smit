
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include "Trigger.h"
#include "stringTools.h"
#include "parseConfig.h"
#include "db.h"
#include "logging.h"
#include "global.h"
#include "session.h"

#define K_TRIGGER "trigger"

/** Format the text for the external program
  *
  * First, some info that is not in the properties of the issue:
  *     project name, issue id, entry id, author,
  *     users of the project
  *     files attached to the entry
  *     modified properties
  * Then, all the properties, in the form: logical-name label value ...
  *     all on one line
  * Finally, the message, if any
  *
  */
std::string Trigger::formatEntry(const Project &project, const Issue &issue, const Entry &entry,
                                 const std::map<std::string, Role> &users)
{
    ProjectConfig pconfig = project.getConfig();

    std::ostringstream s;
    s << "+project " << project.getName() << "\n";
    s << "+issue " << issue.id << "\n";
    s << "+entry " << entry.id << "\n";
    s << "+author " << entry.author << "\n";

    // put the users of the project
    std::map<std::string, Role>::const_iterator u;
    std::string users_rw, users_ro, users_admin, users_ref;
    FOREACH(u, users) {
        std::string uname = serializeSimpleToken(u->first);
        switch (u->second) {
        case ROLE_REFERENCED: users_ref += " " + uname; break;
        case ROLE_RO: users_ro += " " + uname; break;
        case ROLE_RW: users_rw += " " + uname; break;
        case ROLE_ADMIN: users_admin += " " + uname; break;
        // default: ignore
        }
    }
    s << "+user.admin" << users_admin << "\n";
    s << "+user.rw" << users_rw << "\n";
    s << "+user.ro" << users_ro << "\n";
    s << "+user.ref" << users_ref << "\n";

    std::map<std::string, std::list<std::string> >::const_iterator p;

    // put the uploaded files, if any
    std::ostringstream files;
    FOREACH(p, entry.properties) {
        if (p->first == K_FILE) files << " " << p->second.front();
    }
    if (files.str().size()) s << K_FILE << files;

    // put the list of the properties modified by the entry
    s << "+modified";
    FOREACH(p, entry.properties) {
        if (p->first[0] != '+') s << " " << p->first;
    }
    s << "\n";

    // put the properties of the issue
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, pconfig.properties) {
        // check if
        s << pspec->name << " ";
        s << serializeSimpleToken(pspec->label);
        p = issue.properties.find(pspec->name);
        if (p != issue.properties.end()) {
            std::list<std::string>::const_iterator value;
            FOREACH(value, p->second) {
                s << " " << serializeSimpleToken(*value);
            }
        }
        s << "\n";
    }

    // put the message, if any
    std::string msg = entry.getMessage();
    if (msg.size()) s << K_MESSAGE << "\n" << msg << "\n";

    return s.str();
}

/** Run an external program for notifying a new entry
  */
void Trigger::notifyEntry(const Project &project, const std::string issueId, const std::string &entryId)
{
    LOG_FUNC();
    // load the 'trigger' file, in order to get the path of the external program
    std::string programPath;
    std::string trigger = project.getPath() + "/" + K_TRIGGER;
    std::ifstream triggerFile(trigger.c_str());
    std::getline(triggerFile, programPath);

    LOG_DEBUG("Trigger::notifyEntry: trigger=%s, programPath=%s", trigger.c_str(), programPath.c_str());

    if (programPath.size()) {
        // format the data that will be given to the external program on its stdin
        Issue i;
        int r = project.get(issueId, i);
        if (r != 0) {
            LOG_ERROR("notifyEntry: could not retrieved issue: project '%s', issue '%s'",
                      project.getName().c_str(), issueId.c_str());
            return;
        }

        Entry *e = i.getEntry(entryId);
        if (!e) {
            LOG_ERROR("notifyEntry: could not retrieved entry: project '%s', issue '%s', entry '%s'",
                      project.getName().c_str(), issueId.c_str(), entryId.c_str());
            return;
        }

        // TODO add the users of the project

        std::map<std::string, Role> users = UserBase::getUsersRolesOfProject(project.getName());
        std::string text = formatEntry(project, i, *e, users);

        run(programPath, text);
    }
}

void Trigger::run(const std::string &program, const std::string &toStdin)
{
    LOG_FUNC();
#ifndef _WIN32
    pid_t p = fork();
    if (p) {
        // in parent
        // do nothing
    } else {
        LOG_DEBUG("chdir '%s'...", Database::Db.pathToRepository.c_str());
        int r = chdir(Database::Db.pathToRepository.c_str());
        if (r != 0) {
            LOG_ERROR("Cannot chdir to repo '%s': %s", Database::Db.pathToRepository.c_str(),
                      strerror(errno));
            return;
        }
        FILE *fp;
        fp = popen(program.c_str(), "w");
        if (fp == NULL) {
            LOG_ERROR("popen error: %s", strerror(errno));
        } else {
            size_t n = fwrite(toStdin.c_str(), 1, toStdin.size(), fp);
            if (n != toStdin.size()) {
                LOG_ERROR("fwrite error: %d bytes sent (%d requested)", n, toStdin.size());
            }
            pclose(fp);
            exit(0);
        }
    }
#endif
}

