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

check_PROGRAMS = T_parseConfig \
				 T_stringTools \
				 T_Args \
				 get_random_value \
				 T_pipe \
				 T_gitdb \
				 T_subprocess

T_parseConfig_SOURCES = T_parseConfig.cpp ../src/utils/parseConfig.cpp ../src/utils/stringTools.cpp
T_stringTools_SOURCES = T_stringTools.cpp ../src/utils/stringTools.cpp
T_Args_SOURCES = T_Args.cpp ../src/Args.cpp ../src/utils/stringTools.cpp
get_random_value_SOURCES = get_random_value.c

T_pipe_SOURCES = T_pipe.cpp ../src/utils/pipe.cpp
T_gitdb_SOURCES = T_gitdb.cpp \
				  ../src/utils/pipe.cpp \
				  ../src/project/gitdb.cpp \
				  ../src/utils/gitTools.cpp \
				  ../src/utils/stringTools.cpp \
				  ../src/utils/filesystem.cpp \
				  ../src/utils/subprocess.cpp
T_subprocess_SOURCES = T_subprocess.cpp ../src/utils/subprocess.cpp

AM_CPPFLAGS = -I$(top_srcdir)/src \
			  -I$(top_srcdir)/src/utils \
			  -I$(top_srcdir)/src/project \
			  -include logging.h


# include the tests on the distribution
EXTRA_DIST = .
# cleanup tests results before including
dist-hook:
	rm -f $(distdir)/.git* $(distdir)/*~ $(distdir)/*.out

clean-local:
	rm -f *.out
