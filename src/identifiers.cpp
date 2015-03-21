/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include <openssl/sha.h>
#include <string.h>
#include <fstream>

#include "identifiers.h"
#include "logging.h"
#include "stringTools.h"

std::string getSha1(const char *data, size_t len)
{
    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1((uint8_t*)data, len, sha);
    return bin2hex(sha, SHA_DIGEST_LENGTH);
}

std::string getSha1(const std::string &data)
{
    return getSha1(data.data(), data.size());
}

/** Read a file, and return its sha1 in ascii-hexadecimal
  *
  */
std::string getSha1OfFile(const std::string &path)
{
    std::ifstream f(path.c_str(), std::ios_base::binary);
    if (!f) return "";
    const size_t BUZ_SIZE = 4096;
    char buffer[BUZ_SIZE];
    SHA_CTX sha1Ctx;
    SHA1_Init(&sha1Ctx);

    do {
        f.read(buffer, BUZ_SIZE);
        SHA1_Update(&sha1Ctx, buffer, f.gcount());
        if (f.eof()) break;
        if (!f.good()) return "";
    } while (1);

    unsigned char md[20];
    SHA1_Final(md, &sha1Ctx);
    std::string sha1 = bin2hex(ustring(md, sizeof(md)));
    return sha1;
}

/** not really base64, as _- are used instead of +/
  * and the trailing = are not added.
  *
  * The purpose of using "-_" instead of "+/" is
  * to obtain identifiers suitable for file names.
  */
std::string base64(const ustring &src)
{

    static const char *b64 =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

    size_t i;
    int a, b, c;
    size_t srcLen = src.size();
    std::string dst;
    for (i = 0; i < srcLen; i += 3) {
        a = src[i];
        b = i + 1 >= srcLen ? 0 : src[i + 1];
        c = i + 2 >= srcLen ? 0 : src[i + 2];

        dst += b64[a >> 2];
        dst += b64[((a & 3) << 4) | (b >> 4)];
        if (i + 1 < srcLen) {
            dst += b64[(b & 15) << 2 | (c >> 6)];
        }
        if (i + 2 < srcLen) {
            dst += b64[c & 63];
        }
    }
    //LOG_DEBUG("base64(%s)=%s", bin2hex(src).c_str(), dst.c_str());
    return dst;
}


std::string getBase64Id(const uint8_t *data, size_t len)
{
    unsigned char sha[SHA_DIGEST_LENGTH+1]; // + 1 for aligning on 3 bytes
    SHA1(data, len, sha);
    sha[SHA_DIGEST_LENGTH] = 0;

    ustring sha1sum(sha, SHA_DIGEST_LENGTH+1);
    std::string id = base64(sha1sum);
    // remove last character (always 'A' because 6 last bits of the sha1 are always 0)
    // 20+1 bytes binary => 28 bytes base64, the last of which is always 'A'

    return id.substr(0, 27);
}
