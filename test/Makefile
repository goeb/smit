

SRCS_T_parseConfig = T_parseConfig.cpp ../src/parseConfig.cpp
SRCS_T_identifiers = T_identifiers.cpp ../src/identifiers.cpp

CPPFLAGS = -I ../src

T_parseConfig: $(SRCS_T_parseConfig) utest.h
	g++ -o $@ -g $(SRCS_T_parseConfig) $(CPPFLAGS)
	./$@

T_identifiers: $(SRCS_T_identifiers) utest.h
	g++ -o $@ -g $(SRCS_T_identifiers) $(CPPFLAGS) -lcrypto
	./$@

