
#include <sstream>
#include <stdlib.h>

#include "Tag.h"
#include "parseConfig.h"
#include "Object.h"
#include "filesystem.h"
#include "logging.h"
#include "global.h"

#define K_TAG "+tag"

std::string Tag::serialize() const
{
    std::stringstream s;
    s << K_PARENT " " << serializeSimpleToken(parent) << "\n";
    s << K_CTIME " " << ctime << "\n";
    s << K_AUTHOR " " << author << "\n";
    s << K_TAG " " << tagName << " " <<  serializeSimpleToken(entryId) << "\n";
    return s.str();
}

Tag *Tag::load(const std::string &path)
{
    std::string data;
    int r = loadFile(path, data);
    if (r != 0) {
        LOG_ERROR("Cannot load tag '%s': %s", path.c_str(), STRERROR(errno));
        return 0;
    }
    std::list<std::list<std::string> > tokens = parseConfigTokens(data.c_str(), data.size());
    std::list<std::list<std::string> >::iterator line;
    Tag *tag = new Tag;
    FOREACH(line, tokens) {
        std::string token = popListToken(*line);
        if (token == K_PARENT) tag->parent = popListToken(*line);
        else if (token == K_CTIME) {
            std::string ctimeStr = popListToken(*line);
            tag->ctime = strtoul(ctimeStr.c_str(), NULL, 0);
        } else if (token == K_AUTHOR) tag->author = popListToken(*line);
        else if (token == K_TAG) {
            tag->tagName = popListToken(*line);
            tag->entryId = popListToken(*line);
        } else {
            LOG_ERROR("Tag::load: invalid token '%s'", token.c_str());
        }
    }
    return tag;
}
