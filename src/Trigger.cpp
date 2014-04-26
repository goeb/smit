
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

#define K_TRIGGER "trigger"

/** Format the text for the external program
  */
std::string Trigger::formatEntry(const Project &project, const Issue &issue, const Entry &entry)
{
    std::ostringstream s;
    s << "+project " << project.getName() << "\n";
    s << "+issue " << issue.id << "\n";
    s << "+entry " << entry.id << "\n";
    s << "+author " << entry.author << "\n";

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
    FOREACH(p, issue.properties) {
        s << p->first << " " << toString(p->second, " ") << "\n";
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
    std::string trigger = Database::Db.pathToRepository + "/" + K_TRIGGER;
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
        std::string text = formatEntry(project, i, *e);

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

