# Enable parallel tests
TESTS = T_parseConfig T_stringTools T_smparser T_smp_encode_decode.sh functest.sh T_clone.sh

check_PROGRAMS = T_parseConfig T_stringTools smparser get_random_value

T_parseConfig_SOURCES = T_parseConfig.cpp ../src/parseConfig.cpp
T_parseConfig_CPPFLAGS = -include logging.h -I../src

T_stringTools_SOURCES = T_stringTools.cpp ../src/stringTools.cpp
T_stringTools_CPPFLAGS = -include logging.h -I../src

get_random_value_SOURCES = get_random_value.c

smparser_SOURCES = ../src/parseConfig.cpp
smparser_CPPFLAGS = -DSM_PARSER


# include the tests on the distribution
EXTRA_DIST = .
# cleanup tests results before including
dist-hook:
	rm -f $(distdir)/.git* $(distdir)/*~ $(distdir)/*.out

clean-local:
	rm -f *.out
