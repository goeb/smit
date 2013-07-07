
SRCS = mongoose/mongoose.c \
		src/upload.c

SRCS_CPP = src/db.cpp

OBJS = $(SRCS:%.c=obj/%.o) $(SRCS_CPP:%.cpp=obj/%.o)

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
	g++ $(CPPFLAGS) -c $< -o $@

obj/%.o: %.c
	mkdir -p `dirname $@`
	gcc $(CFLAGS) -c $< -o $@

smit: $(OBJS)
	g++ -o $@ $^ $(LDFLAGS)

clean:
	find . -name "*.o" -delete
	rm smit
