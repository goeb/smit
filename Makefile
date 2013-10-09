
ifeq ($(WIN),1)
	CC = i586-mingw32msvc-gcc
	CXX = i586-mingw32msvc-g++
	BUILD_DIR = build_win
else
	CC = gcc
	CXX = g++
	BUILD_DIR = build_linux86
endif

SRCS = mongoose/mongoose.c \


SRCS_CPP = src/db.cpp  \
		   src/parseConfig.cpp \
		   src/identifiers.cpp \
		   src/main.cpp \
		   src/httpdHandlers.cpp \
		   src/renderingText.cpp \
		   src/renderingHtml.cpp \
		   src/cpio.cpp \
		   src/stringTools.cpp \
		   src/mutexTools.cpp \
		   src/session.cpp \
		   src/dateTools.cpp


OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o) $(SRCS_CPP:%.cpp=$(BUILD_DIR)/%.o)
DEPENDS = $(SRCS:%.c=$(BUILD_DIR)/%.d) $(SRCS_CPP:%.cpp=$(BUILD_DIR)/%.d)

CFLAGS = -g
CFLAGS += -I mongoose

LDFLAGS = -ldl -pthread -lcrypto


all: smit

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
	$(CC) -M $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

$(BUILD_DIR)/%.d: %.cpp
	mkdir -p `dirname $@`
	@set -e; rm -f $@; \
	$(CXX) -M $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

smit: $(OBJS)
	g++ -o $@ $^ $(LDFLAGS)

embedcpio:
	find repositories | cpio -o > repositories.cpio
	cat repositories.cpio >> smit
	size=`stat -c %s repositories.cpio`; \
	python -c "import struct; import sys; sys.stdout.write(struct.pack('I', $$size))" >> smit


clean:
	find . -name "*.o" -delete
	rm smit

.PHONY: test
test:
	$(MAKE) -C test

include $(DEPENDS)
