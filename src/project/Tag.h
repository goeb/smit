#ifndef _Tag_h
#define _Tag_h

#include <string>
#include <stdint.h>

struct TagSpec {
    TagSpec(): display(false) {}
    std::string id;
    std::string label; // UTF-8 text
    bool display; // status should be displayed in issue header
};


#endif
