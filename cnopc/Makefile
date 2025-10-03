rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

CC = cc
LD = cc
CFLAGS = -std=c99 -Werror -Wextra -Wall -Wimplicit -Wstrict-aliasing \
		 -Iinclude
CFLAGS += -g

SRCS = $(call rwildcard,src,*.c)
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

TSTSRCS = $(call rwildcard,tst,*.c)
TSTEXES = $(TSTSRCS:.c=.tst)
TSTDEPS = $(TSTSRCS:.c=.d)

.PHONY: all test clean

all: bin/nopc test

bin/nopc: $(DEPS) $(OBJS)
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

test: $(TSTEXES)
	$(foreach exe,$(TSTEXES),$(exe))

%.tst: %.o %.d
	$(LD) $< -o $@ $(LDFLAGS)

%.o %.d: %.c
	$(CC) $(CFLAGS) -MD -MF $(addsuffix .d,$(basename $<)) -c $< -o $(addsuffix .o,$(basename $<))

clean:
	rm -f bin/*
	rm -f $(call rwildcard,src,*.o)
	rm -f $(call rwildcard,src,*.d)
	rm -f $(call rwildcard,tst,*.tst)

ifneq ($(MAKECMDGOALS),clean)
include $(DEPS)
include $(TSTDEPS)
endif
