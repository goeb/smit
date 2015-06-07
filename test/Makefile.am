# Enable parallel tests
TESTS = T_parseConfig \
		T_stringTools \
		T_smparser \
		T_smp_encode_decode.sh T_functest.sh \
		T_clone.sh T_pull.sh T_pull_2.sh \
		T_push.sh T_push2.sh T_push3.sh \
		T_push_endurance.sh T_permissions.sh \
		T_project_config.sh

check_PROGRAMS = T_parseConfig T_stringTools smparser get_random_value
T_parseConfig_SOURCES = T_parseConfig.cpp ../src/parseConfig.cpp ../src/stringTools.cpp
T_stringTools_SOURCES = T_stringTools.cpp ../src/stringTools.cpp
get_random_value_SOURCES = get_random_value.c
smparser_SOURCES = ../src/parseConfig.cpp ../src/filesystem.cpp ../src/stringTools.cpp
smparser_CPPFLAGS = -DSM_PARSER -I$(top_srcdir)/mongoose -include logging.h

AM_CPPFLAGS = -I$(top_srcdir)/src -include logging.h


# include the tests on the distribution
EXTRA_DIST = .
# cleanup tests results before including
dist-hook:
	rm -f $(distdir)/.git* $(distdir)/*~ $(distdir)/*.out

clean-local:
	rm -f *.out
