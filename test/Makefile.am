# Enable parallel tests
TESTS = T_parseConfig \
		T_stringTools \
		T_smparser \
		T_Args.sh \
		T_smp_encode_decode.sh T_functest.sh \
		T_clone.sh T_pull.sh T_pull_2.sh T_pull_3.sh \
		T_push.sh T_push2.sh T_push3.sh \
		T_push_endurance.sh \
		T_permissions_project.sh \
		T_permissions_repo.sh \
		T_project_config.sh \
		T_user_config.sh \
		T_get_json.sh

check_PROGRAMS = T_parseConfig T_stringTools T_Args get_random_value
T_parseConfig_SOURCES = T_parseConfig.cpp ../src/utils/parseConfig.cpp ../src/utils/stringTools.cpp
T_stringTools_SOURCES = T_stringTools.cpp ../src/utils/stringTools.cpp
T_Args_SOURCES = T_Args.cpp ../src/Args.cpp ../src/utils/stringTools.cpp
get_random_value_SOURCES = get_random_value.c

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/src/utils -include logging.h


# include the tests on the distribution
EXTRA_DIST = .
# cleanup tests results before including
dist-hook:
	rm -f $(distdir)/.git* $(distdir)/*~ $(distdir)/*.out

clean-local:
	rm -f *.out
