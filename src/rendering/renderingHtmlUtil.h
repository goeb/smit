#ifndef _renderingHtmlUtil_h
#define _renderingHtmlUtil_h

#include <list>
#include <string>

#include "ContextParameters.h"

#define QS_GOTO_NEXT "next"
#define QS_GOTO_PREVIOUS "previous"

std::string getQsAddFilterOut(std::string qs, const std::string &propertyName, const std::string &propertyValue);
std::string getQsRemoveColumn(std::string qs, const std::string &property, const std::list<std::string> &defaultCols);
std::string getQsSubSorting(std::string qs, const std::string &property, bool exclusive);
std::string getPropertyForGrouping(const ProjectConfig &pconfig, const std::string &sortingSpec, PropertyType &type);
std::string queryStringAdd(const ResponseContext *req, const char *param);
std::string convertToRichTextWholeline(const std::string &in, const char *start, const char *htmlTag, const char *htmlClass);
std::string convertToRichTextInline(const std::string &in, const char *begin, const char *end,
                                    bool dropDelimiters, bool multiline, const char *htmlTag, const char *htmlClass);
bool isImage(const std::string &filename);
void printFilters(const ContextParameters &ctx);
std::string jsSetUserCapAndRole(const ContextParameters &ctx);


#endif
