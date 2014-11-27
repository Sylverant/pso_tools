/*
    Sylverant PSO Tools
    BML Tool
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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#include <inttypes.h>
#endif

#include "prs.h"

#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
#define LE32(x) (((x >> 24) & 0x00FF) | \
                 ((x >>  8) & 0xFF00) | \
                 ((x & 0xFF00) <<  8) | \
                 ((x & 0x00FF) << 24))
#else
#define LE32(x) (x)
#endif

typedef struct bml_entry {
    char filename[32];
    uint32_t csize;
    uint32_t unk;
    uint32_t usize;
    uint32_t pvm_csize;
    uint32_t pvm_usize;
    uint32_t padding[3];
} bml_entry_t;

struct update_cxt {
    FILE *fp;
    const char *fn;
    const char *path;
    long fpos;
    long wpos;
    int is_pvm;
};

static uint8_t xbuf[512];

#ifdef _WIN32
/* In windows_compat.c */
char *basename(char *input);
int my_rename(const char *old, const char *new);
#define rename my_rename
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

static FILE *open_bml(const char *fn, uint32_t *entries) {
    FILE *fp;
    uint8_t buf[16];

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        printf("Cannot open %s: %s\n", fn, strerror(errno));
        return NULL;
    }

    /* Make sure that it looks like a sane BML file. */
    if(fread(buf, 1, 12, fp) != 12) {
        printf("Error reading file %s: %s\n", fn, strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0 ||
       buf[8] != 0x50 || buf[9] != 0x01 || buf[10] != 0 || buf[11] != 0) {
        printf("%s is not an BML archive!\n", fn);
        fclose(fp);
        return NULL;
    }

    *entries = (buf[4]) | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);

    /* Seek to the first file. */
    if(fseek(fp, 64, SEEK_SET)) {
        printf("Error seeking file: %s\n", strerror(errno));
        fclose(fp);
        return NULL;
    }

    return fp;
}

static int scan_bml(const char *fn, int (*p)(FILE *, bml_entry_t *, uint32_t,
                                             uint32_t, uint32_t, void *),
                    void *userdata) {
    FILE *fp;
    int rv = 0;
    uint32_t entries, i, offset, poffset, eoffset;
    long next;
    bml_entry_t ent;

    /* Open up the file */
    if(!(fp = open_bml(fn, &entries)))
        return -1;

    /* Gonna guess that this will never be a problem, considering we shouldn't
       have anywhere near a 2GiB BML file. */
    rv = (int)entries;

    /* Figure out the length of the header based on the number of entries. */
    offset = (entries + 1) * 64;

    if((offset & 0x7FF))
        offset = (offset + 0x800) & 0xFFFFF800;

    /* Read in each file in the archive, writing each one out. */
    for(i = 0; i < entries; ++i) {
        /* Read in the header of the file. */
        if(fread(&ent, 1, sizeof(ent), fp) != sizeof(ent)) {
            printf("Error reading file %s: %s\n", fn, strerror(errno));
            rv = -2;
            goto out;
        }

        /* Swap the endianness, if needed. */
        ent.csize = LE32(ent.csize);
        ent.unk = LE32(ent.unk);
        ent.usize = LE32(ent.usize);
        ent.pvm_csize = LE32(ent.pvm_csize);
        ent.pvm_usize = LE32(ent.pvm_usize);

        poffset = offset + ent.csize;
        if((poffset & 0x1F))
            poffset = (poffset + 0x20) & 0xFFFFFFE0;

        eoffset = poffset;
        next = ftell(fp);

        /* Adjust the next offset if there's a PVM attached. */
        if(ent.pvm_csize) {
            eoffset = poffset + ent.pvm_csize;
            if((eoffset & 0x1F))
                eoffset = (eoffset + 0x20) & 0xFFFFFFE0;
        }

        if((rv = p(fp, &ent, i, offset, poffset, userdata)))
            goto out;

        /* Adjust things for the next iteration... */
        offset = eoffset;

        if(fseek(fp, next, SEEK_SET) < 0) {
            printf("Seek error: %s\n", strerror(errno));
            rv = -3;
            goto out;
        }
    }

out:
    fclose(fp);
    return rv;
}

