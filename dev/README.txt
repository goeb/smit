
My cross compilation command for Windows:

    OPENSSL=$HOME/Downloads/openssl-1.0.1e
    CURL=$HOME/win32libs/curl-7.37.0
    HOST=i586-mingw32msvc
    #HOST=i686-w64-mingw32
    ../configure  --host=$HOST OPENSSL_CFLAGS="-I $OPENSSL/include -DHAVE_STDINT -DNO_SSL_DL" OPENSSL_LIBS="$OPENSSL/libssl.a $OPENSSL/libcrypto.a -lws2_32 -lgdi32" CURL_CFLAGS="-I$CURL/include" CURL_LIBS="$CURL/lib/libcurl.dll.a"

    make


Curl

    ./configure --host=i586-mingw32msvc --prefix=/home/fred/win32libs/curl-7.37.0
