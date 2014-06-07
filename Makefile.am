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
    AM_CFLAGS += -DHAVE_STDINT -DNO_SSL_DL
    
    AM_CXXFLAGS += -static -static-libgcc
    AM_CXXFLAGS += -DHAVE_STDINT -DNO_SSL_DL
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


smit_CPPFLAGS = -I mongoose
smit_LDFLAGS = -pthread
smit_LDADD = -ldl -lcrypto

# specific subdirs
SUBDIRS = test

# data
dist_pkgdata_DATA = README.md LICENSE CHANGES

# Test sub-programs (will not be packaged)
smparser_SOURCES = src/parseConfig.cpp
smparser_CPPFLAGS = -DSM_PARSER
smparser_LDADD =

noinst_PROGRAMS = smparser

# Windows installer
if WINDOWS
all-local: @PACKAGE_TARNAME@-@VERSION@-setup$(EXEEXT)

@PACKAGE_TARNAME@-@VERSION@-setup$(EXEEXT): smit.iss smit$(EXEEXT)
	@STRIP@ smit$(EXEEXT)
	$(WINE) "@ISCC@" /q /dMyAppName=@PACKAGE_NAME@ /dMyAppVersion=@PACKAGE_VERSION@ /dMyAppExeName=smit$(EXEEXT) /f@PACKAGE_TARNAME@-@VERSION@-setup $<
endif

clean-local:
	rm -f @PACKAGE_TARNAME@-@VERSION@-setup$(EXEEXT)
	rm -f src/*.gcno src/*.gcda *.*.gcov

