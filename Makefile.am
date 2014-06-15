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
else
    AM_LDFLAGS += -pthread
endif


# Main program
bin_PROGRAMS = smit
smit_SOURCES = \
	mongoose/mongoose.c \
	src/db.cpp  \
    src/parseConfig.cpp \
    src/identifiers.cpp \
    src/main.cpp \
    src/httpdHandlers.cpp \
    src/renderingText.cpp \
    src/renderingCsv.cpp \
    src/renderingHtml.cpp \
    src/cpio.cpp \
    src/stringTools.cpp \
    src/mutexTools.cpp \
    src/session.cpp \
    src/dateTools.cpp \
    src/logging.cpp \
    src/Trigger.cpp \
    src/filesystem.cpp

nodist_smit_SOURCES = embedcpio.c embedcpio.h 
BUILT_SOURCES = embedcpio.c embedcpio.h
CLEANFILES = embedcpio.c embedcpio.h convertToC
X = embedded_data
SM_VERSION = $(X)/sm/version
embedcpio.c embedcpio.h: @srcdir@/tools/convertToC.cpp
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
	./convertToC embedcpio

.PHONY: clean-local-embedded-data
clean-local-embedded-data:
	rm -rf $(X)

smit_CPPFLAGS = -I @srcdir@/mongoose @OPENSSL_CFLAGS@
#smit_LDFLAGS = -pthread
smit_LDADD = @OPENSSL_LIBS@

if WINDOWS
else
    smit_LDADD += -ldl
endif


# specific subdirs
SUBDIRS = test

# data
dist_pkgdata_DATA = README.md LICENSE CHANGES

# Test sub-programs (will not be packaged)
smparser_SOURCES = src/parseConfig.cpp
smparser_CPPFLAGS = -DSM_PARSER
smparser_LDADD =


noinst_PROGRAMS = smparser T_parseConfig

check_PROGRAMS = T_parseConfig
T_parseConfig_SOURCES = test/T_parseConfig.cpp src/parseConfig.cpp
T_parseConfig_CPPFLAGS = -include @srcdir@/test/logging.h -I@srcdir@/src

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

