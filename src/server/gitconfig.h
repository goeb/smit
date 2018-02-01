#ifndef _gitconfig_h
#define _gitconfig_h

int setupGitHookDenyCurrentBranch(const char *dir);
int setupGitServerSideHooks(const char *repo);


#endif
