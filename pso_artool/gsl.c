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

/* This code extracts GSL archives and performs various other tasks on GSL
   archive files. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <sys/stat.h>

#ifndef _WIN32
#include <inttypes.h>
#include <unistd.h>
#include <libgen.h>
#endif

#define GSL_AUTO    -1
#define GSL_BIG     0
#define GSL_LITTLE  1

static uint8_t xbuf[512];

static int endianness = GSL_AUTO;

struct delete_cxt {
    FILE *fp;
    uint32_t item_count;
    const char **items;
    long fpos;
    long wpos;
    uint32_t copied_files;
};

struct update_cxt {
    FILE *fp;
    const char *fn;
    const char *path;
    long fpos;
    long wpos;
};

#ifdef _WIN32
/* In windows_compat.c */
char *basename(char *input);
#endif

static int copy_file(FILE *dst, FILE *src, uint32_t size) {
    /* Read in the file in 512-byte chunks, writing each one out to the
       output file (incuding the last chunk, which may be less than 512
       bytes in length). */
    while(size > 512) {
        if(fread(xbuf, 1, 512, src) != 512) {
            printf("Error reading file: %s\n", strerror(errno));
            return -1;
        }

        if(fwrite(xbuf, 1, 512, dst) != 512) {
            printf("Error writing file: %s\n", strerror(errno));
            return -2;
        }

        size -= 512;
    }

    if(size) {
        if(fread(xbuf, 1, size, src) != size) {
            printf("Error reading file: %s\n", strerror(errno));
            return -3;
        }

        if(fwrite(xbuf, 1, size, dst) != size) {
            printf("Error writing file: %s\n", strerror(errno));
            return -4;
        }
    }

    return 0;
}

static long pad_file(FILE *fp, int boundary) {
    long pos = ftell(fp);
    uint8_t tmp = 0;

    /* If we aren't actually padding, don't do anything. */
    if(boundary <= 0)
        return pos;

    pos = (pos & ~(boundary - 1)) + boundary;

    if(fseek(fp, pos - 1, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(&tmp, 1, 1, fp) != 1) {
        printf("Cannot write to archive: %s\n", strerror(errno));
        return -1;
    }

    return pos;
}

static FILE *open_gsl(const char *fn) {
    FILE *fp;

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        printf("Cannot open %s: %s\n", fn, strerror(errno));
        return NULL;
    }

    /* TODO: Perhaps we should sanity check the file here a bit? Doing so is a
       bit more difficult than with an AFS file, since there is no magic number
       or anything like that at the top of the file. */

    return fp;
}

