#ifndef _View_h
#define _View_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "ustring.h"
#include "mutexTools.h"
#include "stringTools.h"



struct PredefinedView {
    std::string name;
    std::map<std::string, std::list<std::string> > filterin;
    std::map<std::string, std::list<std::string> > filterout;
    std::string colspec;
    std::string sort;
    std::string search;
    int limit;
    bool isDefault; // indicate if this view should be chosen by default when query string is empty

    PredefinedView() : isDefault(false) {}
    static std::string getDirectionName(bool d);
    static std::string getDirectionSign(const std::string &text);
    std::string generateQueryString() const;
    static PredefinedView loadFromQueryString(const std::string &q);
    std::string serialize() const;
    static std::string serializeViews(const std::map<std::string, PredefinedView> &views);

    static void parsePredefinedViews(std::list<std::list<std::string> > lines,
                                     std::map<std::string, PredefinedView> &views);

    static int loadViews(const std::string &path, std::map<std::string, PredefinedView> &views);
    static std::map<std::string, PredefinedView> getDefaultViews();

};


#endif
