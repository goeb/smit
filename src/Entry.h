#ifndef _Entry_h
#define _Entry_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "ustring.h"
#include "stringTools.h"
#include "Object.h"

#define K_MESSAGE     "+message" // keyword used for the message
#define K_FILE        "+file" // keyword used for uploaded files
#define K_SUMMARY     "summary"
#define K_AMEND       "+amend"
#define K_PARENT_NULL "null"
#define K_TAG         "+tag"

#define DELETE_DELAY_S (10*60) // seconds

class Issue;

// Entry
class Entry : public Object {
public:
    std::string parent; // id of the parent entry, empty if top-level

    /** The id of an entry
      * - must not start by a dot (reserved for hidden file)
      * - must notcontain characters forbidden for file names (Linux and Windows):
      *        <>:"/\|?*
      * - must be unique case insensitively (as HTML identifiers are case insensitive)
      *
      * The ids of entries in the current implementation contain: lower case letters and digits
      */
    std::string id; // unique id of this entry
    long ctime; // creation time
    std::string author;
    PropertiesMap properties;
    // chainlist pointers
    struct Entry *next; // child
    struct Entry *prev; // parent
    Issue *issue;
    std::list<std::string> amendments; // id of the entries that amend this entry
    std::set<std::string> tags;

    Entry() : ctime(0), next(0), prev(0), issue(0) {}
    static Entry *loadEntry(const std::string &path, const std::string &id, bool checkId=false);
    void setId();
    std::string serialize() const;
    int getCtime() const;
    std::string getMessage() const;
    bool isAmending() const;
    inline std::string getSubpath() const { return Object::getSubpath(id); }
    static inline std::string getSubpath(const std::string identifier) { return Object::getSubpath(identifier); }
    static Entry *createNewEntry(const PropertiesMap &props, const std::string &author, const Entry *eParent);

};

#endif
