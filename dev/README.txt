
My cross compilation command for Windows:

WIN32_LIBS=$HOME/win32libs

    OPENSSL=$HOME/Downloads/openssl-1.0.1e
    CURL=$WIN32_LIBS/curl-7.37.0
    HOST=i586-mingw32msvc
    #HOST=i686-w64-mingw32
    ../configure  --host=$HOST OPENSSL_CFLAGS="-I $OPENSSL/include -DHAVE_STDINT -DNO_SSL_DL" OPENSSL_LIBS="$OPENSSL/libssl.a $OPENSSL/libcrypto.a" CURL_CFLAGS="-I$CURL/include -DCURL_STATICLIB" CURL_LIBS="$CURL/lib/libcurl.a"

    make


Curl

    ./configure --host=i586-mingw32msvc --prefix=$WIN32_LIBS/curl-7.37.0 --disable-shared --disable-ldap



Open SSL

    TODO

     ./Configure --prefix=$HOME/win32libs/openssl-1.0.1h no-shared  mingw no-asm

