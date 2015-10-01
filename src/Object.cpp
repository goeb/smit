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
    if (id.size() <= 2) {
        return "xx";
    } else {
        std::string subdir = id.substr(0, 2);
        return subdir;
    }
}

std::string Object::getNextObject(ObjectIteraror &objectIt)
{
    if (!objectIt.root) {
        // open the root dir
        objectIt.root = openDir(objectIt.path);
        if (!objectIt.root) {
            LOG_ERROR("Cannot open objects dir '%s': %s", objectIt.path.c_str(), strerror(errno));
            return "";
        }
    }

    // TODO encapsulate this in Project, and check if mutex read-only necessary

    // iterate until a file found, or end of directories reached
    while (1) {
        if (!objectIt.subdir) {
            objectIt.subdirname = getNextFile(objectIt.root);
            if (objectIt.subdirname.empty()) {
                // no more subdir
                closeDir(objectIt.root);
                return "";
            }

            std::string subdirpath = objectIt.path + "/" + objectIt.subdirname;
            objectIt.subdir = openDir(subdirpath);
            if (!objectIt.subdir) {
                LOG_ERROR("Cannot open objects subdir '%s': %s", subdirpath.c_str(), strerror(errno));
                closeDir(objectIt.root);
                return "";
            }
        }

        std::string o = getNextFile(objectIt.subdir);

        if (o.size()) return objectIt.subdirname + o; // got it!

        // end of files ion subdirs, take next subdir
        closeDir(objectIt.subdir);
        objectIt.subdir = 0;
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
int Object::write(const std::string &objectsDir, const char *data, size_t size, std::string &id)
{
    id = getSha1(data, size);
    std::string path = objectsDir + "/" + getSubpath(id);

    LOG_DIAG("Write object: %s", path.c_str());

    if (fileExists(path)) {
        // check if files are the same
        int r = cmpContents(data, size, path);
        if (r != 0) {
            LOG_ERROR("SHA1 conflict on object %s", path.c_str());
            return -2;
        }
        LOG_DIAG("File already exists with same contents: %s", path.c_str());
        return 1; // file already exists with same contents
    }
    std::string subdir = objectsDir + "/" + getSubdir(id);
    mkdir(subdir);
    int r = writeToFile(path.c_str(), data, size);
    if (r != 0) {
        LOG_ERROR("Cannot write to file '%s': %s", path.c_str(), strerror(errno));
        return -1;
    }
    return 0;
}

int Object::write(const std::string &objectsDir, const std::string &data, std::string &id)
{
    return write(objectsDir, data.data(), data.size(), id);
}

int Object::load(const std::string &objectsDir, std::string &id, std::string &data)
{
    std::string path = objectsDir + "/" + getSubpath(id);
    return loadFile(path.c_str(), data);
}
