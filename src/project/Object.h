#ifndef _Object_h
#define _Object_h


#include <string>
#include <dirent.h>

#define K_PARENT "+parent"
#define K_AUTHOR "+author"
#define K_CTIME "+ctime"

struct ObjectIteraror {
    std::string path;
    std::string subdirname;
    DIR *root;
    DIR *subdir;
    ObjectIteraror(const std::string p) : path(p), root(0), subdir(0) {}
};

class Object {

public:
    /** Return the subpath of an object in the database.
      *
      * Eg: id=01020304
      *     return: 01/020304
      */
    static std::string getSubpath(const std::string &id);
    static std::string getSubdir(const std::string &id);
    static std::string getNextObject(ObjectIteraror &objectIt);

    static int writeToId(const std::string &objectsDir, const char *data, size_t size, const std::string &id);
    static int write(const std::string &objectsDir, const char *data, size_t size, std::string &id);
    static int write(const std::string &objectsDir, const std::string &data, std::string &id);
    static int load(const std::string &objectsDir, std::string &id, std::string &data);

};

#endif
