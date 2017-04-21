#ifndef _Entry_h
#define _Entry_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "utils/ustring.h"
#include "utils/stringTools.h"
#include "Object.h"

#define K_MESSAGE     "+message" // keyword used for the message
#define K_FILE        "+file" // keyword used for uploaded files
#define K_SUMMARY     "summary"
#define K_AMEND       "+amend"
#define K_PARENT_NULL "null"
#define K_TAG         "+tag"

#define DELETE_DELAY_S (10*60) // seconds

// Define atomic builtins for gcc < 4.7
#ifdef __GNUC__

  #define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

  #if GCC_VERSION < 40700
    #define HAVE_ATOMIC 0
  #else
    #define HAVE_ATOMIC 1
  #endif // GCC_VERSION < 40700

#endif // __GNUC__

inline const void * atomicGet(const void *ptr) {
	const void *pout;
#if HAVE_ATOMIC
	pout = __atomic_load_n(&ptr, __ATOMIC_ACQUIRE);
#else
	__sync_synchronize();
	pout = ptr;
	__sync_synchronize();
#endif
	return pout;
}

inline void atomicSet(const void **ptr, const void *val) {
#if HAVE_ATOMIC
	__atomic_store_n(ptr, val, __ATOMIC_RELEASE);
#else
	__sync_synchronize();
	*ptr = val;
	__sync_synchronize();
#endif
}

class Issue;

// Entry
class Entry : public Object {
public:
    std::string parent; // id of the parent entry, empty if top-level

    /** The id of an entry
      * - must not start by a dot (reserved for hidden file)
      * - must not contain characters forbidden for file names (Linux and Windows):
      *        <>:"/\|?*
      * - must be unique case insensitively (as HTML identifiers are case insensitive)
      *
      * The ids of entries in the current implementation contain: lower case letters and digits
      */
    std::string id; // unique id of this entry
    long ctime; // creation time
    std::string author;
    PropertiesMap properties;
    Issue *issue;


    // methods
    Entry() : ctime(0), issue(0), next(0), prev(0), message(&EMPTY_MESSAGE) {}
    static Entry *loadEntry(const std::string &path, const std::string &id);
    void setId();
    void updateMessage();
    std::string serialize() const;
    int getCtime() const;

    inline const std::string &getMessage() const { return *((const std::string*)atomicGet(message)); }
    inline void setMessage(const std::string *msg) { atomicSet((const void**)&message, msg); }

    inline Entry *getNext() const { return (Entry *)atomicGet(next); }
    inline void setNext(Entry *nextEntry) { atomicSet((const void**)&next, nextEntry); }

    inline Entry *getPrev() const { return (Entry *)atomicGet(prev); }
    inline void setPrev(Entry *prevEntry) { atomicSet((const void**)&prev, prevEntry); }


    bool isAmending() const;
    inline std::string getSubpath() const { return Object::getSubpath(id); }
    static inline std::string getSubpath(const std::string identifier) { return Object::getSubpath(identifier); }
    static Entry *createNewEntry(const PropertiesMap &props, const std::string &author, const Entry *eParent);

    // methods managing the linked list
    void append(Entry *e);

    static void sort(std::vector<Entry> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec);
    bool lessThan(const Entry &other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    bool lessThan(const Entry *other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;

private:
    // chainlist pointers
    struct Entry *next; // child
    struct Entry *prev; // parent

    /** The member "message" points to :
      * - either null if no message
      * - or this->properties[K_MESSAGE]
      * - or to the message of the latest amending entry
      */
    const std::string *message;

    static const std::string EMPTY_MESSAGE;

};

class EntryComparator {
public:
    EntryComparator(const std::list<std::pair<bool, std::string> > &sSpec) : sortingSpec(sSpec) { }
    inline bool operator() (const Entry* i, const Entry* j) { return i->lessThan(j, sortingSpec); }
    inline bool operator() (const Entry i, const Entry j) { return i.lessThan(j, sortingSpec); }
private:
    const std::list<std::pair<bool, std::string> > &sortingSpec;
};


#endif
