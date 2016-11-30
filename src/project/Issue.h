#ifndef _Issue_h
#define _Issue_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "utils/ustring.h"
#include "utils/mutexTools.h"
#include "utils/stringTools.h"
#include "Entry.h"

#define DIR_DELETED_OLD "_del"

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

enum FilterMode {
    FILTER_IN,
    FILTER_OUT
};

typedef std::string IssueId;
typedef std::string AssociationId;

// Issue
// An issue is consolidated over all its entries
struct Issue {
    std::string id; // same as the first entry
    Entry *first; // the first entry
    std::string project; // name of the project
    int ctime; // creation time (the one of the first entry)

    // mutable members, that may be modified when a user posts an entry
    int mtime; // modification time (the one of the last entry)
    PropertiesMap properties;
    Entry *latest; // the latest entry
    std::map<std::string, std::list<std::string> > amendments; // key: amended entry-id, value: amending entries
    std::map<std::string, std::set<std::string> > tags; // key: entry-id, value: tags

    Issue() : first(0), ctime(0), mtime(0), latest(0) {}

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    std::string getSummary() const;
    bool isInFilter(const std::map<std::string, std::list<std::string> > &filter, FilterMode mode) const;

    void consolidate();
    void consolidateWithSingleEntry(Entry *e);
    void consolidateAmendment(Entry *e);
    bool searchFullText(const char *text) const;
    int getNumberOfTaggedIEntries(const std::string &tagname) const;
    void toggleTag(const std::string &entryId, const std::string &tagname);
    bool hasTag(const std::string &entryId, const std::string &tagname) const;

    void addEntryInTable(Entry *e);
    void addEntry(Entry *e);
    static Issue *load(const std::string &objectsDir, const std::string &latestEntryOfIssue);
    void insertEntry(Entry *e);
    Entry *amendEntry(const std::string &entryId, const std::string &newMsg, const std::string &username);

    std::string getProperty(const std::string &propertyName) const;
    int makeSnapshot(time_t datetime);
};

struct IssueSummary {
    std::string id;
    std::string summary;
    inline bool operator< (const IssueSummary &other) const { return id < other.id; }
};

/** Copy of an issue, used for offline reading
  */
struct IssueCopy : public Issue {
    // associations table, consolidated only at users' request
    // { association-name : [IssueSummary,...] }
    std::map<AssociationId, std::set<IssueSummary> > associations; // issues referenced by this
    std::map<AssociationId, std::set<IssueSummary> > reverseAssociations; // issues that reference this

    IssueCopy(const Issue &i);
    IssueCopy() {}

    bool lessThan(const IssueCopy &other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    bool lessThan(const IssueCopy *other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    static void sort(std::vector<IssueCopy> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec);
};


class IssueComparator {
public:
    IssueComparator(const std::list<std::pair<bool, std::string> > &sSpec) : sortingSpec(sSpec) { }
    inline bool operator() (const IssueCopy* i, const IssueCopy* j) { return i->lessThan(j, sortingSpec); }
    inline bool operator() (const IssueCopy &i, const IssueCopy &j) { return i.lessThan(j, sortingSpec); }
private:
    const std::list<std::pair<bool, std::string> > &sortingSpec;
};



#endif
