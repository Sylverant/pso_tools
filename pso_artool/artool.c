/*
    Sylverant PSO Tools
    PSO Archive Tool
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
#include <string.h>
#include <stdint.h>

/* Available archive types. */
#define ARCHIVE_TYPE_NONE   -1
#define ARCHIVE_TYPE_AFS    0
#define ARCHIVE_TYPE_GSLBE  1

#define ARCHIVE_TYPE_COUNT  2

/* Archive manipulation functions. These should probably go in a header, but I'm
   feeling lazy at the moment. */
int print_afs_files(const char *fn);
int extract_afs(const char *fn);
int create_afs(const char *fn, const char *files[], uint32_t count);
int add_to_afs(const char *fn, const char *files[], uint32_t count);
int update_afs(const char *fn, const char *fno, const char *path);
int delete_from_afs(const char *fn, const char *files[], uint32_t cnt);

int print_gsl_files(const char *fn);
int extract_gsl(const char *fn);
int create_gsl(const char *fn, const char *files[], uint32_t count);
int add_to_gsl(const char *fn, const char *files[], uint32_t count);
int update_gsl(const char *fn, const char *file, const char *path);
int delete_from_gsl(const char *fn, const char *files[], uint32_t cnt);

/* This looks ugly, but whatever. */
struct {
    int (*print)(const char *);
    int (*extract)(const char *);
    int (*create)(const char *, const char *[], uint32_t);
    int (*add)(const char *, const char *[], uint32_t);
    int (*update)(const char *, const char *, const char *);
    int (*delete)(const char *, const char *[], uint32_t);
} archive_funcs[ARCHIVE_TYPE_COUNT] = {
    { &print_afs_files, &extract_afs, &create_afs, &add_to_afs, &update_afs,
      &delete_from_afs },
    { &print_gsl_files, &extract_gsl, &create_gsl, &add_to_gsl, &update_gsl,
      &delete_from_gsl }
};

/* Print information about this program to stdout. */
static void print_program_info(void) {
#if defined(VERSION)
    printf("Sylverant PSO Archive Tool version %s\n", VERSION);
#elif defined(SVN_REVISION)
    printf("Sylverant PSO Archive Tool SVN revision: %s\n", SVN_REVISION);
#else
    printf("Sylverant PSO Archive Tool\n");
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
    printf("Usage:\n"
           "    %s type operation [operation aguments]\n"
           "Where type is one of --afs or --gsl. Available operations and\n"
           "their arguments are shown below:\n"
           "To list the files in an archive:\n"
           "    -t archive\n"
           "To extract an archive:\n"
           "    -x archive\n"
           "To create an archive:\n"
           "    -c archive file1 [file2 ...]\n"
           "To add files to an archive:\n"
           "    -r archive file1 [file2 ...]\n"
           "To update a file in an archive (or replace it with another file):\n"
           "    -u archive file_in_archive filename\n"
           "To remove a file from an archive:\n"
           "    --delete archive file1 [file2 ...]\n\n"
           "Other general operations (which don't require a type):\n"
           "To print this help message:\n"
           "    --help\n"
           "To print version information:\n"
           "    --version\n\n", bin);
    printf("As AFS archives do not store filenames, any files specified in an\n"
           "AFS archive are specified by index within the archive (for the -u\n"
           "and --delete operations). For other archive formats, you should\n"
           "specify the file names within the archive for these operations.\n");
}

/* Parse any command-line arguments passed in. */
static void parse_command_line(int argc, const char *argv[]) {
    int i = 2;
    int t = ARCHIVE_TYPE_NONE;

    if(argc < 2) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if(!strcmp(argv[1], "--afs"))
        t = ARCHIVE_TYPE_AFS;
    else if(!strcmp(argv[1], "--gsl"))
        t = ARCHIVE_TYPE_GSLBE;
    else
        i = 1;

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

    /* See what they asked us to do. */
    if(!strcmp(argv[2], "-t")) {
        if(argc != 4) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(archive_funcs[t].print(argv[3]) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[2], "-x")) {
        if(argc != 4) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(archive_funcs[t].extract(argv[3]) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[2], "-c")) {
        if(argc < 5) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(archive_funcs[t].create(argv[3], argv + 4, argc - 4))
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[2], "-r")) {
        if(argc < 5) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(archive_funcs[t].add(argv[3], argv + 4, argc - 4))
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[2], "-u")) {
        if(argc != 6) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(archive_funcs[t].update(argv[3], argv[4], argv[5]))
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[2], "--delete")) {
        if(argc < 5) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(archive_funcs[t].delete(argv[3], argv + 4, argc - 4))
            exit(EXIT_FAILURE);
    }
    else {
        printf("Illegal archive operation argument: %s\n", argv[2]);
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

int main(int argc, const char *argv[]) {
    /* Parse the command line... */
    parse_command_line(argc, argv);

    return 0;
}
