rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

CC = cc
LD = cc
AR = ar

CFLAGS   = -std=c99 -Werror -Wextra -Wall -Wimplicit -Wstrict-aliasing \
		 -Iinclude
CFLAGS  += -g
LDFLAGS  = -Llib -lnop

LIBSRCS = $(call rwildcard,src/libnop,*.c)
LIBOBJS = $(LIBSRCS:.c=.o)
LIBDEPS = $(LIBSRCS:.c=.d)

TSTSRCS = $(call rwildcard,tst,*.c)
TSTEXES = $(TSTSRCS:.c=.tst)
TSTDEPS = $(TSTSRCS:.c=.d)

SRCS = $(call rwildcard,src/nopc,*.c)
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

.PHONY: all test clean
.PRECIOUS: $(TSTEXES)

all: bin/nopc test

lib/libnop.a: $(LIBDEPS) $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

bin/nopc: $(DEPS) $(OBJS) lib/libnop.a
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

%.tst: %.o %.d lib/libnop.a
	$(LD) $< -o $@ $(LDFLAGS)
	@echo -n "$@..."
	@$@
	@echo " OK"

%.o %.d: %.c
	$(CC) $(CFLAGS) -MD -MF $(addsuffix .d,$(basename $<)) -c $< -o $(addsuffix .o,$(basename $<))

test: $(TSTEXES)

clean:
	rm -f bin/*
	rm -f $(call rwildcard,src,*.o)
	rm -f $(call rwildcard,src,*.d)
	rm -f $(call rwildcard,tst,*.o)
	rm -f $(call rwildcard,tst,*.d)
	rm -f $(call rwildcard,tst,*.tst)

ifneq ($(MAKECMDGOALS),clean)
include $(LIBDEPS)
include $(TSTDEPS)
include $(DEPS)
endif
