

#include "parseConfig.h"
#include "ustring.h"


std::list<std::list<ustring> > parseConfig(const uint8_t *buf, size_t len)
{
    std::list<std::list<ustring> > linesOftokens;
    size_t i = 0;
    enum State {
        P_READY,
        P_IN_DOUBLE_QUOTES,
        P_IN_BACKSLASH,
        P_IN_COMMENT,
        P_IN_BACKSLASH_IN_DOUBLE_QUOTES,
        P_IN_BOUNDARY_HEADER,
        P_IN_BOUNDARY
    };
    enum State state = P_READY;
    ustring token; // current token
    std::list<ustring> line; // current line
    ustring boundary;
    ustring boundedText;
    for (i=0; i<len; i++) {
        uint8_t c = buf[i];
        switch (state) {
        case P_IN_COMMENT:
            if (c == '\n') {
                if (line.size() > 0) { linesOftokens.push_back(line); line.clear(); }
                state = P_READY;
            }
            break;
        case P_IN_BACKSLASH:
            if (c == '\n') { // new line escaped
                // nothing particular here
            } else {
                token += c;
            }
            state = P_READY;
            break;

        case P_IN_DOUBLE_QUOTES:
            if (c == '\\') {
                state = P_IN_BACKSLASH_IN_DOUBLE_QUOTES;
            } else if (c == '"') {
                state = P_READY; // end of double-quoted string
            } else token += c;
            break;
        case P_IN_BACKSLASH_IN_DOUBLE_QUOTES:
            token += c;
            state = P_IN_DOUBLE_QUOTES;
            break;
        case P_IN_BOUNDARY_HEADER:
            if (c == '\n') {
                state = P_IN_BOUNDARY;
                boundary.insert(0, (uint8_t*)"\n"); // add a \n at the beginning
                boundedText.clear();
            } else if (isblank(c)) continue; // ignore blanks
            else {
                boundary += c;
            }
            break;
        case P_IN_BOUNDARY:
            // check if boundary was reached
            if (boundedText.size() >= boundary.size()) { // leading \n already added
                size_t bsize = boundary.size();
                size_t offset = boundedText.size() - bsize;
                if (0 == boundedText.compare(offset, bsize, boundary)) {
                    // boundary found
                    // substract the boundary fro the text
                    boundedText = boundedText.substr(0, offset);
                    state = P_READY;
                    line.push_back(boundedText);
                    boundedText.clear();
                    linesOftokens.push_back(line);
                    line.clear();
                }
            }
            // accumulate character if still in same state
            if (state == P_IN_BOUNDARY) boundedText += c;

            break;
        case P_READY:
        default:
            if (c == '\n') {
                if (token.size() > 0) { line.push_back(token); token.clear(); }
                if (line.size() > 0) { linesOftokens.push_back(line); line.clear(); }
            } else if (c == ' ' || c == '\t' || c == '\r') {
                // current toke is done (because c is a token delimiter)
                if (token.size() > 0) { line.push_back(token); token.clear(); }
            } else if (c == '#') {
                if (token.size() > 0) { line.push_back(token); token.clear(); }
                state = P_IN_COMMENT;
            } else if (c == '\\') {
                state = P_IN_BACKSLASH;
            } else if (c == '"') {
                state = P_IN_DOUBLE_QUOTES;
            } else if (c == '<') {
                state = P_IN_BOUNDARY_HEADER;
                boundary.clear();
            } else {
                token += c;
            }
            break;
        }
    }
    // purge remaininig token and line
    if (token.size() > 0) line.push_back(token);
    if (line.size() > 0) linesOftokens.push_back(line);

    return linesOftokens;
}
