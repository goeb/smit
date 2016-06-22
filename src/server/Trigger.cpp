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

#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>

#ifndef _WIN32
  #include <sys/wait.h>
#endif

#include "Trigger.h"
#include "utils/stringTools.h"
#include "utils/parseConfig.h"
#include "utils/logging.h"
#include "repository/db.h"
#include "global.h"
#include "user/session.h"


std::string toJson(const std::string &text)
{
    std::string json = text;
    json = replaceAll(json, '\\', "\\\\");
    json = replaceAll(json, '"', "\\\"");
    json = replaceAll(json, '/', "\\/");
    json = replaceAll(json, '\b', "\\b");
    json = replaceAll(json, '\f', "\\f");
    json = replaceAll(json, '\n', "\\n");
    json = replaceAll(json, '\r', "\\r");
    json = replaceAll(json, '\t', "\\t");
    json = "\"" + json + "\"";
    return json;
}

/** Convert a list of strings to a JSON array of strings
  */
std::string toJson(const std::list<std::string> &items)
{
    std::string jarray = "[";
    std::list<std::string>::const_iterator v;
    FOREACH(v, items) {
        if (v != items.begin()) {
            jarray += ", ";
        }
        jarray += toJson(*v);
    }
    jarray += "]";
    return jarray;

}


/** Format the text ti JSON, for the external program
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
    s << "{\n" << toJson("project") << ":" << toJson(project.getName()) << ",\n";
    s << toJson("issue") << ":" << toJson(issue.id) << ",\n";
    s << toJson("isNew") << ":" << ( (entry.parent == K_PARENT_NULL) ? "true":"false") << ",\n";
    s << toJson("entry") << ":" << toJson(entry.id) << ",\n";
    s << toJson("author") << ":" << toJson(entry.author) << ",\n";

    // put the users of the project
    std::map<std::string, Role>::const_iterator u;
    s << toJson("users") << ":" << "{\n";
    FOREACH(u, users) {
        if (u != users.begin()) s << ",\n";
        s << "  " << toJson(u->first) << ":" << toJson(roleToString(u->second));
    }
    s << "}";

    std::map<std::string, std::list<std::string> >::const_iterator p;

    // put the uploaded files, if any
    std::string files;
    p = entry.properties.find(K_FILE);
    if (p != entry.properties.end()) {
        files = toJson(p->second);
    }
    if (files.size() > 0) s << ",\n" << toJson("files") << ": " << files << "\n";

    // put the list of the properties modified by the entry
    s << ",\n" << toJson("modified") << ":[";
    std::ostringstream modifiedProperties;
    FOREACH(p, entry.properties) {
        if (p->first[0] != '+') {
            if (modifiedProperties.str().size()) modifiedProperties << ",";
            modifiedProperties << toJson(p->first);
        }
    }
    s << modifiedProperties.str() << "]";

    // put the properties of the issue
    // { property1 : [ label, value ],
    //   property2 : [ label, value ],
    //   ... }
    // (value may be a list, for multiselect)
    s << ",\n" << toJson("properties") << ":{\n";
    std::string consolidatedProps;
    FOREACH(p, issue.properties) {
        if (!pconfig.isValidPropertyName(p->first)) continue;

        if (!consolidatedProps.empty()) consolidatedProps += ",\n";
        consolidatedProps += "  " + toJson(p->first) + ":[";
        consolidatedProps += toJson(pconfig.getLabelOfProperty(p->first)) + ",";
        if (p->second.size() == 1) {
            consolidatedProps += toJson(p->second.front());
        } else {
            // multiple values
            consolidatedProps += toJson(p->second);
        }
        consolidatedProps += "]";
    }
    s << consolidatedProps << "\n}";

    // put the message, if any
    s << ",\n" << toJson("message") << ":" << toJson(entry.getMessage());

    s << "\n}\n";
    return s.str();
}

/** Run an external program for notifying a new entry
  */
void Trigger::notifyEntry(const Project &project, const Entry *entry)
{
    LOG_FUNC();
    if (!entry) return;
    if (!entry->issue) return;

    // load the 'trigger' file, in order to get the path of the external program
    std::string programPath;
    std::string trigger = project.getPath() + "/" + PATH_TRIGGER;
    std::ifstream triggerFile(trigger.c_str());
    std::getline(triggerFile, programPath);

    LOG_DEBUG("Trigger::notifyEntry: trigger=%s, programPath=%s", trigger.c_str(), programPath.c_str());

    if (programPath.size()) {
        // format the data that will be given to the external program on its stdin
        Issue *i = entry->issue;
        std::map<std::string, Role> users = UserBase::getUsersRolesOfProject(project.getName());
        std::string text = formatEntry(project, *i, *entry, users);

        run(programPath, text);
    }
}

void Trigger::run(const std::string &program, const std::string &toStdin)
{
    LOG_FUNC();
#ifndef _WIN32
    // TODO
    // As after a fork one should not call non async-safe functions, the following
    // code is incorrect, because of the logging and the popen).
    // It must be reworked as follows:
    // - remove logging
    // - create a pipe
    // - fork
    //
    // In child:
    // - chdir
    // - redirect the pipe to stdin (dup*)
    // - exec
    //
    // In parent:
    // - set O_NONBLOCK on the pipe (so that if the called process is broken, it does not block the smit server)
    // - write data to the pipe
    pid_t p = fork();
    if (p < 0) {
        LOG_ERROR("fork error: %s", strerror(errno));

    } else if (0 == p) {
        // Child process
        // Fork again so that:
        // - the child terminates quickly
        // - the grand-child new parent becomes 'init'
        // - the smit parent does not have to wait for its child
        pid_t p2 = fork();
        if (p2 < 0) {
            // TODO this logging should not be done as it is not async-safe
            LOG_ERROR("fork error (2): %s", strerror(errno));
            _exit(1);

        } else if (p2 > 0) {
            // Exit immediately in order to kill parenthood with the smit process
            // to have to wait for its child. Init will do it.
            _exit(0);

        } else if (0 == p2) {
            // Grand-child process
            int r = chdir(Database::Db.pathToRepository.c_str());
            if (r != 0) {
                // TODO this logging should not be done as it is not async-safe
                LOG_ERROR("Cannot chdir to repo '%s': %s", Database::Db.pathToRepository.c_str(),
                          strerror(errno));
                _exit(1);
            }
            FILE *fp;
            // TODO check if 'popen' is async-safe
            fp = popen(program.c_str(), "w");
            if (fp == NULL) {
                // TODO this logging should not be done as it is not async-safe
                LOG_ERROR("popen error: %s", strerror(errno));
                _exit(1);
            }

            size_t n = fwrite(toStdin.c_str(), 1, toStdin.size(), fp);
            if (n != toStdin.size()) {
                // TODO this logging should not be done as it is not async-safe
                LOG_ERROR("fwrite error: %lu bytes sent (%lu requested)", L(n), L(toStdin.size()));
            }

            pclose(fp);

            _exit(0);
        }
    }
    // Parent process continuing here...
    int wstatus;
    waitpid(p, &wstatus, 0);

#endif
}

