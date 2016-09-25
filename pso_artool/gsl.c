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
#include <errno.h>

#include <sys/stat.h>

#ifndef _WIN32
#include <inttypes.h>
#include <libgen.h>
#include <unistd.h>
#endif

#include <psoarchive/GSL.h>

static uint32_t endian = 0;

static int digits(uint32_t n) {
    int r = 1;
    while(n /= 10) ++r;
    return r;
}

static int gsl_list(const char *fn) {
    pso_gsl_read_t *cxt;
    pso_error_t err;
    uint32_t cnt, i;
    int dg;
    ssize_t sz;
    char afn[64];

    if(!(cxt = pso_gsl_read_open(fn, endian, &err))) {
        fprintf(stderr, "Cannot open archive %s: %s\n", fn, pso_strerror(err));
        return EXIT_FAILURE;
    }

    /* Loop through each file... */
    cnt = pso_gsl_file_count(cxt);
    dg = digits(cnt);

    for(i = 0; i < cnt; ++i) {
        sz = pso_gsl_file_size(cxt, i);
        pso_gsl_file_name(cxt, i, afn, 64);

#ifndef _WIN32
        printf("File %*" PRIu32 ": '%s' size: %" PRIu32 "\n", dg, i, afn,
               (uint32_t)sz);
#else
        printf("File %*I32u: '%s' size: %I32u\n", dg, i, afn, (uint32_t)sz);
#endif
    }

    pso_gsl_read_close(cxt);
    return 0;
}

