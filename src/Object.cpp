#include "config.h"

#include "Object.h"
#include "identifiers.h"
#include "filesystem.h"
#include "stringTools.h"
#include "logging.h"

std::string Object::getSubpath(const std::string &id) {
    if (id.size() <= 2) {
        std::string result = "xx/" + id;
        return result;
    } else {
        std::string subpath = id.substr(0, 2) + '/' + id.substr(2);
        return subpath;
    }
}

std::string Object::getSubdir(const std::string &id) {
    if (id.size() <= 2) return "xx";
    else {
        std::string subdir = id.substr(0, 2);
        return subdir;
    }
}

/** Write an object into a database
  *
  * @param[out] id
  *    identifier of the stored object
  *
  * @return
  *    0 ok, new object created
  *    1 ok, file already exists
  *   -1 error,
  *   -2 error, sha1 conflict
  */
int Object::write(const std::string &objectsDir, const std::string &data, std::string &id)
{
    id = getSha1(data);
    std::string path = objectsDir + "/" + getSubpath(id);

    if (fileExists(path)) {
        // check if files are the same
        int r = cmpContents(data, path);
        if (r != 0) {
            LOG_ERROR("SHA1 conflict on object %s", path.c_str());
            return -2;
        }
        LOG_DIAG("File already exists with same contents: %s", path.c_str());
        return 1; // file already exists with same contents
    }
    mkdirs(getDirname(path));
    int r = writeToFile(path, data);
    if (r != 0) {
        LOG_ERROR("Cannot write to file '%s': %s", path.c_str(), strerror(errno));
        return -1;
    }
    return 0;
}


