
My cross compilation command for Windows:

WIN32_LIBS=$HOME/win32libs

OPENSSL=$WIN32_LIBS/openssl-1.1.0c
CURL=$WIN32_LIBS/curl-7.37.0
HOST=i686-w64-mingw32

../configure --host=$HOST OPENSSL_CFLAGS="-I $OPENSSL/include -DHAVE_STDINT -DNO_SSL_DL" OPENSSL_LIBS="$OPENSSL/lib/libssl.a $OPENSSL/lib/libcrypto.a" CURL_CFLAGS="-I$CURL/include -DCURL_STATICLIB" CURL_LIBS="$CURL/lib/libcurl.a" CC=${HOST}-gcc CXX=${HOST}-g++ LIBARCHIVE_CFLAGS="-I$WIN32_LIBS/libarchive-3.2.1/include" LIBARCHIVE_LIBS="$WIN32_LIBS/libarchive-3.2.1/lib/libarchive.a"

    make


Curl

    ./configure --host=i586-mingw32msvc --prefix=$WIN32_LIBS/curl-7.37.0 --disable-shared --disable-ldap



Open SSL

    CROSS_COMPILE="i686-w64-mingw32-" ./Configure mingw --prefix=$HOME/win32libs/openssl-1.1.0c no-shared no-asm -DOPENSSL_NO_CAPIENG
    make
    make install


libarchive

	../configure --host=$HOST --prefix $WIN32_LIBS/libarchive-3.2.1 --without-iconv
	make
	make install

(other/old?) smit :
../configure CXXFLAGS="-g -O0" KRB5_LIBS="-L/usr/lib/i386-linux-gnu/mit-krb5 -lkrb5" KRB5_CFLAGS="-I/usr/include" --enable-kerberos --enable-ldap LDAP_CFLAGS="-I/usr/include" LDAP_LIBS="-lldap"

