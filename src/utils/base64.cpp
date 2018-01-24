#include <openssl/bio.h>
#include <openssl/evp.h>

#include "base64.h"
#include "logging.h"

std::string base64decode(const std::string &b64string)
{
    BIO *bio, *b64;
    const int BUF_SIZE = 1024;
    char buffer[BUF_SIZE];

    int resultMaxLen = (b64string.size()/4+1)*3; // +1 to protect against malformed input (not 4-aligned)
    if (resultMaxLen > BUF_SIZE) {
        LOG_ERROR("base64decode: input size too big (%s)", b64string.c_str());
        return "";
    }

    bio = BIO_new_mem_buf(b64string.c_str(), -1);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // do not use newlines to flush buffer
    int n = BIO_read(bio, buffer, b64string.size());

    BIO_free_all(bio);

    if (n <= 0) {
        LOG_ERROR("base64decode error n=%d (%s)", n, b64string.c_str());
        return "";
    }

    return std::string(buffer, n);
}
