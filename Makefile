# Makefile for building ibnbd-tool

# dpkg-parsechangelog is significantly slow
CHANGELOG = $(shell head -n 1 ./debian/changelog)
DEBVER = $(shell echo "$(CHANGELOG)" | sed -n -e 's/.*(\([^)]*\).*/\1/p')
GITVER = $(shell cat ./.git/HEAD 2>/dev/null | sed -n 's/\(ref: refs\/heads\/\)\?\(.*\)/\2/p' | tr -d '\n')

CC = gcc
DEFINES = -DPACKAGE_VERSION='"$(DEBVER)"' -DGIT_BRANCH='"$(GITVER)"'
CFLAGS = -fPIC -Wall -O2 -g -Iinclude $(DEFINES)
LIBS =

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

TARGETS_OBJ = ibnbd.o
TARGETS = $(TARGETS_OBJ:.o=)

      ibnbd_OBJ = levenshtein.o

.PHONY: all
all: $(TARGETS)

$(TARGETS): $(OBJ)
	$(CC) -o $@ $@.o $($@_OBJ) $(LIBS)


ifneq ($(MAKECMDGOALS),clean)
# do not include for 'clean' goal. make won’t create *.d only to
# immediately remove them again.
-include $(OBJ:.o=.d)
endif

# Stolen from:
# https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html
%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -M $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

.PHONY: clean
clean:
	rm -f *~ $(TARGETS) $(OBJ) $(OBJ:.o=.d)