static int print_file_info(FILE *fp, bml_entry_t *ent, uint32_t i,
                           uint32_t offset, uint32_t poffset, void *d) {
#ifndef _WIN32
    printf("File %4" PRIu32 " '%s'\n    compressed size: %" PRIu32 " "
           "uncompressed size: %" PRIu32 " Unknown: %#010" PRIx32 "\n    "
           "offset: %#010" PRIx32 "\n", i, ent->filename, ent->csize,
           ent->usize, ent->unk, offset);

    if(ent->pvm_csize)
        printf("    PVM size: %" PRIu32 " PVM uncompressed size: %" PRIu32
               "\n    PVM offset: %#010" PRIx32 "\n", ent->pvm_csize,
               ent->pvm_usize, poffset);
#else
    printf("File %4u '%s'\n    compressed size: %u uncompressed size: %u"
           " Unknown: %#010x\n    offset: %#010x\n", i, ent->filename,
           ent->csize, ent->usize, ent->unk, offset);

    if(ent->pvm_csize)
        printf("    PVM size: %u PVM uncompressed size: %u\n"
               "    PVM offset: %#010x\n", ent->pvm_csize, ent->pvm_usize,
               poffset);
#endif

    return 0;
}

static int extract_file(FILE *fp, bml_entry_t *ent, uint32_t i,
                        uint32_t offset, uint32_t poffset, void *d) {
    FILE *ofp;
    char fn[50];

    /* If we're only extracting one file, then make sure we have the right one
       before we extract it. */
    if(d && strcmp((const char *)d, ent->filename))
        return 0;

    sprintf(fn, "%s.prs", ent->filename);

    /* Open the output file. */
    if(!(ofp = fopen(fn, "wb"))) {
        printf("Cannot open file '%s' for write: %s\n", fn, strerror(errno));
        return -1;
    }

    if(fseek(fp, offset, SEEK_SET) < 0) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(ofp);
        return -2;
    }

    /* Copy the data out into its new file. */
    if(copy_file(ofp, fp, ent->csize)) {
        fclose(ofp);
        return -3;
    }

    /* We're done with this file, close it and check if we have a PVM to deal
       with still. */
    fclose(ofp);

    if(ent->pvm_csize) {
        sprintf(fn, "%s.pvm.prs", ent->filename);

        /* Open the output file. */
        if(!(ofp = fopen(fn, "wb"))) {
            printf("Cannot open file '%s' for write: %s\n", fn,
                   strerror(errno));
            return -4;
        }

        if(fseek(fp, poffset, SEEK_SET) < 0) {
            printf("Seek error: %s\n", strerror(errno));
            fclose(ofp);
            return -5;
        }

        /* Copy the data out into its new file. */
        if(copy_file(ofp, fp, ent->csize)) {
            fclose(ofp);
            return -6;
        }

        /* We're done with this file, close it. */
        fclose(ofp);
    }

    return 0;
}

static int read_and_dec(FILE *fp, uint32_t offset, uint32_t cs, uint32_t ds,
                        const char *fn) {
    FILE *ofp;
    uint8_t *comp, *decomp;
    int rv;

    if(fseek(fp, offset, SEEK_SET) < 0) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    /* Allocate both of the buffers we'll need. */
    if(!(comp = (uint8_t *)malloc(cs))) {
        printf("Cannot allocate memory: %s\n", strerror(errno));
        return -2;
    }

    if(!(decomp = (uint8_t *)malloc(ds))) {
        printf("Cannot allocate memory: %s\n", strerror(errno));
        free(comp);
        return -3;
    }

    /* Read into the compressed buffer. */
    if(fread(comp, 1, cs, fp) != cs) {
        printf("File read error: %s\n", strerror(errno));
        free(decomp);
        free(comp);
        return -4;
    }

    /* Decompress it. */
    if((rv = prs_decompress_buf2(comp, decomp, cs, ds)) != ds) {
        printf("Error decompressing file %s: ", fn);

        if(rv >= 0)
            printf("Size mismatch!\n");
        else
            printf("%s\n", strerror(-rv));

        free(decomp);
        free(comp);
        return -5;
    }

    /* Free the compressed buffer, since we're done with it. */
    free(comp);

    /* Open the output file. */
    if(!(ofp = fopen(fn, "wb"))) {
        printf("Cannot open file '%s' for write: %s\n", fn, strerror(errno));
        free(decomp);
        return -6;
    }

    /* Write it out. */
    if(fwrite(decomp, 1, ds, ofp) != ds) {
        printf("File write error '%s': %s\n", fn, strerror(errno));
        free(decomp);
        fclose(ofp);
        return -7;
    }

    /* We're done, so clean up. */
    fclose(ofp);
    free(decomp);

    return 0;
}

