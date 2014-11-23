# Enable parallel tests
TESTS = T_parseConfig T_stringTools T_smparser T_smp_encode_decode.sh functest.sh T_clone.sh

check_PROGRAMS = T_parseConfig T_stringTools smparser get_random_value
T_parseConfig_SOURCES = T_parseConfig.cpp ../src/parseConfig.cpp
T_stringTools_SOURCES = T_stringTools.cpp ../src/stringTools.cpp
get_random_value_SOURCES = get_random_value.c
smparser_SOURCES = ../src/parseConfig.cpp ../src/filesystem.cpp
smparser_CPPFLAGS = -DSM_PARSER -I$(top_srcdir)/mongoose -include logging.h

AM_CPPFLAGS = -I$(top_srcdir)/src -include logging.h


# include the tests on the distribution
EXTRA_DIST = .
# cleanup tests results before including
dist-hook:
	rm -f $(distdir)/.git* $(distdir)/*~ $(distdir)/*.out

clean-local:
	rm -f *.out
