#ifndef _Object_h
#define _Object_h


#include <string>

#define K_PARENT "+parent"
#define K_AUTHOR "+author"
#define K_CTIME "+ctime"



class Object {

public:
    /** Return the subpath of an object in the database.
      *
      * Eg: id=01020304
      *     return: 01/020304
      */
    static std::string getSubpath(const std::string &id);
    static std::string getSubdir(const std::string &id);

    static int write(const std::string &objectsDir, const std::string &data, std::string &id);

};

#endif
