/*
    Sylverant PSO Tools
    PSO Archive Tool
    Copyright (C) 2014, 2016 Lawrence Sebald

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
#include <string.h>
#include <stdint.h>

/* Available archive types. */
#define ARCHIVE_TYPE_NONE   -1
#define ARCHIVE_TYPE_AFS    0
#define ARCHIVE_TYPE_GSL    1
#define ARCHIVE_TYPE_PRS    2
#define ARCHIVE_TYPE_PRSD   3

#define ARCHIVE_TYPE_COUNT  4

extern int afs(int argc, const char *argv[]);
extern int gsl(int argc, const char *argv[]);
extern int prs(int argc, const char *argv[]);
extern int prsd(int argc, const char *argv[]);

int (*archive_funcs[ARCHIVE_TYPE_COUNT])(int argc, const char *argv[]) = {
    &afs, &gsl, &prs, &prsd
};

/* Utility functions... */
int write_file(const char *fn, const uint8_t *buf, size_t sz) {
    FILE *fp;

    if(!(fp = fopen(fn, "wb"))) {
        perror("Cannot write file");
        return EXIT_FAILURE;
    }

    if(fwrite(buf, 1, sz, fp) != sz) {
        perror("Cannot write file");
        fclose(fp);
        return EXIT_FAILURE;
    }

    fclose(fp);
    return 0;
}

int read_file(const char *fn, uint8_t **buf) {
    FILE *fp;
    uint8_t *out;
    size_t len;

    if(!(fp = fopen(fn, "rb"))) {
        perror("Cannot read file");
        return -1;
    }

    if(fseek(fp, 0, SEEK_END)) {
        perror("Cannot read file");
        fclose(fp);
        return -1;
    }

    len = (size_t)ftell(fp);

    if(fseek(fp, 0, SEEK_SET)) {
        perror("Cannot read file");
        fclose(fp);
        return -1;
    }

    if(!(out = malloc(len))) {
        perror("Cannot read file");
        fclose(fp);
        return -1;
    }

    if(fread(out, 1, len, fp) != len) {
        perror("Cannot read file");
        free(out);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *buf = out;
    return (int)len;
}

/* Print information about this program to stdout. */
static void print_program_info(void) {
#if defined(VERSION)
    printf("Sylverant PSO Archive Tool version %s\n", VERSION);
#elif defined(GIT_REVISION)
    printf("Sylverant PSO Archive Tool Git revision: %s\n", GIT_REVISION);
#else
    printf("Sylverant PSO Archive Tool\n");
#endif
    printf("Copyright (C) 2014, 2016 Lawrence Sebald\n\n");
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
    printf("General usage:\n"
           "    %s type operation [operation aguments]\n"
           "    %s --help\n"
           "    %s --version\n"
           "Where type is one of the following:\n"
           "    --afs, --afs2, --gsl, --gsl-little, --gsl-big, --prs, --prsd,\n"
           "    --prsd-little, --prsd-big, --prc, --prc-little, or --prc-big\n"
           "    (the prc options are aliases of the prsd ones)\n\n"
           "Available operations per archive type are shown below:\n\n"
           "For AFS (--afs, --afs2) and GSL (--gsl, --gsl-little, --gsl-big)\n"
           "files:\n"
           " -t archive\n"
           "    List all files in the archive.\n"
           " -x archive\n"
           "    Extract all files from the archive.\n"
           " -c archive file1 [file2 ...]\n"
           "    Create a new archive containing the files specified.\n"
           " -r archive file1 [file2 ...]\n"
           "    Append the files specified to an existing archive.\n"
           " -u archive file_in_archive file_on_disk\n"
           "    Update an archive, replacing the file contained in it with\n"
           "    the file on the disk.\n"
           " --delete archive file1 [file2 ...]\n"
           "    Delete the specified files from the archive.\n\n"
           "For PRS (--prs) files:\n"
           " -x archive [to]\n"
           "    Extract the archive to the specified filename. If to is not\n"
           "    specified, the default output filename shall have the same\n"
           "    basename as the archive with the extension .bin appended.\n"
           " -c archive file\n"
           "    Compress the specified file and store it as archive.\n\n"
           "For PRSD/PRC (--prsd, --prsd-little, --prsd-big, --prc, \n"
           "              --prc-little, --prc-big) files:\n"
           " -x archive [to]\n"
           "    Extract the archive to the specified filename. If to is not\n"
           "    specified, the default output filename shall have the same\n"
           "    basename as the archive with the extension .bin appended.\n"
           " -c archive file [key]\n"
           "    Compress the specified file and store it as archive. If\n"
           "    specified, key will be used as the encryption key for the\n"
           "    archive, otherwise a random key will be generated.\n\n",
           bin, bin, bin);
    printf("Many AFS files do not store filenames at all. Files created by\n"
           "this tool with the --afs type will not contain filenames, whereas\n"
           "those created with --afs2 will. If using the --afs type, any\n"
           "files that are specified in an archive (for the -u and --delete\n"
           "operations) must be specified by index, not by name.\n\n");
    printf("GSL and PRSD/PRC archives are supported in both big and\n"
           "little-endian forms. If the endianness is not specified, then it\n"
           "will be auto-detected for operations other than archive creation.\n"
           "For archive creation, little-endian mode is assumed if the\n"
           "endianness is not specified.\n"
           "Big-endian archives are used in PSO for Gamecube, whereas all\n"
           "other versions of the game use little-endian archives.\n\n");
}

/* Parse any command-line arguments passed in. */
static void parse_command_line(int argc, const char *argv[]) {
    int i = 2;
    int t = ARCHIVE_TYPE_NONE;

    if(argc < 2) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if(!strcmp(argv[1], "--afs") || !strcmp(argv[1], "--afs2")) {
        t = ARCHIVE_TYPE_AFS;
    }
    else if(!strcmp(argv[1], "--gsl") || !strcmp(argv[1], "--gsl-little") ||
            !strcmp(argv[1], "--gsl-big")) {
        t = ARCHIVE_TYPE_GSL;
    }
    else if(!strcmp(argv[1], "--prs")) {
        t = ARCHIVE_TYPE_PRS;
    }
    else if(!strcmp(argv[1], "--prsd") || !strcmp(argv[1], "--prsd-little") ||
            !strcmp(argv[1], "--prsd-big") || !strcmp(argv[1], "--prc") ||
            !strcmp(argv[1], "--prc-little") || !strcmp(argv[1], "--prc-big")) {
        t = ARCHIVE_TYPE_PRSD;
    }
    else {
        i = 1;
    }

    if(!strcmp(argv[i], "--version")) {
        print_program_info();
        exit(EXIT_SUCCESS);
    }
    else if(!strcmp(argv[i], "--help")) {
        print_help(argv[0]);
        exit(EXIT_SUCCESS);
    }

    /* All other operations require an archive type, so if we didn't get one,
       then bail out. */
    if(i == 1) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Leave it up to the handler to do the rest of the work. */
    i = archive_funcs[t](argc, argv);
    if(i == -1) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    exit(i);
}

int main(int argc, const char *argv[]) {
    /* Parse the command line... */
    parse_command_line(argc, argv);

    return 0;
}
