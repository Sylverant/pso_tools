# *nix Makefile.
# Should build with any standardish C99-supporting compiler.

all: pso_artool

pso_artool:
	$(CC) -o pso_artool artool.c afs.c gsl.c

.PHONY: clean

clean:
	-rm -fr pso_artool *.o *.dSYM
