# *nix Makefile.
# Should build with any standardish C99-supporting compiler.

all: bmltool

bmltool: bmltool.c prs-comp.c prs-decomp.c
	$(CC) -o bmltool bmltool.c prs-comp.c prs-decomp.c

.PHONY: clean

clean:
	-rm -fr bmltool *.o *.dSYM
