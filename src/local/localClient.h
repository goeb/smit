#ifndef _localClient_h
#define _localClient_h

int cmdIssue(int argc, char *const*argv);
int helpIssue();

// local .smit database (in a local cloned repository)
#define SMIT_LOCAL ".smit_local"
#define PATH_USERNAME   SMIT_LOCAL "/username"
#define PATH_SESSID     SMIT_LOCAL "/sessid"
#define PATH_LOCAL_PERM SMIT_LOCAL "/permissions"
#define PATH_GIT_CREDENTIAL        SMIT_LOCAL "/git_credential"
#define PATH_GIT_CONFIG        SMIT_LOCAL "/gitconfig"

std::string loadUsername(const std::string &clonedRepo);
void storeUsername(const std::string &dir, const std::string &username);



#endif
