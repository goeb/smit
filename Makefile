
SRCS = mongoose/mongoose.c \
		src/smit_httpd.c

SRCS_CPP = src/db.cpp  \
		   src/parseConfig.cpp \
		   src/identifiers.cpp



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
	@set -e; rm -f $@; \
	$(CPP) -M $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

obj/%.d: %.cpp
	@set -e; rm -f $@; \
	$(CC) -M $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

smit: $(OBJS)
	g++ -o $@ $^ $(LDFLAGS)

clean:
	find . -name "*.o" -delete
	rm smit

include $(DEPENDS)
