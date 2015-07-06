#ifndef _clone_h
#define _clone_h

#include "Args.h"


int cmdClone(int argc, char *const*argv);
int helpClone();

int cmdPull(int argc, char *const*argv);
int helpPull();

int cmdGet(int argc, char *const*argv);
int helpGet();

int cmdPush(int argc, char **argv);
int helpPush(const Args *args);

#endif