static int decompress_file(FILE *fp, bml_entry_t *ent, uint32_t i,
                           uint32_t offset, uint32_t poffset, void *d) {
    char fn[50];

    /* If we're only extracting one file, then make sure we have the right one
       before we extract it. */
    if(d && strcmp((const char *)d, ent->filename))
        return 0;

    if(read_and_dec(fp, offset, ent->csize, ent->usize, ent->filename))
        return -1;

    if(ent->pvm_csize) {
        sprintf(fn, "%s.pvm", ent->filename);

        if(read_and_dec(fp, poffset, ent->pvm_csize, ent->pvm_usize, fn))
            return -2;
    }

    return 0;
}

static uint8_t *read_and_cmp(const char *fn, uint32_t *cs, uint32_t *ds) {
    FILE *fp;
    uint8_t *comp, *decomp;
    int rv;
    long len;

    /* Open the file and figure out how long it is. */
    if(!(fp = fopen(fn, "rb"))) {
        printf("Cannot open '%s': %s\n", fn, strerror(errno));
        return NULL;
    }

    if(fseek(fp, 0, SEEK_END) < 0) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(fp);
        return NULL;
    }

    if((len = ftell(fp)) < 0) {
        printf("Cannot read file position: %s\n", strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(fseek(fp, 0, SEEK_SET) < 0) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(fp);
        return NULL;
    }

    /* Allocate both of the buffers we'll need. */
    if(!(decomp = (uint8_t *)malloc(len))) {
        printf("Cannot allocate memory: %s\n", strerror(errno));
        fclose(fp);
        return NULL;
    }

    /* Read into the compressed buffer. */
    if(fread(decomp, 1, len, fp) != len) {
        printf("File read error: %s\n", strerror(errno));
        free(decomp);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    /* Compress it. */
    if((rv = prs_compress(decomp, &comp, len)) < 0) {
        printf("Error compressing file %s: %s", fn, strerror(-rv));
        free(decomp);
        return NULL;
    }

    /* Clean up. */
    free(decomp);
    *cs = (uint32_t)rv;
    *ds = (uint32_t)len;

    return comp;
}

static int copy_update(FILE *fp, bml_entry_t *ent, uint32_t i, uint32_t offset,
                       uint32_t poffset, void *d) {
    struct update_cxt *cxt = (struct update_cxt *)d;
    uint32_t cs = ent->csize, pcs = ent->pvm_csize, ncs, nus;
    uint8_t *buf;

    /* Look if we're supposed to update this one. */
    if(!strcmp(cxt->fn, ent->filename)) {
        /* Read in the file we're replacing this one with, compressing it as we
           do so. */
        if(!(buf = read_and_cmp(cxt->path, &ncs, &nus)))
            return -10;

        /* Write the header out. */
        if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            return -11;
        }

        ent->unk = LE32(ent->unk);

        if(!cxt->is_pvm) {
            ent->csize = LE32(ncs);
            ent->usize = LE32(nus);
            ent->pvm_csize = LE32(ent->pvm_csize);
            ent->pvm_usize = LE32(ent->pvm_usize);
        }
        else {
            ent->csize = LE32(ent->csize);
            ent->usize = LE32(ent->usize);
            ent->pvm_csize = LE32(ncs);
            ent->pvm_usize = LE32(nus);
        }

        if(fwrite(ent, 1, 64, cxt->fp) != 64) {
            printf("Cannot write to file: %s\n", strerror(errno));
            return -12;
        }

        cxt->fpos += 64;

        if(fseek(cxt->fp, cxt->wpos, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            return -13;
        }

        if(!cxt->is_pvm) {
            /* Write out the compressed data. */
            if(fwrite(buf, 1, ncs, cxt->fp) != ncs) {
                printf("Write error: %s\n", strerror(errno));
                return -15;
            }
        }
        else {
            if(fseek(fp, offset, SEEK_SET)) {
                printf("Seek error: %s\n", strerror(errno));
                return -17;
            }

            if(copy_file(cxt->fp, fp, cs))
                return -18;
        }

        /* Add padding, as needed. */
        if((cxt->wpos = pad_file(cxt->fp, 32)) < 0)
            return -16;

        /* Deal with the PVM, if there is one. */
        if(pcs || cxt->is_pvm) {
            if(!cxt->is_pvm) {
                if(fseek(fp, poffset, SEEK_SET)) {
                    printf("Seek error: %s\n", strerror(errno));
                    return -17;
                }

                if(copy_file(cxt->fp, fp, pcs))
                    return -18;
            }
            else {
                if(fwrite(buf, 1, ncs, cxt->fp) != ncs) {
                    printf("Write error: %s\n", strerror(errno));
                    return -15;
                }
            }

            /* Add padding, as needed. */
            if((cxt->wpos = pad_file(cxt->fp, 32)) < 0)
                return -19;
        }

        free(buf);
        return 0;
    }

    /* Write the header back out. */
    if(fseek(cxt->fp, cxt->fpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -1;
    }

    /* Swap the endianness, if needed. */
    ent->csize = LE32(ent->csize);
    ent->unk = LE32(ent->unk);
    ent->usize = LE32(ent->usize);
    ent->pvm_csize = LE32(ent->pvm_csize);
    ent->pvm_usize = LE32(ent->pvm_usize);

    if(fwrite(ent, 1, 64, cxt->fp) != 64) {
        printf("Cannot write to file: %s\n", strerror(errno));
        return -2;
    }

    cxt->fpos += 64;

    if(fseek(cxt->fp, cxt->wpos, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -3;
    }

    if(fseek(fp, offset, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        return -4;
    }

    /* Copy the file over from the old archive to the new one. */
    if(copy_file(cxt->fp, fp, cs))
        return -5;

    /* Add padding, as needed. */
    if((cxt->wpos = pad_file(cxt->fp, 32)) < 0)
        return -6;

    /* Deal with the PVM, if there is one. */
    if(pcs) {
        if(fseek(fp, poffset, SEEK_SET)) {
            printf("Seek error: %s\n", strerror(errno));
            return -7;
        }

        if(copy_file(cxt->fp, fp, pcs))
            return -8;

        /* Add padding, as needed. */
        if((cxt->wpos = pad_file(cxt->fp, 32)) < 0)
            return -9;
    }

    return 0;
}

int update_bml(const char *fn, const char *file, const char *path, int pvm) {
    int fd;
    char tmpfn[16];
    uint32_t entries, hdrlen;
    struct update_cxt cxt;
    FILE *fp;
    uint8_t hdrbuf[64] = { 0 };

#ifndef _WIN32
    mode_t mask;
#endif

    /* Parse out all the entries for the context first. */
    memset(&cxt, 0, sizeof(cxt));
    cxt.fn = file;
    cxt.path = path;
    cxt.is_pvm = pvm;

    /* Figure out how many entries are in the existing file. */
    if(!(fp = open_bml(fn, &entries)))
        return -1;

    fclose(fp);

    /* Figure out the size of the header. */
    /* Figure out the length of the header based on the number of entries. */
    hdrlen = (entries + 1) * 64;

    if((hdrlen & 0x7FF))
        hdrlen = (hdrlen + 0x800) & 0xFFFFF800;

    /* Open up a temporary file for writing. */
    strcpy(tmpfn, "bmltoolXXXXXX");
    if((fd = mkstemp(tmpfn)) < 0) {
        printf("Cannot create temporary file: %s\n", strerror(errno));
        return -2;
    }

    if(!(cxt.fp = fdopen(fd, "wb"))) {
        printf("Cannot open temporary file: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfn);
        return -3;
    }

    if(fseek(cxt.fp, hdrlen, SEEK_SET)) {
        printf("Cannot create blank file table: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -4;
    }

    /* Save where we'll write the first file and move back to the file table. */
    cxt.wpos = ftell(cxt.fp);
    if(fseek(cxt.fp, 0, SEEK_SET)) {
        printf("Seek error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -5;
    }

    /* Write out the first header entry. */
    hdrbuf[4] = (uint8_t)(entries);
    hdrbuf[5] = (uint8_t)(entries >> 8);
    hdrbuf[6] = (uint8_t)(entries >> 16);
    hdrbuf[7] = (uint8_t)(entries >> 24);
    hdrbuf[8] = 0x50;
    hdrbuf[9] = 0x01;

    if(fwrite(hdrbuf, 1, 64, cxt.fp) != 64) {
        printf("Write error: %s\n", strerror(errno));
        fclose(cxt.fp);
        unlink(tmpfn);
        return -6;
    }

    cxt.fpos = 64;

    if(scan_bml(fn, &copy_update, &cxt) < 0) {
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

/* Print information about this program to stdout. */
static void print_program_info(void) {
#if defined(VERSION)
    printf("Sylverant BML Tool version %s\n", VERSION);
#elif defined(SVN_REVISION)
    printf("Sylverant BML Tool SVN revision: %s\n", SVN_REVISION);
#else
    printf("Sylverant BML Tool\n");
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
           "To list the files in an archive:\n"
           "    %s -t bml_archive\n"
           "To extract all files from an archive:\n"
           "    %s -x bml_archive\n"
           "To extract and decompress all files from an archive:\n"
           "    %s -xd bml_archive\n"
           "To extract a single file from an archive:\n"
           "    %s -xs bml_archive file_in_archive\n"
           "To extract and decompress a single file from an archive:\n"
           "    %s -xsd bml_archive file_in_archive\n"
           "To update a file in an archive (or replace it with another file):\n"
           "    %s -u bml_archive file_in_archive filename\n"
           "To update a PVM file (attached to a file in the archive):\n"
           "    %s -up bml_archive parent_file_in_archive filename\n"
           "To print this help message:\n"
           "    %s --help\n"
           "To print version information:\n"
           "    %s --version\n\n"
           "Note that when extracting a single file, if there is an attached\n"
           "PVM file to the specified file, it will also be extracted.\n\n"
           "Also, for updating a file, you must provide the uncompressed file\n"
           "to be added. This program will compress it as appropriate.\n",
           bin, bin, bin, bin, bin, bin, bin, bin, bin);
}

/* Parse any command-line arguments passed in. */
static void parse_command_line(int argc, const char *argv[]) {
    if(argc < 2) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    if(!strcmp(argv[1], "--version")) {
        print_program_info();
    }
    else if(!strcmp(argv[1], "--help")) {
        print_help(argv[0]);
    }
    else if(!strcmp(argv[1], "-t")) {
        if(argc != 3) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(scan_bml(argv[2], &print_file_info, NULL) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[1], "-x")) {
        if(argc != 3) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(scan_bml(argv[2], &extract_file, NULL) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[1], "-xd")) {
        if(argc != 3) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(scan_bml(argv[2], &decompress_file, NULL) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[1], "-xs")) {
        if(argc != 4) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(scan_bml(argv[2], &extract_file, (void *)argv[3]) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[1], "-xsd")) {
        if(argc != 4) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(scan_bml(argv[2], &decompress_file, (void *)argv[3]) < 0)
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[1], "-u")) {
        if(argc != 5) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(update_bml(argv[2], argv[3], argv[4], 0))
            exit(EXIT_FAILURE);
    }
    else if(!strcmp(argv[1], "-up")) {
        if(argc != 5) {
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }

        if(update_bml(argv[2], argv[3], argv[4], 1))
            exit(EXIT_FAILURE);
    }
    else {
        printf("Illegal command line argument: %s\n", argv[1]);
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
