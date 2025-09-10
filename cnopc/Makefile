rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

CC = cc
LD = cc
CFLAGS = -std=c99 -Werror -Wextra -Wall -Wimplicit -Wstrict-aliasing

SRCS = $(call rwildcard,src,*.c)
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

.PHONY: all clean

all: bin/nopc

bin/nopc: $(DEPS) $(OBJS)
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

%.o %.d: %.c
	$(CC) $(CFLAGS) -MD -MF $(addsuffix .d,$(basename $<)) -c $< -o $(addsuffix .o,$(basename $<))

clean:
	rm -f bin/*
	rm -f $(call rwildcard,src,*.o)
	rm -f $(call rwildcard,src,*.d)

ifneq ($(MAKECMDGOALS),clean)
include $(DEPS)
endif
