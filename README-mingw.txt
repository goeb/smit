

../configure --host=i586-mingw32msvc OPENSSL_CFLAGS="-I $HOME/Downloads/openssl-1.0.1e/include -DHAVE_STDINT -DNO_SSL_DL" OPENSSL_LIBS="$HOME/Downloads/openssl-1.0.1e/libssl.a $HOME/Downloads/openssl-1.0.1e/libcrypto.a -lws2_32 -lgdi32"