static int scan_gsl(const char *fn, int (*p)(FILE *fp, uint32_t i, uint32_t sz,
                                             const char *fn, void *d),
                    void *userdata) {
    FILE *fp;
    uint8_t buf[4];
    char filename[33];
    int rv = 0;
    uint32_t offset, size, i;
    long next, total;

    /* Open up the file */
    if(!(fp = open_gsl(fn)))
        return -1;

    /* Figure out the length of the file. */
    if(fseek(fp, 0, SEEK_END)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }

    if((total = ftell(fp)) < 0) {
        printf("Cannot determine length of file: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }

    if(fseek(fp, 0, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }

    /* Read in each file in the archive, writing each one out. */
    for(i = 0; ; ++i) {
        /* Read the filename. */
        if(fread(filename, 1, 32, fp) != 32) {
            printf("Error reading file %s: %s\n", fn, strerror(errno));
            rv = -2;
            goto out;
        }

        /* If we have an empty filename, we've hit the end of the list. */
        if(filename[0] == '\0') {
            rv = (int)i;
            break;
        }

        filename[32] = '\0';

        /* Figure out where the next file starts in the archive. */
        if(fread(buf, 1, 4, fp) != 4) {
            printf("Error reading file %s: %s\n", fn, strerror(errno));
            rv = -3;
            goto out;
        }

        /* If the user hasn't specified the endianness, then try to guess. */
        if(endianness == GSL_AUTO) {
            /* Guess big endian first. */
            offset = (buf[3]) | (buf[2] << 8) | (buf[1] << 16) | (buf[0] << 24);
            endianness = GSL_BIG;

            /* If the offset of the file is outside of the archive length, the
               we probably guessed wrong, try as little endian. */
            if(offset > (uint32_t)total || offset * 2048 > (uint32_t)total) {
                offset = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) |
                    (buf[0]);
                endianness = GSL_LITTLE;

                if(offset * 2048 > (uint32_t)total) {
                    printf("GSL file looks corrupt. Cowardly refusing to even "
                           "attempt further operation.\n");
                    rv = -9;
                    goto out;
                }
            }
        }
        else if(endianness == GSL_BIG) {
            offset = (buf[3]) | (buf[2] << 8) | (buf[1] << 16) | (buf[0] << 24);
        }
        else {
            offset = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
        }

        /* Figure out the size of the next file. */
        if(fread(buf, 1, 4, fp) != 4) {
            printf("Error reading file5 %s: %s\n", fn, strerror(errno));
            rv = -4;
            goto out;
        }

        if(endianness == GSL_BIG) {
            size = (buf[3]) | (buf[2] << 8) | (buf[1] << 16) | (buf[0] << 24);
        }
        else {
            size = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
        }

        /* Seek over the blank padding space. */
        if(fseek(fp, 8, SEEK_CUR)) {
            printf("Seek error: %s\n", strerror(errno));
            rv = -5;
            goto out;
        }

        next = ftell(fp);

        if(fseek(fp, (long)offset * 2048, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            rv = -6;
            goto out;
        }

        /* Call the callback function. */
        if(p && p(fp, i, size, filename, userdata)) {
            rv = -7;
            goto out;
        }

        /* Move back to the file table to go onto the next file. */
        if(fseek(fp, next, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            rv = -8;
            goto out;
        }
    }

out:
    fclose(fp);
    return rv;
}

static int add_files_to_gsl(FILE *ofp, long fpos, long wpos,
                            const char *files[], uint32_t count,
                            long *rfpos, long *rwpos) {
    uint32_t i;
    FILE *ifp;
    uint32_t size;
    uint8_t buf[32];
    char *tmp, *filename;
    size_t fnlen;
    long wposp;

    /* Scan through each entry, writing the file to the archive. */
    for(i = 0; i < count; ++i) {
        /* Open the input file. */
        if(!(ifp = fopen(files[i], "rb"))) {
            printf("Cannot open file '%s': %s\n", files[i], strerror(errno));
            return -1;
        }

        /* Figure out its size. */
        if(fseek(ifp, 0, SEEK_END)) {
            printf("Seek error: %s\n", strerror(errno));
            fclose(ifp);
            return -2;
        }

        size = (uint32_t)ftell(ifp);

        if(fseek(ifp, 0, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            fclose(ifp);
            return -3;
        }

        /* Get the filename from the path. */
        if(!(tmp = strdup(files[i]))) {
            printf("Cannot copy filename string: %s\n", strerror(errno));
            fclose(ifp);
            return -9;
        }

        if(!(filename = basename(tmp))) {
            printf("Cannot find basename of path: %s\n", strerror(errno));
            free(tmp);
            fclose(ifp);
            return -10;
        }

        if((fnlen = strlen(filename)) >= 32) {
            printf("File name \"%s\" too long (must be 31 or less chars)\n",
                   filename);
            free(tmp);
            fclose(ifp);
            return -11;
        }

        /* Write the filename to the file. */
        if(fwrite(filename, 1, fnlen, ofp) != fnlen) {
            printf("Cannot write to archive: %s\n", strerror(errno));
            free(tmp);
            fclose(ifp);
            return -11;
        }

        memset(buf, 0, 32);

        if(fwrite(buf, 1, 32 - fnlen, ofp) != 32 - fnlen) {
            printf("Cannot write to archive: %s\n", strerror(errno));
            free(tmp);
            fclose(ifp);
            return -12;
        }

        /* Free the copied filename. */
        free(tmp);

        /* Write the file's information into the file table. */
        wposp = wpos >> 11;
        if(endianness == GSL_BIG) {
            buf[0] = (uint8_t)(wposp >> 24);
            buf[1] = (uint8_t)(wposp >> 16);
            buf[2] = (uint8_t)(wposp >> 8);
            buf[3] = (uint8_t)(wposp);
            buf[4] = (uint8_t)(size >> 24);
            buf[5] = (uint8_t)(size >> 16);
            buf[6] = (uint8_t)(size >> 8);
            buf[7] = (uint8_t)(size);
        }
        else {
            buf[0] = (uint8_t)(wposp);
            buf[1] = (uint8_t)(wposp >> 8);
            buf[2] = (uint8_t)(wposp >> 16);
            buf[3] = (uint8_t)(wposp >> 24);
            buf[4] = (uint8_t)(size);
            buf[5] = (uint8_t)(size >> 8);
            buf[6] = (uint8_t)(size >> 16);
            buf[7] = (uint8_t)(size >> 24);
        }

        buf[8] = 0;
        buf[9] = 0;
        buf[10] = 0;
        buf[11] = 0;
        buf[12] = 0;
        buf[13] = 0;
        buf[14] = 0;
        buf[15] = 0;

        if(fwrite(buf, 1, 16, ofp) != 16) {
            printf("Cannot write to archive: %s\n", strerror(errno));
            fclose(ifp);
            return -4;
        }

        fpos = ftell(ofp);

        /* Write the file itself to the archive. */
        if(fseek(ofp, wpos, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            fclose(ifp);
            return -5;
        }

        if(copy_file(ofp, ifp, size)) {
            fclose(ifp);
            return -6;
        }

        /* Add padding, as needed. */
        if((wpos = pad_file(ofp, 2048)) == -1) {
            fclose(ifp);
            return -7;
        }

        /* Rewind back to the file table for the next entry. */
        if(fseek(ofp, fpos, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            fclose(ifp);
            return -8;
        }

        /* Close the input file. */
        fclose(ifp);
    }

    if(rfpos)
        *rfpos = fpos;

    if(rwpos)
        *rwpos = wpos;

    return 0;
}

static int print_file_info(FILE *fp, uint32_t i, uint32_t sz, const char *fn,
                           void *d) {
    uint32_t offset = (uint32_t)ftell(fp);

#ifndef _WIN32
    printf("File %4" PRIu32 " '%s' @ offset %#010" PRIx32 " size: %" PRIu32
           "\n", i, fn, offset, sz);
#else
    printf("File %4d '%s' @ offset %#010x size %d\n", i, fn, offset, sz);
#endif
    return 0;
}

static int extract_file(FILE *fp, uint32_t i, uint32_t sz, const char *fn,
                        void *d) {
    FILE *ofp;

    /* Open the output file. */
    if(!(ofp = fopen(fn, "wb"))) {
        printf("Cannot open file '%s' for write: %s\n", fn, strerror(errno));
        return -1;
    }

    /* Copy the data out into its new file. */
    if(copy_file(ofp, fp, sz)) {
        fclose(ofp);
        return -2;
    }

    /* We're done with this file, return to the scan function. */
    fclose(ofp);
    return 0;
}

static int copy_file_cb(FILE *fp, uint32_t i, uint32_t sz,
                        const char *fn, void *d) {
    FILE *ofp = (FILE *)d;
    long fpos = (uint32_t)(i * 48);
    uint32_t wpos = (uint32_t)ftell(ofp), wposp = wpos >> 11;
    uint8_t buf[48];

    /* Copy the filename, filling in zero bytes as needed. */
    strncpy((char *)buf, fn, 32);

    /* Fill in the header data for the file. */
    if(endianness == GSL_BIG) {
        buf[32] = (uint8_t)(wposp >> 24);
        buf[33] = (uint8_t)(wposp >> 16);
        buf[34] = (uint8_t)(wposp >> 8);
        buf[35] = (uint8_t)(wposp);
        buf[36] = (uint8_t)(sz >> 24);
        buf[37] = (uint8_t)(sz >> 16);
        buf[38] = (uint8_t)(sz >> 8);
        buf[39] = (uint8_t)(sz);
    }
    else {
        buf[32] = (uint8_t)(wposp);
        buf[33] = (uint8_t)(wposp >> 8);
        buf[34] = (uint8_t)(wposp >> 16);
        buf[35] = (uint8_t)(wposp >> 24);
        buf[36] = (uint8_t)(sz);
        buf[37] = (uint8_t)(sz >> 8);
        buf[38] = (uint8_t)(sz >> 16);
        buf[39] = (uint8_t)(sz >> 24);
    }

    buf[40] = 0;
    buf[41] = 0;
    buf[42] = 0;
    buf[43] = 0;
    buf[44] = 0;
    buf[45] = 0;
    buf[46] = 0;
    buf[47] = 0;

    if(fseek(ofp, fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(buf, 1, 48, ofp) != 48) {
        printf("Cannot write to file: %s\n", strerror(errno));
        return -2;
    }

    if(fseek(ofp, wpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -3;
    }

    /* Copy the file over from the old archive to the new one. */
    if(copy_file(ofp, fp, sz))
        return -4;

    /* Add padding, as needed. */
    if(pad_file(ofp, 2048) < 0)
        return -5;

    return 0;
}

static int copy_filtered(FILE *fp, uint32_t i, uint32_t sz, const char *fn,
                         void *d) {
    struct delete_cxt *cxt = (struct delete_cxt *)d;
    uint32_t j;
    uint32_t wposp = cxt->wpos >> 11;
    uint8_t buf[48];

    /* Look if we're supposed to leave this one off. */
    for(j = 0; j < cxt->item_count; ++j) {
        if(!strcmp(cxt->items[j], fn))
            return 0;
    }

    /* Copy the filename, filling in zero bytes as needed. */
    strncpy((char *)buf, fn, 32);

    /* Fill in the header data for the file. */
    if(endianness == GSL_BIG) {
        buf[32] = (uint8_t)(wposp >> 24);
        buf[33] = (uint8_t)(wposp >> 16);
        buf[34] = (uint8_t)(wposp >> 8);
        buf[35] = (uint8_t)(wposp);
        buf[36] = (uint8_t)(sz >> 24);
        buf[37] = (uint8_t)(sz >> 16);
        buf[38] = (uint8_t)(sz >> 8);
        buf[39] = (uint8_t)(sz);
    }
    else {
        buf[32] = (uint8_t)(wposp);
        buf[33] = (uint8_t)(wposp >> 8);
        buf[34] = (uint8_t)(wposp >> 16);
        buf[35] = (uint8_t)(wposp >> 24);
        buf[36] = (uint8_t)(sz);
        buf[37] = (uint8_t)(sz >> 8);
        buf[38] = (uint8_t)(sz >> 16);
        buf[39] = (uint8_t)(sz >> 24);
    }

    buf[40] = 0;
    buf[41] = 0;
    buf[42] = 0;
    buf[43] = 0;
    buf[44] = 0;
    buf[45] = 0;
    buf[46] = 0;
    buf[47] = 0;

    if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(buf, 1, 48, cxt->fp) != 48) {
        printf("Cannot write to file: %s\n", strerror(errno));
        return -2;
    }

    cxt->fpos += 48;

    if(fseek(cxt->fp, cxt->wpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -3;
    }

    /* Copy the file over from the old archive to the new one. */
    if(copy_file(cxt->fp, fp, sz))
        return -4;

    /* Add padding, as needed. */
    if((cxt->wpos = pad_file(cxt->fp, 2048)) < 0)
        return -5;

    ++cxt->copied_files;

    return 0;
}

static int copy_update(FILE *fp, uint32_t i, uint32_t sz, const char *fn,
                       void *d) {
    struct update_cxt *cxt = (struct update_cxt *)d;
    uint8_t buf[48];
    uint32_t wposp = cxt->wpos >> 11;

    /* Look if we're supposed to update this one. */
    if(!strcmp(cxt->fn, fn)) {
        if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            return -7;
        }

        if(add_files_to_gsl(cxt->fp, cxt->fpos, cxt->wpos, &cxt->path, 1,
                            &cxt->fpos, &cxt->wpos))
            return -6;

        return 0;
    }

    /* Copy the filename, filling in zero bytes as needed. */
    strncpy((char *)buf, fn, 32);

    /* Fill in the header data for the file. */
    if(endianness == GSL_BIG) {
        buf[32] = (uint8_t)(wposp >> 24);
        buf[33] = (uint8_t)(wposp >> 16);
        buf[34] = (uint8_t)(wposp >> 8);
        buf[35] = (uint8_t)(wposp);
        buf[36] = (uint8_t)(sz >> 24);
        buf[37] = (uint8_t)(sz >> 16);
        buf[38] = (uint8_t)(sz >> 8);
        buf[39] = (uint8_t)(sz);
    }
    else {
        buf[32] = (uint8_t)(wposp);
        buf[33] = (uint8_t)(wposp >> 8);
        buf[34] = (uint8_t)(wposp >> 16);
        buf[35] = (uint8_t)(wposp >> 24);
        buf[36] = (uint8_t)(sz);
        buf[37] = (uint8_t)(sz >> 8);
        buf[38] = (uint8_t)(sz >> 16);
        buf[39] = (uint8_t)(sz >> 24);
    }

    buf[40] = 0;
    buf[41] = 0;
    buf[42] = 0;
    buf[43] = 0;
    buf[44] = 0;
    buf[45] = 0;
    buf[46] = 0;
    buf[47] = 0;

    if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(buf, 1, 48, cxt->fp) != 48) {
        printf("Cannot write to file: %s\n", strerror(errno));
        return -2;
    }

    cxt->fpos += 48;

    if(fseek(cxt->fp, cxt->wpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -3;
    }

    /* Copy the file over from the old archive to the new one. */
    if(copy_file(cxt->fp, fp, sz))
        return -4;

    /* Add padding, as needed. */
    if((cxt->wpos = pad_file(cxt->fp, 2048)) < 0)
        return -5;

    return 0;
}

int create_gsl(const char *fn, const char *files[], uint32_t count) {
    FILE *ofp;
    int fd;
    char tmpfn[16];
    long fpos, wpos, hdrlen;

#ifndef _WIN32
    mode_t mask;
#endif

    /* If the user hasn't specified the endianness of the file he or she is
       creating, then assume big endian. */
    if(endianness == GSL_AUTO)
        endianness = GSL_BIG;

    /* Figure out the size of the header. Sega's files seem (to me) to have no
       rhyme or reason to how long the header is. I just round it up to the next
       multiple of 2048.  */
    hdrlen = ((count * 48) + 2048) & 0xFFFFF000;

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "gsltoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        printf("Cannot create temporary file: %s\n", strerror(errno));
        return -1;
    }

    if(!(ofp = fdopen(fd, "wb"))) {
        printf("Cannot open temporary file: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfn);
        return -2;
    }

    /* Make space for the file table. */
    fpos = 0;

    if(fseek(ofp, hdrlen, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -4;
    }

    /* Save where we'll write the first file and move back to the file table. */
    wpos = ftell(ofp);
    if(fseek(ofp, fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -5;
    }

    /* Add all the files to the archive. */
    if(add_files_to_gsl(ofp, fpos, wpos, files, count, NULL, NULL)) {
        fclose(ofp);
        unlink(tmpfn);
        return -6;
    }

    /* All the files are copied, so move the archive into its place. */
#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fileno(ofp), (~mask) & 0666);
#endif
    fclose(ofp);
    rename(tmpfn, fn);

    return 0;
}

int add_to_gsl(const char *fn, const char *files[], uint32_t count) {
    FILE *ofp;
    int fd;
    char tmpfn[16];
    int entries;
    long fpos, wpos, hdrlen;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Figure out how many files are already in the file... */
    if((entries = scan_gsl(fn, NULL, NULL)) < 0) {
        return -11;
    }

    /* Figure out the size of the header. Sega's files seem (to me) to have no
       rhyme or reason to how long the header is. I just round it up to the next
       multiple of 2048.  */
    hdrlen = (((count + entries) * 48) + 2048) & 0xFFFFF000;

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "gsltoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        printf("Cannot create temporary file: %s\n", strerror(errno));
        return -1;
    }

    if(!(ofp = fdopen(fd, "wb"))) {
        printf("Cannot open temporary file: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfn);
        return -2;
    }

    /* Make space for the file table. */
    if(fseek(ofp, hdrlen, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -3;
    }

    /* Copy the data from the existing file to the new one. */
    if(scan_gsl(fn, &copy_file_cb, ofp) < 0) {
        fclose(ofp);
        unlink(tmpfn);
        return -5;
    }

    wpos = ftell(ofp);
    fpos = entries * 48;

    if(fseek(ofp, fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -9;
    }

    /* Add all the new files to the archive. */
    if(add_files_to_gsl(ofp, fpos, wpos, files, count, NULL, NULL)) {
        fclose(ofp);
        unlink(tmpfn);
        return -10;
    }

    /* All the files are copied, so move the archive into its place. */
#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fileno(ofp), (~mask) & 0666);
#endif
    fclose(ofp);
    rename(tmpfn, fn);

    return 0;
}

int update_gsl(const char *fn, const char *file, const char *path) {
    int fd;
    char tmpfn[16];
    int entries;
    struct update_cxt cxt;
    long hdrlen;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Parse out all the entries for the context first. */
    memset(&cxt, 0, sizeof(cxt));
    cxt.fn = file;
    cxt.path = path;

    /* Figure out how many files are already in the file... */
    if((entries = scan_gsl(fn, NULL, NULL)) < 0) {
        return -11;
    }

    /* Figure out the size of the header. Sega's files seem (to me) to have no
       rhyme or reason to how long the header is. I just round it up to the next
       multiple of 2048.  */
    hdrlen = ((entries * 48) + 2048) & 0xFFFFF000;

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "gsltoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        printf("Cannot create temporary file: %s\n", strerror(errno));
        return -3;
    }

    if(!(cxt.fp = fdopen(fd, "wb"))) {
        printf("Cannot open temporary file: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfn);
        return -4;
    }

    if(fseek(cxt.fp, hdrlen, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -5;
    }

    /* Save where we'll write the first file and move back to the file table. */
    cxt.wpos = ftell(cxt.fp);
    if(fseek(cxt.fp, cxt.fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -6;
    }

    if(scan_gsl(fn, &copy_update, &cxt) < 0) {
        fclose(cxt.fp);
        unlink(tmpfn);
        return -7;
    }

    /* All the files are copied, so move the archive into its place. */
#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fileno(cxt.fp), (~mask) & 0666);
#endif
    fclose(cxt.fp);
    rename(tmpfn, fn);

    return 0;
}

int delete_from_gsl(const char *fn, const char *files[], uint32_t cnt) {
    int fd;
    char tmpfn[16];
    int entries;
    struct delete_cxt cxt;
    uint32_t i;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Parse out all the entries for the context first. */
    memset(&cxt, 0, sizeof(cxt));
    cxt.item_count = cnt;

    if(!(cxt.items = (const char **)malloc(sizeof(const char *) * cnt))) {
        printf("Cannot allocate memory: %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    for(i = 0; i < cnt; ++i) {
        cxt.items[i] = files[i];
    }

    /* Figure out how many files are already in the file... */
    if((entries = scan_gsl(fn, NULL, NULL)) < 0) {
        free((void *)cxt.items);
        return -11;
    }

    /* Figure out the size of the header. Sega's files seem (to me) to have no
       rhyme or reason to how long the header is. I just round it up to the next
       multiple of 2048.  */
    cxt.wpos = ((entries * 48) + 2048) & 0xFFFFF000;

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "gsltoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        printf("Cannot create temporary file: %s\n", strerror(errno));
        free((void *)cxt.items);
        return -3;
    }

    if(!(cxt.fp = fdopen(fd, "wb"))) {
        printf("Cannot open temporary file: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfn);
        free((void *)cxt.items);
        return -4;
    }

    /* Make space for the file table. */
    cxt.fpos = 0;

    if(fseek(cxt.fp, cxt.wpos, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        free((void *)cxt.items);
        return -5;
    }

    /* Save where we'll write the first file and move back to the file table. */
    if(fseek(cxt.fp, cxt.fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        free((void *)cxt.items);
        return -6;
    }

    if(scan_gsl(fn, &copy_filtered, &cxt) < 0) {
        fclose(cxt.fp);
        unlink(tmpfn);
        free((void *)cxt.items);
        return -7;
    }

    free((void *)cxt.items);

    /* All the files are copied, so move the archive into its place. */
#ifndef _WIN32
    mask = umask(0);
    umask(mask);
    fchmod(fileno(cxt.fp), (~mask) & 0666);
#endif
    fclose(cxt.fp);
    rename(tmpfn, fn);

    return 0;
}

int print_gsl_files(const char *fn) {
    return scan_gsl(fn, &print_file_info, NULL);
}

int extract_gsl(const char *fn) {
    return scan_gsl(fn, &extract_file, (void *)fn);
}

void gsl_set_endianness(int e) {
    endianness = e;
}
