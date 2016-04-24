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
