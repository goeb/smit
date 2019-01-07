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
#include "utils/filesystem.h"
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

std::string toJson(const PropertiesMap &properties)
{
    std::string jobject = "{";
    PropertiesIt p = properties.begin();
    FOREACH(p, properties) {
        if (p != properties.begin()) jobject += ",";
        jobject += toJson(p->first) + ":" + toJson(p->second);
    }
    jobject += "}";
    return jobject;
}

/** Format the text to JSON, for feeding to the external program
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
std::string Trigger::formatEntry(const Project &project, const std::string &issueId, const IssueCopy &oldIssue, const Entry &entry,
                                 const std::list<Recipient> &recipients)
{
    //
    // Typical output:
    // { "project": "...",
    //   "issue_id": "...",
    //   "old_issue" : { "id": "...",
    //                  "properties": { ... },
    //                },
    //   "entry": { "id": "...",
    //              "ctime": "...",
    //              "author": "...",
    //              "properties": { ... }
    //            },
    //   "recipients": [ { "email": "...",
    //                     "gpg_pub_key", "..." },
    //                   ...
    //                 ],
    //   "properties_labels": { <property_id>: <property_label>,
    //                          ...
    //                        }
    //
    //
    // Notes:
    // - if old_issue is null, it denotes a new issue
    // - gpg_pub_key may be null
    //
    std::ostringstream s;
    s << "{";

    s << toJson("project") << ":" << toJson(project.getName()) << ",\n";
    s << toJson("issue_id") << ":" << toJson(issueId) << ",\n";

    s << toJson("old_issue") << ":";
    if (oldIssue.id.empty()) s << "null"; // no old issue
    else {
        s << "{";
        s << toJson("id") << ":" << toJson(oldIssue.id) << ",\n";
        s << toJson("properties") << ":" << toJson(oldIssue.properties);
        s << "}";
    }
    s << ",\n";

    s << toJson("entry") << ":";
    s << "{";
    s << toJson("id") << ":" << toJson(entry.id) << ",\n";
    //s << toJson("ctime") << ":" << toJson(entry.ctime) << ",\n";
    s << toJson("author") << ":" << toJson(entry.author) << ",\n";
    s << toJson("properties") << ":" << toJson(entry.properties);
    s << "},\n";

    // recipients
    s << toJson("recipients") << ":";
    s << "[";
    std::list<Recipient>::const_iterator r;
    FOREACH(r, recipients) {
        if (r != recipients.begin()) s << ",";
        s << "{";
        s << toJson("email") << ":" << toJson(r->email);
        s << ",";
        s << toJson("gpg_pub_key") << ":" << toJson(r->gpgPubKey);
        s << "}";
    }
    s << "],";

    // properties_labels
    ProjectConfig pconfig = project.getConfig();
    std::map<std::string, std::string>::const_iterator plabel;
    s << toJson("properties_labels") << ":";
    s << "{";
    FOREACH(plabel, pconfig.propertyLabels) {
        if (plabel != pconfig.propertyLabels.begin()) s << ",";
        s << toJson(plabel->first) << ":" << toJson(plabel->second);
    }
    s << "}"; // end of labels

    s << "}"; // end of main object

   return s.str();
}

/** Run an external program for notifying a new entry
  */
void Trigger::notifyEntry(const Project &project, const std::string &issueId, const Entry *entry,
                          const IssueCopy &oldIssue, const std::list<Recipient> &recipients)
{
    LOG_FUNC();
    if (!entry) return;

    // load the 'trigger' file, in order to get the path of the external program
    std::string cmdline = project.getTriggerCmdline();

    if (cmdline.empty()) return; // no notification

    LOG_DIAG("Trigger::notifyEntry: cmdline=%s", cmdline.c_str());

    // format the data that will be given to the external program on its stdin
    std::map<std::string, Role> users = UserBase::getUsersRolesOfProject(project.getName());
    std::string text = formatEntry(project, issueId, oldIssue, *entry, recipients);

    run(cmdline, text);
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

