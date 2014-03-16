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

#include <openssl/sha.h>
#include <string.h>


#include "identifiers.h"
#include "logging.h"


std::string bin2hex(const ustring & in)
{
    const char hexTable[] = { '0', '1', '2', '3',
                              '4', '5', '6', '7',
                              '8', '9', 'a', 'b',
                              'c', 'd', 'e', 'f' };
    std::string hexResult;
    size_t i;
    size_t L = in.size();
    for (i=0; i<L; i++) {
        int c = in[i];
        hexResult += hexTable[c >> 4];
        hexResult += hexTable[c & 0x0f];
    }
    return hexResult;
}

std::string getSha1(const char *data, size_t len)
{
    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1((uint8_t*)data, len, sha);
    ustring sha1sum;
    sha1sum.assign(sha, SHA_DIGEST_LENGTH);
    return bin2hex(sha1sum);
}

std::string getSha1(const std::string &data)
{
    return getSha1(data.data(), data.size());
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
