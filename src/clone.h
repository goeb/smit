#ifndef _clone_h
#define _clone_h

// local .smit database (in a local cloned repository)
#define SMIT_DIR ".smit"
#define PATH_USERNAME   SMIT_DIR "/username"
#define PATH_SESSID     SMIT_DIR "/sessid"
#define PATH_URL        SMIT_DIR "/remote"

std::string loadUsername(const std::string &clonedRepo);


int cmdClone(int argc, char *const*argv);
int helpClone();

int cmdPull(int argc, char *const*argv);
int helpPull();

int cmdGet(int argc, char *const*argv);
int helpGet();

int cmdPush(int argc, char *const*argv);
int helpPush();

#endif
