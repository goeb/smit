
all: T_parseConfig T_identifiers

SRCS_T_parseConfig = T_parseConfig.cpp ../src/parseConfig.cpp
SRCS_T_identifiers = T_identifiers.cpp ../src/identifiers.cpp

CPPFLAGS = -include logging.h -I../src -g

T_parseConfig: $(SRCS_T_parseConfig) utest.h
	g++ -o $@ $(CPPFLAGS) $(SRCS_T_parseConfig)
	./$@

T_identifiers: $(SRCS_T_identifiers) utest.h
	g++ -o $@ $(CPPFLAGS) $(SRCS_T_identifiers) -lcrypto
	./$@

