#ifndef _db_h
#define _db_h

#include <string>
#include <map>
#include <list>
#include <set>

#include "ustring.h"

// Data types

// Entry
typedef struct Entry {
    ustring parent; // id of the parent entry, empty if top-level
    ustring id; // unique id of this entry
    int ctime; // creation time
    ustring author;
    std::map<ustring, ustring> singleProperties;
    std::map<ustring, std::list<ustring> > multiProperties;

} Entry;

// Issue
// An issue is consolidated over all its entries
struct Issue {
    ustring id; // same as the first entry
    ustring head; // the latest entry
    int ctime; // creation time (the one of the first entry)
    int mtime; // modification time (the one of the last entry)
    std::map<ustring, ustring> singleProperties; // properties of the issue
    std::map<ustring, std::list<ustring> > multiProperties;

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    void loadHead(const std::string &issuePath);
};

enum FieldType { F_TEXT, F_SELECT, F_MULTISELECT, F_SELECT_USER};
typedef struct FieldSpec {
    ustring name;
    enum FieldType type;
    std::list<ustring> selectOptions; // for F_SELECT and F_MULTISELECT only
} FieldSpec;

// Project config
struct ProjectConfig {
    ustring name; // name of the project
    std::map<ustring, FieldSpec> fields;
    std::map<ustring, ustring> customDisplays;
    ustring defaultDislpay; // one of customDisplays
    
};

// Functions
int db_init(const char * pathToRepository); // initialize the given repository

// load in memory the given project
// re-load if it was previously loaded
int loadProject(const char *path);


// search
//  project: name of project where the search should be conduected
//  fulltext: text that is searched (optional: 0 for no fulltext search)
//  filterSpec: "status:open+label:v1.0+xx:yy"
//  sortingSpec: "id+title-owner" (+ for ascending, - for descending order)
// @return number of issues 
//  When fulltext search is enabled (fulltext != 0) then the search is done
//  through all entries.
std::list<struct Issue> search(const char * project, const char *fulltext, const char *filterSpec, const char *sortingSpec);


// add an entry in the database
int add(const char *project, const char *issueId, const Entry &entry);

// Get a given issue and all its entries
int get(const char *project, const char *issueId, Issue &issue, std::list<Entry> &Entries);


// Deleting an entry is only possible if:
//     - this entry is the HEAD (has no child)
//     - the deleting happens less than 5 minutes after creation of the entry
// @return TODO
int deleteEntry(ustring entry);

ustring bin2hex(const ustring & in);

#endif
