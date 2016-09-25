/*
    Sylverant PSO Tools
    PSO Archive Tool
    Copyright (C) 2016 Lawrence Sebald

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
#include <string.h>
#include <stdlib.h>
#include <psoarchive/PRS.h>

#ifndef _WIN32
#include <libgen.h>
#endif

extern int write_file(const char *fn, const uint8_t *buf, size_t sz);
extern int read_file(const char *fn, uint8_t **buf);

int prs(int argc, const char *argv[]) {
    uint8_t *dst, *src;
    char *fn, *tmp;
    int sz;

    /* Make sure it's sane... */
    if(argc < 4 || argc > 5)
        return -1;

    /* Parse out the operation requested. */
    if(!strcmp(argv[2], "-x")) {
        /* Extract. */
        if((sz = pso_prs_decompress_file(argv[3], &dst)) < 0) {
            fprintf(stderr, "Cannot extract %s: %s\n", argv[3],
                    pso_strerror(sz));
            return EXIT_FAILURE;
        }

        if(argc == 5) {
            return write_file(argv[4], dst, sz);
        }
        else {
            if(!(fn = (char *)malloc(strlen(argv[3]) + 5))) {
                perror("Cannot write file");
                return EXIT_FAILURE;
            }

            tmp = strdup(argv[3]);
            strcpy(fn, basename(tmp));
            strcat(fn, ".bin");
            sz = write_file(fn, dst, sz);
            free(tmp);
            free(fn);
            return sz;
        }
    }
    else if(!strcmp(argv[2], "-c")) {
        /* Compress. */
        if(argc != 5)
            return -1;

        if((sz = read_file(argv[4], &src)) < 0)
            return EXIT_FAILURE;

        if((sz = pso_prs_compress(src, &dst, sz)) < 0) {
            fprintf(stderr, "Cannot compress %s: %s\n", argv[4],
                    pso_strerror(sz));
            return EXIT_FAILURE;
        }

        free(src);
        sz = write_file(argv[3], dst, sz);
        free(dst);
        return sz;
    }

    return -1;
}
