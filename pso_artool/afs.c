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

/* This code extracts AFS archives and performs various other tasks on AFS
   files. Note that the AFS archive format doesn't actually save the names of
   the files stored in it, so there's no way to know what the names of the files
   should be on extraction (so we punt by just naming them the name of the input
   file, with an extension for the index within the file). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#include <inttypes.h>
#endif

static uint8_t xbuf[512];

struct delete_cxt {
    FILE *fp;
    uint32_t item_count;
    uint32_t *items;
    long fpos;
    long wpos;
    uint32_t copied_files;
};

struct update_cxt {
    FILE *fp;
    uint32_t item;
    const char *fn;
    long fpos;
    long wpos;
};

static int digits(uint32_t n) {
    int r = 1;
    while(n /= 10) ++r;
    return r;
}

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

static FILE *open_afs(const char *fn, uint32_t *entries) {
    FILE *fp;
    uint8_t buf[4];

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        printf("Cannot open %s: %s\n", fn, strerror(errno));
        return NULL;
    }

    /* Make sure that it looks like a sane AFS file. */
    if(fread(buf, 1, 4, fp) != 4) {
        printf("Error reading file %s: %s\n", fn, strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(buf[0] != 0x41 || buf[1] != 0x46 || buf[2] != 0x53 || buf[3] != 0x00) {
        printf("%s is not an AFS archive!\n", fn);
        fclose(fp);
        return NULL;
    }

    /* Read the number of entries. */
    if(fread(buf, 1, 4, fp) != 4) {
        printf("Error reading file %s: %s\n", fn, strerror(errno));
        fclose(fp);
        return NULL;
    }

    *entries = (buf[0]) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    return fp;
}

static int scan_afs(const char *fn, int (*p)(FILE *fp, uint32_t i, uint32_t cnt,
                                             uint32_t sz, void *d),
                    void *userdata) {
    FILE *fp;
    uint8_t buf[4];
    int rv = 0;
    uint32_t entries, offset, size, i;
    long next;

    /* Open up the file */
    if(!(fp = open_afs(fn, &entries)))
        return -1;

    /* Since AFS files should have a 16-bit entry count (I think), this should
       work fine. */
    rv = (int)entries;

    /* Read in each file in the archive, writing each one out. */
    for(i = 0; i < entries; ++i) {
        /* Figure out where the next file starts in the archive. */
        if(fread(buf, 1, 4, fp) != 4) {
            printf("Error reading file %s: %s\n", fn, strerror(errno));
            rv = -2;
            goto out;
        }

        offset = (buf[0]) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

        /* Figure out the size of the next file. */
        if(fread(buf, 1, 4, fp) != 4) {
            printf("Error reading file %s: %s\n", fn, strerror(errno));
            rv = -3;
            goto out;
        }

        size = (buf[0]) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
        next = ftell(fp);

        if(fseek(fp, (long)offset, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            rv = -4;
            goto out;
        }

        /* Call the callback function. */
        if(p(fp, i, entries, size, userdata)) {
            rv = -5;
            goto out;
        }

        /* Move back to the file table to go onto the next file. */
        if(fseek(fp, next, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            rv = -6;
            goto out;
        }
    }

out:
    fclose(fp);
    return rv;
}

static int add_files_to_afs(FILE *ofp, long fpos, long wpos,
                            const char *files[], uint32_t count,
                            long *rfpos, long *rwpos) {
    uint32_t i;
    FILE *ifp;
    uint32_t size;
    uint8_t buf[8];

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

        /* Write the file's information into the file table. */
        buf[0] = (uint8_t)(wpos);
        buf[1] = (uint8_t)(wpos >> 8);
        buf[2] = (uint8_t)(wpos >> 16);
        buf[3] = (uint8_t)(wpos >> 24);
        buf[4] = (uint8_t)(size);
        buf[5] = (uint8_t)(size >> 8);
        buf[6] = (uint8_t)(size >> 16);
        buf[7] = (uint8_t)(size >> 24);

        if(fwrite(buf, 1, 8, ofp) != 8) {
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

static int print_file_info(FILE *fp, uint32_t i, uint32_t cnt, uint32_t sz,
                           void *d) {
    int dg = digits(cnt);
    uint32_t offset = (uint32_t)ftell(fp);

#ifndef _WIN32
    printf("File %*" PRIu32 " @ offset %#010" PRIx32 " size: %" PRIu32 "\n",
           dg, i, offset, sz);
#else
    printf("File %*d @ offset %#010x size: %d\n", dg, i, offset, sz);
#endif

    return 0;
}

static int extract_file(FILE *fp, uint32_t i, uint32_t cnt, uint32_t sz,
                        void *d) {
    int dg = digits(cnt);
    const char *fn = (const char *)d;
    size_t len = strlen(fn);
    FILE *ofp;
#ifndef _WIN32
    char ofn[len + 12];
#else
    char ofn[256];
#endif

    /* Open the output file. */
#ifndef _WIN32
    snprintf(ofn, len + 12, "%s.%0*" PRIu32, fn, dg, i);
#else
    sprintf_s(ofn, 256, "%s.%0*d", fn, dg, i);
#endif

    if(!(ofp = fopen(ofn, "wb"))) {
        printf("Cannot open file %s for write: %s\n", ofn, strerror(errno));
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

static int copy_file_cb(FILE *fp, uint32_t i, uint32_t cnt, uint32_t sz,
                        void *d) {
    FILE *ofp = (FILE *)d;
    long fpos = (uint32_t)(i << 3) + 8;
    uint32_t wpos = (uint32_t)ftell(ofp);
    uint8_t buf[8];

    if(!i)
        wpos = 0x80000;

    /* Fill in the header data for the file. */
    buf[0] = (uint8_t)(wpos);
    buf[1] = (uint8_t)(wpos >> 8);
    buf[2] = (uint8_t)(wpos >> 16);
    buf[3] = (uint8_t)(wpos >> 24);
    buf[4] = (uint8_t)(sz);
    buf[5] = (uint8_t)(sz >> 8);
    buf[6] = (uint8_t)(sz >> 16);
    buf[7] = (uint8_t)(sz >> 24);

    if(fseek(ofp, fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(buf, 1, 8, ofp) != 8) {
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

static int copy_filtered(FILE *fp, uint32_t i, uint32_t cnt, uint32_t sz,
                         void *d) {
    struct delete_cxt *cxt = (struct delete_cxt *)d;
    uint8_t buf[8];
    uint32_t j;

    /* Look if we're supposed to leave this one off. */
    for(j = 0; j < cxt->item_count; ++j) {
        if(cxt->items[j] == i)
            return 0;
    }

    /* Fill in the header data for the file. */
    buf[0] = (uint8_t)(cxt->wpos);
    buf[1] = (uint8_t)(cxt->wpos >> 8);
    buf[2] = (uint8_t)(cxt->wpos >> 16);
    buf[3] = (uint8_t)(cxt->wpos >> 24);
    buf[4] = (uint8_t)(sz);
    buf[5] = (uint8_t)(sz >> 8);
    buf[6] = (uint8_t)(sz >> 16);
    buf[7] = (uint8_t)(sz >> 24);

    if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(buf, 1, 8, cxt->fp) != 8) {
        printf("Cannot write to file: %s\n", strerror(errno));
        return -2;
    }

    cxt->fpos += 8;

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

static int copy_update(FILE *fp, uint32_t i, uint32_t cnt, uint32_t sz,
                       void *d) {
    struct update_cxt *cxt = (struct update_cxt *)d;
    uint8_t buf[8];

    /* Look if we're supposed to update this one. */
    if(cxt->item == i) {
        if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            return -7;
        }

        if(add_files_to_afs(cxt->fp, cxt->fpos, cxt->wpos, &cxt->fn, 1,
                            &cxt->fpos, &cxt->wpos))
            return -6;

        return 0;
    }

    /* Fill in the header data for the file. */
    buf[0] = (uint8_t)(cxt->wpos);
    buf[1] = (uint8_t)(cxt->wpos >> 8);
    buf[2] = (uint8_t)(cxt->wpos >> 16);
    buf[3] = (uint8_t)(cxt->wpos >> 24);
    buf[4] = (uint8_t)(sz);
    buf[5] = (uint8_t)(sz >> 8);
    buf[6] = (uint8_t)(sz >> 16);
    buf[7] = (uint8_t)(sz >> 24);

    if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    if(fwrite(buf, 1, 8, cxt->fp) != 8) {
        printf("Cannot write to file: %s\n", strerror(errno));
        return -2;
    }

    cxt->fpos += 8;

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

int create_afs(const char *fn, const char *files[], uint32_t count) {
    FILE *ofp;
    int fd;
    char tmpfn[16];
    uint8_t buf[8];
    long fpos, wpos;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Make sure we won't overflow the file table. */
    if(count > 65535) {
        printf("Cowardly refusing to make an archive with > 65535 files.\n");
        return -14;
    }

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "afstoolXXXXXX");
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

    /* Write the header to the file. */
    buf[0] = 0x41; /* 'A' */
    buf[1] = 0x46; /* 'F' */
    buf[2] = 0x53; /* 'S' */
    buf[3] = 0x00; /* '\0' */
    buf[4] = (uint8_t)(count);
    buf[5] = (uint8_t)(count >> 8);
    buf[6] = (uint8_t)(count >> 16);
    buf[7] = (uint8_t)(count >> 24);

    if(fwrite(buf, 1, 8, ofp) != 8) {
        printf("Cannot write to temporary file: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -3;
    }

    /* Make space for the file table. */
    fpos = ftell(ofp);

    if(fseek(ofp, 0x80000, SEEK_SET)) {
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
    if(add_files_to_afs(ofp, fpos, wpos, files, count, NULL, NULL)) {
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

int add_to_afs(const char *fn, const char *files[], uint32_t count) {
    FILE *ofp;
    int fd;
    char tmpfn[16];
    uint8_t buf[8];
    int entries;
    long fpos, wpos;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "afstoolXXXXXX");
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
    fpos = 8;

    if(fseek(ofp, 0x80000, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -3;
    }

    /* Save where we'll write the first file and move back to the file table. */
    wpos = ftell(ofp);
    if(fseek(ofp, fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -4;
    }

    if((entries = scan_afs(fn, &copy_file_cb, ofp)) < 0) {
        fclose(ofp);
        unlink(tmpfn);
        return -5;
    }

    /* Kind of a bit late to be checking this, but oh well. */
    if(entries + count > 65535) {
        printf("Cowardly refusing to make an archive with > 65535 files.\n");
        fclose(ofp);
        unlink(tmpfn);
        return -6;
    }

    wpos = ftell(ofp);
    fpos = (entries << 3) + 8;

    /* Write the header to the file. */
    buf[0] = 0x41; /* 'A' */
    buf[1] = 0x46; /* 'F' */
    buf[2] = 0x53; /* 'S' */
    buf[3] = 0x00; /* '\0' */
    buf[4] = (uint8_t)((entries + count));
    buf[5] = (uint8_t)((entries + count) >> 8);
    buf[6] = (uint8_t)((entries + count) >> 16);
    buf[7] = (uint8_t)((entries + count) >> 24);

    if(fseek(ofp, 0, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -7;
    }

    if(fwrite(buf, 1, 8, ofp) != 8) {
        printf("Cannot write to file: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -8;
    }

    if(fseek(ofp, fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(ofp);
        unlink(tmpfn);
        return -9;
    }

    /* Add all the new files to the archive. */
    if(add_files_to_afs(ofp, fpos, wpos, files, count, NULL, NULL)) {
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

int update_afs(const char *fn, const char *fno, const char *file) {
    int fd;
    char tmpfn[16];
    uint8_t buf[8];
    int entries;
    struct update_cxt cxt;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Parse out all the entries for the context first. */
    memset(&cxt, 0, sizeof(cxt));

    errno = 0;
    cxt.item = (uint32_t)strtoul(fno, NULL, 0);
    cxt.fn = file;
    if(errno) {
        printf("%s is not a valid file number.\n", fno);
        return -2;
    }

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "afstoolXXXXXX");
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

    /* Make space for the file table. */
    cxt.fpos = 8;

    if(fseek(cxt.fp, 0x80000, SEEK_SET)) {
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

    if((entries = scan_afs(fn, &copy_update, &cxt)) < 0) {
        fclose(cxt.fp);
        unlink(tmpfn);
        return -7;
    }

    if(cxt.item >= entries) {
#ifndef _WIN32
        printf("Item out of range: %" PRIu32 "\n", cxt.item);
#else
        printf("Item out of range: %d\n", cxt.item);
#endif

        fclose(cxt.fp);
        unlink(tmpfn);
        return -8;
    }

    /* Write the header to the file. */
    buf[0] = 0x41; /* 'A' */
    buf[1] = 0x46; /* 'F' */
    buf[2] = 0x53; /* 'S' */
    buf[3] = 0x00; /* '\0' */
    buf[4] = (uint8_t)(entries);
    buf[5] = (uint8_t)(entries >> 8);
    buf[6] = (uint8_t)(entries >> 16);
    buf[7] = (uint8_t)(entries >> 24);

    if(fseek(cxt.fp, 0, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -7;
    }

    if(fwrite(buf, 1, 8, cxt.fp) != 8) {
        printf("Cannot write to file: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -8;
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

int delete_from_afs(const char *fn, const char *files[], uint32_t cnt) {
    int fd;
    char tmpfn[16];
    uint8_t buf[8];
    int entries;
    struct delete_cxt cxt;
    uint32_t i;

#ifndef _WIN32
    mode_t mask;
#endif

    /* Parse out all the entries for the context first. */
    memset(&cxt, 0, sizeof(cxt));
    cxt.item_count = cnt;

    if(!(cxt.items = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        printf("Cannot allocate memory: %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    for(i = 0; i < cnt; ++i) {
        cxt.items[i] = (uint32_t)strtoul(files[i], NULL, 0);
        if(errno) {
            printf("%s is not a valid file number.\n", files[i]);
            free(cxt.items);
            return -2;
        }
    }

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "afstoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        printf("Cannot create temporary file: %s\n", strerror(errno));
        free(cxt.items);
        return -3;
    }

    if(!(cxt.fp = fdopen(fd, "wb"))) {
        printf("Cannot open temporary file: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfn);
        free(cxt.items);
        return -4;
    }

    /* Make space for the file table. */
    cxt.fpos = 8;

    if(fseek(cxt.fp, 0x80000, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        free(cxt.items);
        return -5;
    }

    /* Save where we'll write the first file and move back to the file table. */
    cxt.wpos = ftell(cxt.fp);
    if(fseek(cxt.fp, cxt.fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        free(cxt.items);
        return -6;
    }

    if((entries = scan_afs(fn, &copy_filtered, &cxt)) < 0) {
        fclose(cxt.fp);
        unlink(tmpfn);
        free(cxt.items);
        return -7;
    }

    for(i = 0; i < cnt; ++i) {
        if(cxt.items[i] >= entries) {
#ifndef _WIN32
            printf("Item out of range: %" PRIu32 "\n", cxt.items[i]);
#else
            printf("Item out of range: %d\n", cxt.items[i]);
#endif

            fclose(cxt.fp);
            unlink(tmpfn);
            free(cxt.items);
            return -8;
        }
    }

    free(cxt.items);

    /* Write the header to the file. */
    buf[0] = 0x41; /* 'A' */
    buf[1] = 0x46; /* 'F' */
    buf[2] = 0x53; /* 'S' */
    buf[3] = 0x00; /* '\0' */
    buf[4] = (uint8_t)(cxt.copied_files);
    buf[5] = (uint8_t)(cxt.copied_files >> 8);
    buf[6] = (uint8_t)(cxt.copied_files >> 16);
    buf[7] = (uint8_t)(cxt.copied_files >> 24);

    if(fseek(cxt.fp, 0, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -7;
    }

    if(fwrite(buf, 1, 8, cxt.fp) != 8) {
        printf("Cannot write to file: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -8;
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

int print_afs_files(const char *fn) {
    return scan_afs(fn, &print_file_info, NULL);
}

int extract_afs(const char *fn) {
    return scan_afs(fn, &extract_file, (void *)fn);
}
