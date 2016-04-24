#ifndef _Tag_h
#define _Tag_h

#include <string>
#include <stdint.h>

class Tag {
public:
    std::string id;
    std::string parent;
    std::string author;
    time_t ctime;
    std::string entryId;
    std::string tagName;
    std::string serialize() const;
    static Tag *load(const std::string &path, const std::string &id);
};

struct TagSpec {
    TagSpec(): display(false) {}
    std::string id;
    std::string label; // UTF-8 text
    bool display; // status should be displayed in issue header
};


#endif
