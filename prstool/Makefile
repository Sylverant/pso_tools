# *nix Makefile.
# Should build with any standardish C99-supporting compiler.

all: prstool

prstool: prstool.c prs-comp.c prs-decomp.c
	$(CC) -o prstool prstool.c prs-comp.c prs-decomp.c

.PHONY: clean

clean:
	-rm -fr prstool *.o *.dSYM
