
SRCS = mongoose/mongoose.c \


SRCS_CPP = src/db.cpp  \
		   src/parseConfig.cpp \
		   src/identifiers.cpp \
		   src/main.cpp \
		   src/httpdHandlers.cpp \
		   src/renderingText.cpp \
		   src/renderingHtml.cpp \
		   src/cpio.cpp \
		   src/stringTools.cpp

CC = gcc
CPP = g++

OBJS = $(SRCS:%.c=obj/%.o) $(SRCS_CPP:%.cpp=obj/%.o)
DEPENDS = $(SRCS:%.c=obj/%.d) $(SRCS_CPP:%.cpp=obj/%.d)

CFLAGS = -g
CFLAGS += -I mongoose
CPPFLAGS = -g -I mongoose

LDFLAGS = -ldl -pthread -lcrypto


all: smit

print:
	@echo SRCS=$(SRCS)
	@echo OBJS=$(OBJS)

obj/%.o: %.cpp
	mkdir -p `dirname $@`
	$(CPP) $(CPPFLAGS) -c $< -o $@

obj/%.o: %.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

obj/%.d: %.c
	mkdir -p `dirname $@`
	@set -e; rm -f $@; \
	$(CPP) -M $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

obj/%.d: %.cpp
	mkdir -p `dirname $@`
	@set -e; rm -f $@; \
	$(CC) -M $(CFLAGS) $< > $@.$$$$; \
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
