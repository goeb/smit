
My cross compilation command for Windows:

    OPENSSL=$HOME/Downloads/openssl-1.0.1e
    ../configure  --host=i686-w64-mingw32 OPENSSL_CFLAGS="-I $OPENSSL/include -DHAVE_STDINT -DNO_SSL_DL" OPENSSL_LIBS="$OPENSSL/libssl.a $OPENSSL/libcrypto.a -lws2_32 -lgdi32"

    make

