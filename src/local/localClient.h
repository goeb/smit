#ifndef _localClient_h
#define _localClient_h

int cmdIssue(int argc, char *const*argv);
int helpIssue();

// local .smit database (in a local cloned repository)
#define SMIT_DIR ".smit"
#define PATH_USERNAME   SMIT_DIR "/username"
#define PATH_SESSID     SMIT_DIR "/sessid"
#define PATH_URL        SMIT_DIR "/remote"

std::string loadUsername(const std::string &clonedRepo);
void storeUsername(const std::string &dir, const std::string &username);



#endif