static int gsl_extract(const char *fn) {
    pso_gsl_read_t *cxt;
    pso_error_t err;
    uint32_t cnt, i;
    ssize_t sz;
    char afn[64];
    FILE *fp;
    uint8_t *buf;

    if(!(cxt = pso_gsl_read_open(fn, endian, &err))) {
        fprintf(stderr, "Cannot open archive %s: %s\n", fn, pso_strerror(err));
        return EXIT_FAILURE;
    }

    /* Loop through each file... */
    cnt = pso_gsl_file_count(cxt);

    for(i = 0; i < cnt; ++i) {
        if((sz = pso_gsl_file_size(cxt, i)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            pso_gsl_read_close(cxt);
            return EXIT_FAILURE;
        }

        if(pso_gsl_file_name(cxt, i, afn, 64) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            pso_gsl_read_close(cxt);
            return EXIT_FAILURE;
        }

        if(!(buf = malloc(sz))) {
            perror("Cannot extract file");
            pso_gsl_read_close(cxt);
            return EXIT_FAILURE;
        }

        if((err = pso_gsl_file_read(cxt, i, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            free(buf);
            pso_gsl_read_close(cxt);
            return EXIT_FAILURE;
        }

        if(!(fp = fopen(afn, "wb"))) {
            perror("Cannot extract file");
            free(buf);
            pso_gsl_read_close(cxt);
            return EXIT_FAILURE;
        }

        if(fwrite(buf, 1, sz, fp) != (size_t)sz) {
            perror("Cannot extract file");
            fclose(fp);
            free(buf);
            pso_gsl_read_close(cxt);
            return EXIT_FAILURE;
        }

        /* Clean up, we're done with this file. */
        fclose(fp);
        free(buf);
    }

    pso_gsl_read_close(cxt);
    return 0;
}

static int gsl_create(const char *fn, int file_cnt, const char *files[]) {
    pso_gsl_write_t *cxt;
    pso_error_t err;
    int i;
    char *tmp, *bn;

    if(!(cxt = pso_gsl_new(fn, endian, &err))) {
        fprintf(stderr, "Cannot create archive %s: %s\n", fn,
                pso_strerror(err));
        return EXIT_FAILURE;
    }

    if((err = pso_gsl_write_set_ftab_size(cxt, file_cnt)) < 0) {
        fprintf(stderr, "Cannot create archive %s: %s\n", fn,
                pso_strerror(err));
        pso_gsl_write_close(cxt);
        return EXIT_FAILURE;
    }

    for(i = 0; i < file_cnt; ++i) {
        if(!(tmp = strdup(files[i]))) {
            perror("Cannot create archive");
            pso_gsl_write_close(cxt);
            return EXIT_FAILURE;
        }

        /* Figure out the basename of the file... */
        bn = basename(tmp);

        /* Add the file to the archive. */
        if((err = pso_gsl_write_add_file(cxt, bn, files[i]))) {
            fprintf(stderr, "Cannot add file '%s' to archive: %s\n", files[i],
                    pso_strerror(err));
            free(tmp);
            pso_gsl_write_close(cxt);
            return EXIT_FAILURE;
        }

        free(tmp);
    }

    pso_gsl_write_close(cxt);
    return 0;
}

static int gsl_append(const char *fn, int file_cnt, const char *files[]) {
    pso_gsl_read_t *rcxt;
    pso_gsl_write_t *wcxt;
    pso_error_t err;
    uint32_t cnt, i;
    int fd, j;
    ssize_t sz;
    uint8_t *buf;
    char afn[64], tmpfn[16];
    char *bn, *tmp;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Create a temporary file for the new archive... */
    strcpy(tmpfn, "artoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        perror("Cannot create temporary file");
        return EXIT_FAILURE;
    }

    /* Open the source archive. */
    if(!(rcxt = pso_gsl_read_open(fn, endian, &err))) {
        fprintf(stderr, "Cannot open archive %s: %s\n", fn, pso_strerror(err));
        close(fd);
        unlink(tmpfn);
        return EXIT_FAILURE;
    }

    /* Create a context to write to the temporary file. */
    if(!(wcxt = pso_gsl_new_fd(fd, endian, &err))) {
        fprintf(stderr, "Cannot create archive: %s\n", pso_strerror(err));
        pso_gsl_read_close(rcxt);
        close(fd);
        unlink(tmpfn);
        return EXIT_FAILURE;
    }

    /* Loop through each file that's already in the archive... */
    cnt = pso_gsl_file_count(rcxt);

    if((err = pso_gsl_write_set_ftab_size(wcxt, file_cnt + cnt)) < 0) {
        fprintf(stderr, "Cannot create archive %s: %s\n", fn,
                pso_strerror(err));
        goto err_out;
    }

    for(i = 0; i < cnt; ++i) {
        if((sz = pso_gsl_file_size(rcxt, i)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            goto err_out;
        }

        if(pso_gsl_file_name(rcxt, i, afn, 64) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            goto err_out;
        }

        if(!(buf = malloc(sz))) {
            perror("Cannot extract file");
            goto err_out;
        }

        if((err = pso_gsl_file_read(rcxt, i, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            free(buf);
            goto err_out;
        }

        if((err = pso_gsl_write_add(wcxt, afn, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot add file to archive: %s\n",
                    pso_strerror(sz));
            free(buf);
            goto err_out;
        }

        /* Clean up, we're done with this file. */
        free(buf);
    }

    /* We're done with the old archive... */
    pso_gsl_read_close(rcxt);

    /* Loop through all the new files. */
    for(j = 0; j < file_cnt; ++j) {
        if(!(tmp = strdup(files[j]))) {
            perror("Cannot add file to archive");
            goto err_out2;
        }

        /* Figure out the basename of the file. */
        bn = basename(tmp);

        /* Add the file to the archive. */
        if((err = pso_gsl_write_add_file(wcxt, bn, files[j]))) {
            fprintf(stderr, "Cannot add file '%s' to archive: %s\n", files[j],
                    pso_strerror(err));
            free(tmp);
            goto err_out2;
        }

        free(tmp);
    }

    /* Done writing to the temporary file, time to overwrite the archive... */
    pso_gsl_write_close(wcxt);

#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fd, (~mask) & 0666);
#endif

    rename(tmpfn, fn);

    return 0;

err_out:
    pso_gsl_read_close(rcxt);

err_out2:
    pso_gsl_write_close(wcxt);
    close(fd);
    unlink(tmpfn);
    return EXIT_FAILURE;
}

static int gsl_update(const char *fn, const char *oldfn, const char *newfn) {
    pso_gsl_read_t *rcxt;
    pso_gsl_write_t *wcxt;
    pso_error_t err;
    uint32_t cnt, i;
    int fd, replace = 0;
    ssize_t sz;
    uint8_t *buf;
    char afn[64], tmpfn[16];

#ifndef _WIN32
    mode_t mask;
#endif

    /* Create a temporary file for the new archive... */
    strcpy(tmpfn, "artoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        perror("Cannot create temporary file");
        return EXIT_FAILURE;
    }

    /* Open the source archive. */
    if(!(rcxt = pso_gsl_read_open(fn, endian, &err))) {
        fprintf(stderr, "Cannot open archive %s: %s\n", fn, pso_strerror(err));
        close(fd);
        unlink(tmpfn);
        return EXIT_FAILURE;
    }

    /* Create a context to write to the temporary file. */
    if(!(wcxt = pso_gsl_new_fd(fd, endian, &err))) {
        fprintf(stderr, "Cannot create archive: %s\n", pso_strerror(err));
        pso_gsl_read_close(rcxt);
        close(fd);
        unlink(tmpfn);
        return EXIT_FAILURE;
    }

    /* Loop through each file that's already in the archive... */
    cnt = pso_gsl_file_count(rcxt);

    if((err = pso_gsl_write_set_ftab_size(wcxt, cnt)) < 0) {
        fprintf(stderr, "Cannot create archive %s: %s\n", fn,
                pso_strerror(err));
        goto err_out;
    }

    for(i = 0; i < cnt; ++i) {
        if(pso_gsl_file_name(rcxt, i, afn, 64) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            goto err_out;
        }

        /* See if this file is the one we're updating... */
        if(!replace) {
            if(!strcmp(afn, oldfn))
                replace = 1;
        }

        /* Was this the one to replace? */
        if(replace == 1) {
            if((err = pso_gsl_write_add_file(wcxt, afn, newfn))) {
                fprintf(stderr, "Cannot add file to archive: %s\n",
                        pso_strerror(sz));
                goto err_out;
            }

            replace = 2;
            continue;
        }

        if((sz = pso_gsl_file_size(rcxt, i)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            goto err_out;
        }

        if(!(buf = malloc(sz))) {
            perror("Cannot extract file");
            goto err_out;
        }

        if((err = pso_gsl_file_read(rcxt, i, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            free(buf);
            goto err_out;
        }

        if((err = pso_gsl_write_add(wcxt, afn, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot add file to archive: %s\n",
                    pso_strerror(sz));
            free(buf);
            goto err_out;
        }

        /* Clean up, we're done with this file. */
        free(buf);
    }

    /* We're done with both of the archives... */
    pso_gsl_read_close(rcxt);
    pso_gsl_write_close(wcxt);

#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fd, (~mask) & 0666);
#endif

    rename(tmpfn, fn);

    return 0;

err_out:
    pso_gsl_read_close(rcxt);
    pso_gsl_write_close(wcxt);
    close(fd);
    unlink(tmpfn);
    return EXIT_FAILURE;
}

static int gsl_delete(const char *fn, int file_cnt, const char *files[]) {
    pso_gsl_read_t *rcxt;
    pso_gsl_write_t *wcxt;
    pso_error_t err;
    uint32_t cnt, i;
    int fd, j, skip = 0;
    ssize_t sz;
    uint8_t *buf;
    char afn[64], tmpfn[16];

#ifndef _WIN32
    mode_t mask;
#endif

    /* Create a temporary file for the new archive... */
    strcpy(tmpfn, "artoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        perror("Cannot create temporary file");
        return EXIT_FAILURE;
    }

    /* Open the source archive. */
    if(!(rcxt = pso_gsl_read_open(fn, endian, &err))) {
        fprintf(stderr, "Cannot open archive %s: %s\n", fn, pso_strerror(err));
        close(fd);
        unlink(tmpfn);
        return EXIT_FAILURE;
    }

    /* Create a context to write to the temporary file. */
    if(!(wcxt = pso_gsl_new_fd(fd, endian, &err))) {
        fprintf(stderr, "Cannot create archive: %s\n", pso_strerror(err));
        pso_gsl_read_close(rcxt);
        close(fd);
        unlink(tmpfn);
        return EXIT_FAILURE;
    }

    /* Loop through each file that's already in the archive... */
    cnt = pso_gsl_file_count(rcxt);

    /* Play it safe here, in case we don't actually end up deleting anything. */
    if((err = pso_gsl_write_set_ftab_size(wcxt, cnt)) < 0) {
        fprintf(stderr, "Cannot create archive %s: %s\n", fn,
                pso_strerror(err));
        goto err_out;
    }

    for(i = 0; i < cnt; ++i) {
        if(pso_gsl_file_name(rcxt, i, afn, 64) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            goto err_out;
        }

        /* See if this file is in the list to delete. */
        for(j = 0; j < file_cnt; ++j) {
            if(!strcmp(afn, files[j])) {
                skip = 1;
                break;
            }
        }

        if(skip)
            continue;

        if((sz = pso_gsl_file_size(rcxt, i)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            goto err_out;
        }

        if(!(buf = malloc(sz))) {
            perror("Cannot extract file");
            goto err_out;
        }

        if((err = pso_gsl_file_read(rcxt, i, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot extract file: %s\n", pso_strerror(sz));
            free(buf);
            goto err_out;
        }

        if((err = pso_gsl_write_add(wcxt, afn, buf, (size_t)sz)) < 0) {
            fprintf(stderr, "Cannot add file to archive: %s\n",
                    pso_strerror(sz));
            free(buf);
            goto err_out;
        }

        /* Clean up, we're done with this file. */
        free(buf);
    }

    /* We're done with both of the archives... */
    pso_gsl_read_close(rcxt);
    pso_gsl_write_close(wcxt);

#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fd, (~mask) & 0666);
#endif

    rename(tmpfn, fn);

    return 0;

err_out:
    pso_gsl_read_close(rcxt);
    pso_gsl_write_close(wcxt);
    close(fd);
    unlink(tmpfn);
    return EXIT_FAILURE;
}

int gsl(int argc, const char *argv[]) {
    if(argc < 4)
        return -1;

    /* Which style of GSL archive are we making? */
    if(!strcmp(argv[1], "--gsl-little")) {
        /* Little endian. */
        endian = PSO_GSL_LITTLE_ENDIAN;
    }
    else if(!strcmp(argv[1], "--gsl-big")) {
        /* Big endian. */
        endian = PSO_GSL_BIG_ENDIAN;
    }

    if(!strcmp(argv[2], "-t")) {
        /* List archive. */
        if(argc != 4)
            return -1;

        return gsl_list(argv[3]);
    }
    else if(!strcmp(argv[2], "-x")) {
        /* Extract. */
        if(argc != 4)
            return -1;

        return gsl_extract(argv[3]);
    }
    else if(!strcmp(argv[2], "-c")) {
        /* Create archive. */
        if(argc < 5)
            return -1;

        return gsl_create(argv[3], argc - 4, argv + 4);
    }
    else if(!strcmp(argv[2], "-r")) {
        /* Append file(s). */
        if(argc < 5)
            return -1;

        return gsl_append(argv[3], argc - 4, argv + 4);
    }
    else if(!strcmp(argv[2], "-u")) {
        /* Update archived file. */
        if(argc != 6)
            return -1;

        return gsl_update(argv[3], argv[4], argv[5]);
    }
    else if(!strcmp(argv[2], "--delete")) {
        /* Delete file from archive. */
        if(argc < 5)
            return -1;

        return gsl_delete(argv[3], argc - 4, argv + 4);
    }

    return -1;
}
