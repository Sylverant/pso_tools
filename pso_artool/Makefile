# *nix Makefile.
# Should build with any standardish C99-supporting compiler.

SRCS = artool.c prs.c prsd.c afs.c gsl.c
LIBS = -lpsoarchive
TARGET = pso_artool
INSTDIR ?= /usr/local
CFLAGS ?= -Wall -Wextra -I/usr/local/include
LDFLAGS ?= -Wall -Wextra -L/usr/local/lib

# Nothing should have to change below here...

OBJS = $(patsubst %.c,%.o,$(SRCS))

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

.PHONY: clean install

clean:
	-rm -fr $(TARGET) $(OBJS) *.dSYM

install: $(TARGET)
	install -d '$(INSTDIR)/bin'
	install -m 755 $(TARGET) '$(INSTDIR)/bin'
