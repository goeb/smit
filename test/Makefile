

SRCS_T_parseConfig = T_parseConfig.cpp ../src/parseConfig.cpp

CPPFLAGS = -I ../src

T_parseConfig: $(SRCS_T_parseConfig) utest.h
	g++ -o $@ -g $(SRCS_T_parseConfig) $(CPPFLAGS)
	./$@
