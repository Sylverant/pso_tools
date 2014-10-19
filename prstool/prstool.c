/*
    Sylverant PSO Tools
    PRS Archive Compression/Decompression Tool
    Copyright (C) 2014 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sylverant/prs.h>

static const char *in_file, *out_file;
static int operation = 0;

/* Print information about this program to stdout. */
static void print_program_info(void) {
#if defined(VERSION)
    printf("Sylverant PRS Tool version %s\n", VERSION);
#elif defined(SVN_REVISION)
    printf("Sylverant PRS Tool SVN revision: %s\n", SVN_REVISION);
#else
    printf("Sylverant PRS Tool\n");
#endif
    printf("Copyright (C) 2014 Lawrence Sebald\n\n");
    printf("This program is free software: you can redistribute it and/or\n"
           "modify it under the terms of the GNU Affero General Public\n"
           "License version 3 as published by the Free Software Foundation.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n\n"
           "You should have received a copy of the GNU Affero General Public\n"
           "License along with this program.  If not, see "
           "<http://www.gnu.org/licenses/>.\n");
}

/* Print help to the user to stdout. */
static void print_help(const char *bin) {
    printf("Usage: %s arguments [input_file] [output_file]\n"
           "-----------------------------------------------------------------\n"
           "--help          Print this help and exit\n"
           "--version       Print version info and exit\n"
           "-x              Decompress input_file into output_file\n"
           "-c              Compress input_file into output_file\n", bin);
}

/* Parse any command-line arguments passed in. */
static void parse_command_line(int argc, char *argv[]) {
    int i;

    if(argc < 4) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    for(i = 1; i < argc - 1; ++i) {
        if(!strcmp(argv[i], "--version")) {
            print_program_info();
            exit(EXIT_SUCCESS);
        }
        else if(!strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else if(!strcmp(argv[i], "-x")) {
            if(argc != 4) {
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }

            operation = 2;
        }
        else if(!strcmp(argv[i], "-c")) {
            if(argc != 4) {
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }

            operation = 1;
        }
        else {
            printf("Illegal command line argument: %s\n", argv[i]);
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Did we get something to do? */
    if(!operation) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Save the files we'll be working with. */
    in_file = argv[argc - 2];
    out_file = argv[argc - 1];
}

static uint8_t *read_input(long *len) {
    FILE *ifp;
    uint8_t *rv;

    if(!(ifp = fopen(in_file, "rb"))) {
        perror("");
        exit(EXIT_FAILURE);
    }

    /* Figure out the length of the file. */
    if(fseek(ifp, 0, SEEK_END)) {
        perror("");
        exit(EXIT_FAILURE);
    }

    if((*len = ftell(ifp)) < 0) {
        perror("");
        exit(EXIT_FAILURE);
    }

    if(fseek(ifp, 0, SEEK_SET)) {
        perror("");
        exit(EXIT_FAILURE);
    }

    /* Allocate a buffer. */
    if(!(rv = malloc(*len))) {
        perror("");
        exit(EXIT_FAILURE);
    }

    /* Read in the file. */
    if(fread(rv, 1, *len, ifp) != *len) {
        perror("");
        exit(EXIT_FAILURE);
    }

    /* Clean up. */
    fclose(ifp);
    return rv;
}

static void write_output(int len, const uint8_t *buf) {
    FILE *ofp;

    if(!(ofp = fopen(out_file, "wb"))) {
        perror("");
        exit(EXIT_FAILURE);
    }

    if(fwrite(buf, 1, len, ofp) != len) {
        perror("");
        exit(EXIT_FAILURE);
    }

    fclose(ofp);
}

static void decompress(void) {
    uint8_t *buf;
    int len;

    /* Decompress the file. */
    if((len = prs_decompress_file(in_file, &buf)) < 0) {
        fprintf(stderr, "decompress: %s\n", strerror(-len));
        exit(EXIT_FAILURE);
    }

    /* Write it out. */
    write_output(len, buf);

    /* Clean up. */
    free(buf);
}

static void compress(void) {
    long unc_len;
    uint8_t *unc, *cmp;
    int cmp_len;

    /* Read the file in */
    unc = read_input(&unc_len);

    /* Compress it. */
    if((cmp_len = prs_compress(unc, &cmp, (size_t)unc_len)) < 0) {
        fprintf(stderr, "compress; %s\n", strerror(-cmp_len));
        exit(EXIT_FAILURE);
    }

    /* Write it out to the output file. */
    write_output(cmp_len, cmp);

    /* Clean up. */
    free(cmp);
    free(unc);
}

int main(int argc, char *argv[]) {
    /* Parse the command line... */
    parse_command_line(argc, argv);

    if(operation == 1)
        compress();
    else
        decompress();

    return 0;
}
