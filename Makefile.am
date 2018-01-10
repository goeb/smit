# Manage configure options
AM_CPPFLAGS=-Wall
AM_CFLAGS=-Wall
AM_CXXFLAGS=-Wall
AM_LDFLAGS=

if GCOV_ENABLED
AM_CFLAGS += -O0 -g3 -fprofile-arcs -ftest-coverage
AM_CXXFLAGS += -O0 -g3 -fprofile-arcs -ftest-coverage
AM_LDFLAGS += -lgcov
endif

if GPROF_ENABLED
AM_CFLAGS += -O0 -g3 -pg -fprofile-arcs
AM_CXXFLAGS += -O0 -g3 -pg -fprofile-arcs
AM_LDFLAGS += -pg -lgcov
endif

if WINDOWS
AM_CFLAGS += -static -static-libgcc
AM_CFLAGS += -Dopenssl_EXPORTS

AM_CXXFLAGS += -static -static-libgcc
AM_CXXFLAGS += -Dopenssl_EXPORTS
endif

AM_LDFLAGS += -pthread


# Main program
bin_PROGRAMS = smit smparser
smit_SOURCES = \
			   src/repository/db.cpp  \
			   src/project/Entry.cpp  \
			   src/project/Issue.cpp  \
			   src/project/Project.cpp  \
			   src/project/View.cpp  \
			   src/project/Tag.cpp  \
			   src/project/ProjectConfig.cpp \
			   src/project/Object.cpp \
			   src/project/gitdb.cpp \
			   src/utils/parseConfig.cpp \
			   src/utils/identifiers.cpp \
			   src/utils/cpio.cpp \
			   src/utils/stringTools.cpp \
			   src/utils/jTools.cpp \
			   src/utils/mutexTools.cpp \
			   src/utils/dateTools.cpp \
			   src/utils/logging.cpp \
			   src/utils/filesystem.cpp \
			   src/utils/pipe.cpp \
			   src/utils/subprocess.cpp \
			   src/utils/gitTools.cpp \
			   src/main.cpp \
			   src/server/httpdHandlers.cpp \
			   src/server/httpdUtils.cpp \
			   src/server/httpdHandlerGit.cpp \
			   src/server/Trigger.cpp \
			   src/server/HttpContext.cpp \
			   src/rendering/renderingText.cpp \
			   src/rendering/renderingJson.cpp \
			   src/rendering/renderingCsv.cpp \
			   src/rendering/renderingHtml.cpp \
			   src/rendering/ContextParameters.cpp \
			   src/rendering/renderingHtmlIssue.cpp \
			   src/rendering/renderingHtmlUtil.cpp \
			   src/rendering/renderingZip.cpp \
			   src/user/session.cpp \
			   src/user/AuthSha1.cpp \
			   src/user/notification.cpp \
			   src/local/console.cpp \
			   src/local/localClient.cpp \
			   src/Args.cpp \
			   src/third-party/mongoose.c \
			   src/third-party/fnmatch.c

if CURL_ENABLED
smit_SOURCES += \
			   src/local/clone.cpp \
			   src/local/httpClient.cpp
endif

if KERBEROS_ENABLED
smit_SOURCES += src/user/AuthKrb5.cpp
endif

if LDAP_ENABLED
smit_SOURCES += src/user/AuthLdap.cpp
endif


nodist_smit_SOURCES = embedcpio.c embedcpio.h 
BUILT_SOURCES = embedcpio.c embedcpio.h
CLEANFILES = embedcpio.c embedcpio.h convertToC
X = embedded_data
SM_VERSION = $(X)/sm/version
embedcpio.c embedcpio.h: @srcdir@/tools/convertToC.cpp @srcdir@/data/sm/smit.js
	rm -rf $(X)
	cp -r @srcdir@/data $(X)
	@# generate sm/version
	mkdir -p `dirname $(SM_VERSION)`
	echo "Version: @VERSION@" > $(SM_VERSION)
	echo "Build: "`date "+%Y-%m-%d %H:%M:%S"` >> $(SM_VERSION)
	which git && echo -n "Latest " >> $(SM_VERSION) && git log -1 | head -1 >> $(SM_VERSION)
	@#
	cd $(X) && find * | cpio -o > ../embedcpio
	@# build convertToC with gcc
	@# I do not know how to make this portable with the GNU autotools
	g++ -o ./convertToC @srcdir@/tools/convertToC.cpp
	./convertToC embedcpio `date +%s`

.PHONY: clean-local-embedded-data
clean-local-embedded-data:
	rm -rf $(X)

smit_CPPFLAGS = -I @srcdir@/src -I @srcdir@/src/third-party \
					@OPENSSL_CFLAGS@ \
					@CURL_CFLAGS@ \
					@LIBARCHIVE_CFLAGS@

smit_LDADD = @OPENSSL_LIBS@ \
			 @CURL_LIBS@ \
			 @LIBARCHIVE_LIBS@

if CURL_ENABLED
smit_LDADD += @CURL_LIBS@
smit_CPPFLAGS += @CURL_CFLAGS@ -DCURL_ENABLED
endif

if LDAP_ENABLED
smit_LDADD += @LDAP_LIBS@
smit_CPPFLAGS += @LDAP_CFLAGS@ -DLDAP_ENABLED
endif

if KERBEROS_ENABLED
smit_LDADD += @KRB5_LIBS@
smit_CPPFLAGS += @KRB5_CFLAGS@ -DKERBEROS_ENABLED
endif

if WINDOWS
smit_LDADD += -lws2_32 -lgdi32
else
smit_LDADD += -ldl
endif

smparser_SOURCES = \
				   src/utils/parseConfig.cpp \
				   src/utils/filesystem.cpp \
				   src/utils/stringTools.cpp

smparser_CPPFLAGS = -DSM_PARSER -DSM_NO_LOGGING -I @srcdir@/src



# specific subdirs
SUBDIRS = test

# data
dist_pkgdata_DATA = README.md LICENSE CHANGES

# Windows installer
if WINDOWS
all-local: @PACKAGE_TARNAME@-@VERSION@-setup$(EXEEXT)

@PACKAGE_TARNAME@-@VERSION@-setup$(EXEEXT): smit.iss smit$(EXEEXT)
	@STRIP@ smit$(EXEEXT)
	$(WINE) "@ISCC@" /q /dMyAppName=@PACKAGE_NAME@ /dMyAppVersion=@PACKAGE_VERSION@ /dMyAppExeName=smit$(EXEEXT) /f@PACKAGE_TARNAME@-@VERSION@-setup $<
endif

clean-local: clean-local-embedded-data
	rm -f @PACKAGE_TARNAME@-@VERSION@-setup$(EXEEXT)
	rm -f src/*.gcno src/*.gcda *.*.gcov

