
ifeq ($(WIN),1)
	SM_PARSER = bin/smparser.exe
	EXE = smit.exe
	OPENSSL = $(HOME)/Downloads/openssl-1.0.1e
	CC = i586-mingw32msvc-gcc
	CXX = i586-mingw32msvc-g++
	BUILD_DIR = build_win
	CFLAGS += -I $(OPENSSL)/include -DHAVE_STDINT -DNO_SSL_DL
	LDFLAGS += $(OPENSSL)/libssl.a $(OPENSSL)/libcrypto.a -lws2_32 -lgdi32
	PACK_NAME = smit-win32
else
	SM_PARSER = bin/smparser
	EXE = smit
	CC = gcc
	CXX = g++
	BUILD_DIR = build_linux86
	LDFLAGS = -ldl -pthread -lcrypto
	PACK_NAME = smit-linux86
endif

SRCS = mongoose/mongoose.c \


SRCS_CPP = src/db.cpp  \
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


OBJS = $(SRCS_CPP:%.cpp=$(BUILD_DIR)/%.o) $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPENDS = $(SRCS:%.c=$(BUILD_DIR)/%.d) $(SRCS_CPP:%.cpp=$(BUILD_DIR)/%.d)

CFLAGS += -g -Wall
#CFLAGS = -O2 -Wall
CFLAGS += -I mongoose

ifeq ($(GCOV),1)
	CFLAGS += -fprofile-arcs -ftest-coverage
	LDFLAGS += -lgcov
endif

ifeq ($(GPROF),1)
	CFLAGS += -pg -fprofile-arcs
	LDFLAGS += -pg -lgcov
endif



all: $(SM_PARSER) embedcpio

.PHONY: $(SM_PARSER)
$(SM_PARSER):
	$(CXX) -g -o $@ src/parseConfig.cpp -DSM_PARSER

print:
	@echo SRCS=$(SRCS)
	@echo OBJS=$(OBJS)
	@echo WIN=$(WIN)

$(BUILD_DIR)/%.o: %.cpp
	mkdir -p `dirname $@`
	$(CXX) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.d: %.c
	mkdir -p `dirname $@`
	@set -e; rm -f $@; \
	obj=`echo $@ | sed -e "s/\.d$$/.o/"`; \
	$(CXX) -MM $(CFLAGS) $< > $@.$$$$; \
	sed "s,.*:,$$obj $@ : ,g" < $@.$$$$ > $@; \
	rm -f $@.$$$$

$(BUILD_DIR)/%.d: %.cpp
	mkdir -p `dirname $@`
	@set -e; rm -f $@; \
	obj=`echo $@ | sed -e "s/\.d$$/.o/"`; \
	$(CXX) -MM $(CFLAGS) $< > $@.$$$$; \
	sed "s,.*:,$$obj $@ : ,g" < $@.$$$$ > $@; \
	rm -f $@.$$$$

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

.PHONY: data/sm/version
data/sm/version:
	echo -n "Version: v" > $@
	set -e; V=`grep "#define VERSION" src/* |sed -e "s/.*VERSION *//" -e 's/"//g'`; \
		echo $$V >> $@
	echo -n "Build: " >> $@
	date "+%Y-%m-%d %H:%M:%S" >> $@
	which git && echo -n "Latest " >> $@ && git log -1 | head -1 >> $@



CPIO_ARCHIVE = embedded.cpio
cpio: embedcpio
embedcpio: $(EXE) data/*/* data/sm/version
	cd data && find * | cpio -o > ../$(CPIO_ARCHIVE)
	cat $(CPIO_ARCHIVE) >> $(EXE)
	size=`stat -c %s $(CPIO_ARCHIVE)`; \
	python -c "import struct; import sys; sys.stdout.write(struct.pack('I', $$size))" >> $(EXE)


clean:
	find . -name "*.o" -delete
	find . -name "*.d" -delete
	rm $(EXE)

.PHONY: test
test:
	$(MAKE) -C test

.PHONY: release
release: $(EXE)
	i586-mingw32msvc-strip $(EXE)
	$(MAKE) embedcpio
	set -e; V=`grep "#define VERSION" src/* |sed -e "s/.*VERSION *//" -e 's/"//g'`; \
		rm -rf "smit-win32-$$V"; \
		mkdir "smit-win32-$$V"; \
		cp $(EXE) bin/*bat smit-win32-$$V/. ; \
		zip -r smit-win32-$$V.zip smit-win32-$$V

selfSignedCertificate:
	openssl genrsa > privkey.pem
	openssl req -new -x509 -key privkey.pem -out cacert.pub.pem -days 1095
	cat privkey.pem cacert.pub.pem > cacert.pem
	@echo "cacert.pem ready."


include $(DEPENDS)
