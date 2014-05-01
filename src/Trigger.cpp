
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include "Trigger.h"
#include "stringTools.h"
#include "parseConfig.h"
#include "db.h"
#include "logging.h"
#include "global.h"
#include "session.h"

#define K_TRIGGER "trigger"

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
    std::ostringstream files;
    FOREACH(p, entry.properties) {
        if (p->first == K_FILE) {
            if (files.str().size()) files << ",";
            files << toJson(p->second.front());
        }
    }
    if (files.str().size()) s << ",\n" << toJson("files") << ":[" << files << "]\n";

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
    FOREACH(p, issue.properties) {
        if (!pconfig.isValidPropertyName(p->first)) continue;

        if (p != issue.properties.begin()) s << ",\n";
        s << "  " << toJson(p->first) << ":[";
        s << toJson(pconfig.getLabelOfProperty(p->first)) << ",";
        if (p->second.size() == 1) {
            s << toJson(p->second.front());
        } else {
            // multiple values
            s << toJson(p->second);
        }
        s << "]";
    }
    s << "\n}";

    // put the message, if any
    s << ",\n" << toJson("message") << ":" << toJson(entry.getMessage());

    s << "\n}\n";
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
    signal(SIGCHLD, SIG_IGN); // ignore return values from child processes
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
        }
        exit(0);
    }
#endif
}

