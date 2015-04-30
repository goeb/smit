#ifndef _Issue_h
#define _Issue_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "ustring.h"
#include "mutexTools.h"
#include "stringTools.h"
#include "Entry.h"

#define DIR_DELETED_OLD "_del"

#define K_PARENT_NULL "null"

#define DELETE_DELAY_S (10*60) // seconds

enum PropertyType {
    F_TEXT,
    F_SELECT,
    F_MULTISELECT,
    F_SELECT_USER,
    F_TEXTAREA,
    F_TEXTAREA2,
    F_ASSOCIATION
};

class Project; // forward declaration

// Issue
// An issue is consolidated over all its entries
struct Issue {
    std::string id; // same as the first entry
    std::string path; // path of the directory where the issue is stored
    Entry *first; // the first entry
    Entry *latest; // the latest entry
    std::string project; // name of the project
    int ctime; // creation time (the one of the first entry)
    int mtime; // modification time (the one of the last entry)
    std::map<std::string, std::list<std::string> > properties;

    /** { association-name : [related issues] } */
    Issue() : first(0), latest(0), ctime(0), mtime(0) {}

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    std::string getSummary() const;
    bool lessThan(const Issue *other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    bool isInFilter(const std::map<std::string, std::list<std::string> > &filter) const;

    void consolidate();
    void consolidateWithSingleEntry(Entry *e, bool overwrite);
    void consolidateAmendment(Entry *e);
    bool searchFullText(const char *text) const;
    int getNumberOfTaggedIEntries(const std::string &tagId) const;
    Entry *getEntry(const std::string id);

    void addEntryInTable(Entry *e);
    void addEntry(Entry *e);
    static Issue *load(const std::string &objectsDir, const std::string &latestEntryOfIssue);
    void insertEntry(Entry *e);
    Entry *amendEntry(const std::string &entryId, const std::string &newMsg, const std::string &username);
    static void sort(std::vector<const Issue*> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec);
};


class IssueComparator {
public:
    IssueComparator(const std::list<std::pair<bool, std::string> > &sSpec) : sortingSpec(sSpec) { }
    inline bool operator() (const Issue* i, const Issue* j) { return i->lessThan(j, sortingSpec); }
private:
    const std::list<std::pair<bool, std::string> > &sortingSpec;
};



#endif
