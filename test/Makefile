
all: T_parseConfig T_stringTools T_smparser

SRCS_T_parseConfig = T_parseConfig.cpp ../src/parseConfig.cpp
SRCS_T_stringTools = T_stringTools.cpp ../src/stringTools.cpp

CPPFLAGS = -include logging.h -I../src -g

T_parseConfig: $(SRCS_T_parseConfig) utest.h
	g++ -o $@ $(CPPFLAGS) $(SRCS_T_parseConfig)
	./$@

T_stringTools: $(SRCS_T_stringTools) utest.h
	g++ -o $@ $(CPPFLAGS) $(SRCS_T_stringTools)
	./$@

.PHONY: T_smparser
T_smparser:
	sh T_smparser
	@echo OK

functest:
	sh functest.sh
