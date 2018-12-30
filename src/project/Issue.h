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
    std::vector<Entry> entries;
    std::string project; // name of the project
    int ctime; // creation time (the one of the first entry)

    // mutable members, that may be modified when a user posts an entry
    int mtime; // modification time (the one of the last entry)
    PropertiesMap properties;
    std::map<uint32_t, std::list<uint32_t> > amendments; // key: amended entry-id, value: amending entries
    std::map<uint32_t, std::set<std::string> > tags; // key: entry-index, value: tags

    Issue() : ctime(0), mtime(0) {}

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    std::string getSummary() const;
    bool isInFilter(const std::map<std::string, std::list<std::string> > &filter, FilterMode mode) const;

    void consolidate();
    void consolidateWithSingleEntry(const Entry &e);
    void consolidateAmendment(const Entry &amending, uint32_t idxAmending);
    bool searchFullText(const char *text) const;
    int getNumberOfTaggedIEntries(const std::string &tagname) const;
    void addTag(uint32_t &entryIndex, const std::string &tagname);
    void setTags(uint32_t entryIndex, std::set<std::string> &tagsOfEntry);
    std::set<std::string> getTags(uint32_t entryIndex);

    bool hasTag(uint32_t entryIndex, const std::string &tagname) const;

    uint32_t addEntryInTable(Entry *e);
    uint32_t addEntry(Entry *e);
    void insertEntry(Entry *e);
    void amendEntry(Entry *amendingEntry);

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
