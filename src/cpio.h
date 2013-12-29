#include <stdio.h>

FILE *cpioOpenArchive(const char *file);
int cpioGetFile(FILE* cpioArchiveFd, const char *file);
int cpioExtractFile(const char *exe, const char *src, const char *dst);
