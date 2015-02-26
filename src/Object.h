#ifndef _Object_h
#define _Object_h


class Object {

public:
    /** Return the subpath of an object in the database.
      *
      * Eg: id=01020304
      *     return: 01/020304
      */
    inline static std::string getSubpath(const std::string &id) {
        if (id.size() <= 2) return id;
        else {
            std::string subpath = id.substr(0, 2) + '/' + id.substr(2);
            return subpath;
        }
    }

    inline static std::string getSubdir(const std::string &id) {
        if (id.size() <= 2) return id;
        else {
            std::string subdir = id.substr(0, 2);
            return subdir;
        }
    }

};

#endif
