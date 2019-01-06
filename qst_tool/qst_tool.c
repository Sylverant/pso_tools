/*
    Sylverant Quest Tool
    Copyright (C) 2012, 2015, 2019 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

/* I really hate you Visual Studio... There is no reason to warn about
   standard C functions... */
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

/* This stuff copied from the packets.h file from Sylverant... */
#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
#define LE16(x) (((x >> 8) & 0xFF) | ((x & 0xFF) << 8))
#define LE32(x) (((x >> 24) & 0x00FF) | \
                 ((x >>  8) & 0xFF00) | \
                 ((x & 0xFF00) <<  8) | \
                 ((x & 0x00FF) << 24))
#else
#define LE16(x) x
#define LE32(x) x
#endif

#ifdef PACKED
#undef PACKED
#endif

#if defined(_MSC_VER)
#define PACKED
#pragma pack(push, 1)
#elif defined (__GNUC__)
#define PACKED __attribute__((packed))
#else
#error Need to define how to pack structures for your compiler!
#endif

typedef struct dc_pkt_hdr {
    uint8_t pkt_type;
    uint8_t flags;
    uint16_t pkt_len;
} PACKED dc_pkt_hdr_t;

typedef struct pc_pkt_hdr {
    uint16_t pkt_len;
    uint8_t pkt_type;
    uint8_t flags;
} PACKED pc_pkt_hdr_t;

typedef struct bb_pkt_hdr {
    uint16_t pkt_len;
    uint16_t pkt_type;
    uint32_t flags;
} PACKED bb_pkt_hdr_t;

typedef struct dc_qst_hdr_s {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint8_t unused1[3];
    char filename[16];
    uint8_t unused2;
    uint32_t length;
} PACKED dc_qst_hdr;

typedef struct pc_qst_hdr_s {
    pc_pkt_hdr_t hdr;
    char name[32];
    uint16_t unused;
    uint16_t flags;
    char filename[16];
    uint32_t length;
} PACKED pc_qst_hdr;

typedef struct gc_qst_hdr_s {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint16_t unused;
    uint16_t flags;
    char filename[16];
    uint32_t length;
} PACKED gc_qst_hdr;

typedef struct bb_qst_hdr_s {
    bb_pkt_hdr_t hdr;
    char unused1[32];
    uint16_t unused2;
    uint16_t flags;
    char filename[16];
    uint32_t length;
    char name[24];
} PACKED bb_qst_hdr;

typedef struct dc_quest_chunk {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char filename[16];
    uint8_t data[1024];
    uint32_t length;
} PACKED qst_chunk;

typedef struct bb_quest_chunk {
    bb_pkt_hdr_t hdr;
    char filename[16];
    uint8_t data[1024];
    uint32_t length;
} PACKED bb_qst_chunk;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#undef PACKED

#define QUEST_CHUNK_TYPE                0x0013
#define QUEST_FILE_TYPE                 0x0044
#define DL_QUEST_FILE_TYPE              0x00A6
#define DL_QUEST_CHUNK_TYPE             0x00A7

#define QUEST_VER_DC                    0x00000001
#define QUEST_VER_PC                    0x00000002
#define QUEST_VER_GC                    0x00000004
#define QUEST_VER_BB                    0x00000008

#define QUEST_TYPE_ONLINE               0x00000100
#define QUEST_TYPE_DOWNLOAD             0x00000200

#define QUEST_TYPE_INVALID              0xFFFFFFFF

static uint8_t buf[4096];

static void usage(const char *argv[]) {
    printf("Usage:\n");
    printf("To extract a .qst file:\n    %s -x <file.qst>\n", argv[0]);
    printf("To merge a .bin/.dat to a .qst:\n    %s -m <type> <file.bin> "
           "<file.dat> [file.bin.hdr] [file.dat.hdr]\n", argv[0]);
    printf("To merge a .bin/.dat/.pvr to a .qst:\n    %s -mp <type> <file.bin> "
           "<file.dat> <file.pvr> [file.bin.hdr] [file.dat.hdr] "
           "[file.pvr.hdr]\n", argv[0]);
    printf("\n");
    printf("For merging, the available types are:\n");
    printf("    dc - Dreamcast (online)\n"
           "    pc - PSO for PC (online)\n"
           "    gc - PSO for Gamecube (online)\n"
           "    bb - Blue Burst (online)\n"
           "    dcdl - Dreamcast (download)\n"
           "    pcdl - PSO for PC (download)\n"
           "    gcdl - PSO for Gamecube (download)\n");
}

static uint32_t detect_qst_type(void) {
    uint32_t rv = 0;
    dc_qst_hdr *hdr = (dc_qst_hdr *)buf;

    if(buf[0] == QUEST_FILE_TYPE && buf[2] == 0x3C) {
        if(hdr->filename[0] == 0)
            /* Sure sign that we're on GC... */
            rv = QUEST_VER_GC | QUEST_TYPE_ONLINE;
        else
            /* Assume we're on DC for now, I guess... */
            rv = QUEST_VER_DC | QUEST_TYPE_ONLINE;
    }
    else if(buf[0] == 0x3C && buf[2] == QUEST_FILE_TYPE) {
        /* PC */
        rv = QUEST_VER_PC | QUEST_TYPE_ONLINE;
    }
    else if(buf[0] == DL_QUEST_FILE_TYPE && buf[2] == 0x3C) {
        if(hdr->filename[0] == 0)
        /* Sure sign that we're on GC... */
            rv = QUEST_VER_GC | QUEST_TYPE_DOWNLOAD;
        else
        /* Assume we're on DC for now, I guess... */
            rv = QUEST_VER_DC | QUEST_TYPE_DOWNLOAD;
    }
    else if(buf[0] == 0x3C && buf[2] == DL_QUEST_FILE_TYPE) {
        /* PC */
        rv = QUEST_VER_PC | QUEST_TYPE_DOWNLOAD;
    }
    else if(buf[0] == 0x58 && buf[2] == QUEST_FILE_TYPE) {
        /* BB -- there is no bb download quest type */
        rv = QUEST_VER_BB | QUEST_TYPE_ONLINE;
    }
    else {
        return QUEST_TYPE_INVALID;
    }

    return rv;
}

int process_hdr_file(uint32_t qst_type) {
    FILE *fp;
    dc_qst_hdr *dchdr = (dc_qst_hdr *)buf;
    pc_qst_hdr *pchdr = (pc_qst_hdr *)buf;
    bb_qst_hdr *bbhdr = (bb_qst_hdr *)buf;
    char fn[32];

    /* Open the header file */
    switch(qst_type & 0x000000FF) {
        case QUEST_VER_DC:
            if(buf[0] != DL_QUEST_FILE_TYPE && buf[0] != QUEST_FILE_TYPE)
                return -2;

            strncpy(fn, dchdr->filename, 16);
            fn[16] = 0;
            unlink(fn);
            strcat(fn, ".hdr");
            fp = fopen(fn, "wb");
            break;

        case QUEST_VER_PC:
            if(buf[2] != DL_QUEST_FILE_TYPE && buf[0] != QUEST_FILE_TYPE)
                return -2;

            goto pc_gc_common;

        case QUEST_VER_GC:
            if(buf[0] != DL_QUEST_FILE_TYPE && buf[0] != QUEST_FILE_TYPE)
                return -2;

pc_gc_common:
            strncpy(fn, pchdr->filename, 16);
            fn[16] = 0;
            unlink(fn);
            strcat(fn, ".hdr");
            fp = fopen(fn, "wb");
            break;

        case QUEST_VER_BB:
            if(buf[2] != QUEST_FILE_TYPE)
                return -2;

            strncpy(fn, bbhdr->filename, 16);
            fn[16] = 0;
            unlink(fn);
            strcat(fn, ".hdr");
            fp = fopen(fn, "wb");
            break;

        default:
            fprintf(stderr, "Unknown quest version: %d\n", qst_type & 0xFF);
            return -1;
    }

    if(!fp) {
        perror("Cannot open header file for writing");
        return -1;
    }

    /* Write the file out and clean up */
    if((qst_type & 0x000000FF) != QUEST_VER_BB) {
        if(fwrite(buf, 1, sizeof(dc_qst_hdr), fp) != sizeof(dc_qst_hdr)) {
            perror("fwrite");
            return -1;
        }
    }
    else {
        if(fwrite(buf, 1, sizeof(bb_qst_hdr), fp) != sizeof(bb_qst_hdr)) {
            perror("fwrite");
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int convert_dc_qst(FILE *qst) {
    FILE *wfp;
    qst_chunk *chunk = (qst_chunk *)buf;
    char fn[17];
    uint32_t clen;

    while(fread(buf, 1, 4, qst) == 4) {
        /* Check the header */
        if((chunk->hdr.dc.pkt_type != QUEST_CHUNK_TYPE &&
            chunk->hdr.dc.pkt_type != DL_QUEST_CHUNK_TYPE) ||
           chunk->hdr.dc.pkt_len != LE16(0x0418)) {
            fprintf(stderr, "Unknown or damaged chunk at offset %ld\n",
                    ftell(qst) - 4);
            return -1;
        }

        /* Read the rest of the chunk in */
        if(fread(buf + 4, 1, 0x0414, qst) != 0x0414) {
            perror("fread");
            return -1;
        }

        /* Grab our vital information and verify it... */
        strncpy(fn, chunk->filename, 16);
        fn[16] = 0;
        clen = LE32(chunk->length);

        if(clen > 1024) {
            fprintf(stderr, "Unknown or damaged chunk at offset %ld\n",
                    ftell(qst) - 4);
            return -1;
        }

        printf("%s chunk %d (%d bytes)\n", fn, chunk->hdr.dc.flags, (int)clen);

        /* Open the file we need to write to, and write the chunk to it. */
        if(!(wfp = fopen(fn, "ab"))) {
            perror("fopen");
            return -1;
        }

        if(fwrite(chunk->data, 1, clen, wfp) != clen) {
            perror("fwrite");
            fclose(wfp);
            return -1;
        }

        fclose(wfp);
    }

    if(feof(qst))
        return 0;

    perror("fread");
    return -1;
}

static int convert_pc_qst(FILE *qst) {
    FILE *wfp;
    qst_chunk *chunk = (qst_chunk *)buf;
    char fn[17];
    uint32_t clen;

    while(fread(buf, 1, 4, qst) == 4) {
        /* Check the header */
        if((chunk->hdr.pc.pkt_type != QUEST_CHUNK_TYPE &&
            chunk->hdr.pc.pkt_type != DL_QUEST_CHUNK_TYPE) ||
           chunk->hdr.pc.pkt_len != LE16(0x0418)) {
            fprintf(stderr, "Unknown or damaged chunk at offset %ld\n",
                    ftell(qst) - 4);
            return -1;
        }

        /* Read the rest of the chunk in */
        if(fread(buf + 4, 1, 0x0414, qst) != 0x0414) {
            perror("fread");
            return -1;
        }

        /* Grab our vital information and verify it... */
        strncpy(fn, chunk->filename, 16);
        fn[16] = 0;
        clen = LE32(chunk->length);

        if(clen > 1024) {
            fprintf(stderr, "Unknown or damaged chunk at offset %ld\n",
                    ftell(qst) - 4);
            return -1;
        }

        printf("%s chunk %d (%d bytes)\n", fn, chunk->hdr.pc.flags, (int)clen);

        /* Open the file we need to write to, and write the chunk to it. */
        if(!(wfp = fopen(fn, "ab"))) {
            perror("fopen");
            return -1;
        }

        if(fwrite(chunk->data, 1, clen, wfp) != clen) {
            perror("fwrite");
            fclose(wfp);
            return -1;
        }

        fclose(wfp);
    }

    if(feof(qst))
        return 0;

    perror("fread");
    return -1;
}

static int convert_bb_qst(FILE *qst) {
    FILE *wfp;
    bb_qst_chunk *chunk = (bb_qst_chunk *)buf;
    char fn[17];
    uint32_t clen;

    while(fread(buf, 1, 8, qst) == 8) {
        /* Check the header */
        if(chunk->hdr.pkt_type != LE16(QUEST_CHUNK_TYPE) ||
           chunk->hdr.pkt_len != LE16(0x041C)) {
            fprintf(stderr, "Unknown or damaged chunk at offset %ld\n",
                    ftell(qst) - 8);
            return -1;
        }

        /* Read the rest of the chunk in */
        if(fread(buf + 8, 1, 0x0414, qst) != 0x0414) {
            perror("fread");
            return -1;
        }

        /* Grab our vital information and verify it... */
        strncpy(fn, chunk->filename, 16);
        fn[16] = 0;
        clen = LE32(chunk->length);

        if(clen > 1024) {
            fprintf(stderr, "Unknown or damaged chunk at offset %ld\n",
                    ftell(qst) - 4);
            return -1;
        }

        printf("%s chunk %d (%d bytes)\n", fn, LE32(chunk->hdr.flags),
               (int)clen);

        /* Open the file we need to write to, and write the chunk to it. */
        if(!(wfp = fopen(fn, "ab"))) {
            perror("fopen");
            return -1;
        }

        if(fwrite(chunk->data, 1, clen, wfp) != clen) {
            perror("fwrite");
            fclose(wfp);
            return -1;
        }

        fclose(wfp);

        /* Sigh... */
        fseek(qst, 4, SEEK_CUR);
    }

    if(feof(qst))
        return 0;

    perror("fread");
    return -1;
}

static int qst_to_bindat(const char *fn) {
    FILE *fp;
    uint32_t qst_type;
    int rv = 0, morehdr = 0;
    int hdr_size = sizeof(dc_qst_hdr);

    if(!(fp = fopen(fn, "rb"))) {
        perror("fopen");
        return -1;
    }

    /* Try to figure out what version this quest is for... */
    if(fread(buf, 1, sizeof(dc_qst_hdr), fp) != sizeof(dc_qst_hdr)) {
        perror("fread");
        rv = -1;
        goto out_close_qst;
    }

    if((qst_type = detect_qst_type()) == QUEST_TYPE_INVALID) {
        fprintf(stderr, "Cannot detect quest type!\n");
        rv = -1;
        goto out_close_qst;
    }

    /* If it's bb, we have a few more bytes to read... */
    if(qst_type == (QUEST_TYPE_ONLINE | QUEST_VER_BB)) {
        hdr_size = sizeof(bb_qst_hdr);
        if(fread(buf + 0x3C, 1, 0x1C, fp) != 0x1C) {
            perror("fread");
            rv = -1;
            goto out_close_qst;
        }
    }

    /* Write the first header out */
    if(process_hdr_file(qst_type)) {
        rv = -1;
        goto out_close_qst;
    }

    /* Read the second header in */
    if(fread(buf, 1, hdr_size, fp) != hdr_size) {
        perror("fread");
        rv = -1;
        goto out_close_qst;
    }

    /* Write that out too */
    if(process_hdr_file(qst_type)) {
        rv = -1;
        goto out_close_qst;
    }

    /* Handle any additional files. This is probably limited to one file, but
       doing it for the generic case is just as easy. */
    while(!morehdr) {
        if(fread(buf, 1, hdr_size, fp) != hdr_size) {
            perror("fread");
            rv = -1;
            goto out_close_qst;
        }

        if((morehdr = process_hdr_file(qst_type)) == -1) {
            rv = -1;
            goto out_close_qst;
        }
    }

    /* We've read one file header beyond where we belong, so rewind. */
    if(fseek(fp, -hdr_size, SEEK_CUR)) {
        perror("fseek");
        rv = -1;
        goto out_close_qst;
    }

    /* The rest depends on the version... */
    switch(qst_type & 0xFF) {
        case QUEST_VER_DC:
        case QUEST_VER_GC:
            rv = convert_dc_qst(fp);
            break;

        case QUEST_VER_PC:
            rv = convert_pc_qst(fp);
            break;

        case QUEST_VER_BB:
            rv = convert_bb_qst(fp);
            break;
    }

out_close_qst:
    fclose(fp);
    return rv;
}

static int read_hdr(const char *fn, uint8_t mbuf[], uint32_t type, char **rfn) {
    FILE *hfp;
    dc_qst_hdr *dc = (dc_qst_hdr *)mbuf;
    pc_qst_hdr *pc = (pc_qst_hdr *)mbuf;
    gc_qst_hdr *gc = (gc_qst_hdr *)mbuf;
    bb_qst_hdr *bb = (bb_qst_hdr *)mbuf;
    long len;

    if(!(hfp = fopen(fn, "rb"))) {
        fprintf(stderr, "Error opening \"%s\": %s\n", fn, strerror(errno));
        return -1;
    }

    /* Make sure it is the right size */
    if(fseek(hfp, 0, SEEK_END)) {
        perror("fseek");
        fclose(hfp);
        return -1;
    }

    len = ftell(hfp);

    if(len != sizeof(dc_qst_hdr) && len != sizeof(bb_qst_hdr)) {
        fprintf(stderr, "\"%s\" is not of the correct size\n", fn);
        fclose(hfp);
        return -1;
    }

    if(fseek(hfp, 0, SEEK_SET)) {
        perror("fseek");
        fclose(hfp);
        return -1;
    }

    if(fread(mbuf, 1, len, hfp) != len) {
        fprintf(stderr, "Cannot read \"%s\"\n", fn);
        fclose(hfp);
        return -1;
    }

    fclose(hfp);

    /* Now, check the header for validity. */
    switch(type) {
        case QUEST_VER_DC | QUEST_TYPE_ONLINE:
            if(dc->hdr.pkt_type != QUEST_FILE_TYPE ||
               dc->hdr.pkt_len != LE16(sizeof(dc_qst_hdr)))
                goto bad_hdr;
            *rfn = dc->filename;
            break;

        case QUEST_VER_DC | QUEST_TYPE_DOWNLOAD:
            if(dc->hdr.pkt_type != DL_QUEST_FILE_TYPE ||
               dc->hdr.pkt_len != LE16(sizeof(dc_qst_hdr)))
                goto bad_hdr;
            *rfn = dc->filename;
            break;

        case QUEST_VER_PC | QUEST_TYPE_ONLINE:
            if(pc->hdr.pkt_type != QUEST_FILE_TYPE ||
               pc->hdr.pkt_len != LE16(sizeof(pc_qst_hdr)))
                goto bad_hdr;
            *rfn = pc->filename;
            break;

        case QUEST_VER_PC | QUEST_TYPE_DOWNLOAD:
            if(pc->hdr.pkt_type != DL_QUEST_FILE_TYPE ||
               pc->hdr.pkt_len != LE16(sizeof(pc_qst_hdr)))
                goto bad_hdr;
            *rfn = pc->filename;
            break;

        case QUEST_VER_GC | QUEST_TYPE_ONLINE:
            if(gc->hdr.pkt_type != QUEST_FILE_TYPE ||
               gc->hdr.pkt_len != LE16(sizeof(gc_qst_hdr)))
                goto bad_hdr;
            *rfn = gc->filename;
            break;

        case QUEST_VER_GC | QUEST_TYPE_DOWNLOAD:
            if(gc->hdr.pkt_type != DL_QUEST_FILE_TYPE ||
               gc->hdr.pkt_len != LE16(sizeof(gc_qst_hdr)))
                goto bad_hdr;
            *rfn = gc->filename;
            break;

        case QUEST_VER_BB | QUEST_TYPE_ONLINE:
            if(bb->hdr.pkt_type != LE16(QUEST_FILE_TYPE) ||
               bb->hdr.pkt_len != LE16(sizeof(bb_qst_hdr)))
                goto bad_hdr;
            *rfn = bb->filename;
            break;
    }

    return 0;

bad_hdr:
    fprintf(stderr, "Header file \"%s\" is invalid\n", fn);
    return -1;
}

static int make_hdr(const char *fn, uint8_t mbuf[], uint32_t type, char **rfn) {
    dc_qst_hdr *dc = (dc_qst_hdr *)mbuf;
    pc_qst_hdr *pc = (pc_qst_hdr *)mbuf;
    gc_qst_hdr *gc = (gc_qst_hdr *)mbuf;
    bb_qst_hdr *bb = (bb_qst_hdr *)mbuf;

    /* Build the appropriate header... */
    switch(type) {
        case QUEST_VER_DC | QUEST_TYPE_ONLINE:
            memset(dc, 0, sizeof(dc_qst_hdr));
            dc->hdr.pkt_type = QUEST_FILE_TYPE;
            dc->hdr.pkt_len = LE16(sizeof(dc_qst_hdr));
            strcpy(dc->filename, fn);
            *rfn = dc->filename;
            break;

        case QUEST_VER_DC | QUEST_TYPE_DOWNLOAD:
            memset(dc, 0, sizeof(dc_qst_hdr));
            dc->hdr.pkt_type = DL_QUEST_FILE_TYPE;
            dc->hdr.pkt_len = LE16(sizeof(dc_qst_hdr));
            strcpy(dc->filename, fn);
            *rfn = dc->filename;
            break;

        case QUEST_VER_PC | QUEST_TYPE_ONLINE:
            memset(pc, 0, sizeof(pc_qst_hdr));
            pc->hdr.pkt_type = QUEST_FILE_TYPE;
            pc->hdr.pkt_len = LE16(sizeof(pc_qst_hdr));
            strcpy(pc->filename, fn);
            *rfn = pc->filename;
            break;

        case QUEST_VER_PC | QUEST_TYPE_DOWNLOAD:
            memset(pc, 0, sizeof(pc_qst_hdr));
            pc->hdr.pkt_type = DL_QUEST_FILE_TYPE;
            pc->hdr.pkt_len = LE16(sizeof(pc_qst_hdr));
            strcpy(pc->filename, fn);
            *rfn = pc->filename;
            break;

        case QUEST_VER_GC | QUEST_TYPE_ONLINE:
            memset(gc, 0, sizeof(gc_qst_hdr));
            gc->hdr.pkt_type = QUEST_FILE_TYPE;
            gc->hdr.pkt_len = LE16(sizeof(gc_qst_hdr));
            strcpy(gc->filename, fn);
            *rfn = gc->filename;
            break;

        case QUEST_VER_GC | QUEST_TYPE_DOWNLOAD:
            memset(gc, 0, sizeof(gc_qst_hdr));
            gc->hdr.pkt_type = DL_QUEST_FILE_TYPE;
            gc->hdr.pkt_len = LE16(sizeof(gc_qst_hdr));
            strcpy(gc->filename, fn);
            *rfn = gc->filename;
            break;

        case QUEST_VER_BB | QUEST_TYPE_ONLINE:
            memset(bb, 0, sizeof(bb_qst_hdr));
            bb->hdr.pkt_type = LE16(QUEST_FILE_TYPE);
            bb->hdr.pkt_len = LE16(sizeof(bb_qst_hdr));
            strcpy(bb->filename, fn);
            *rfn = bb->filename;
            break;
    }

    return 0;
}

static int merge_chunks(FILE *qst, const char *bfn, const char *dfn, FILE *bfp,
                        FILE *dfp, uint32_t qst_type, const char *pfn,
                        FILE *pfp) {
    qst_chunk *chunk = (qst_chunk *)buf;
    uint8_t *nptr;
    int bindone = 0, datdone = 0, pvrdone = 0;
    ssize_t amt;
    size_t chsz = sizeof(qst_chunk);

    if(!pfn || !pfp)
        pvrdone = 1;

    /* Set up the header first. */
    if(qst_type & QUEST_VER_PC) {
        chunk->hdr.pc.pkt_len = LE16(sizeof(qst_chunk));
        nptr = &chunk->hdr.pc.flags;
        *nptr = 0;

        if(qst_type & QUEST_TYPE_DOWNLOAD)
            chunk->hdr.pc.pkt_type = DL_QUEST_CHUNK_TYPE;
        else
            chunk->hdr.pc.pkt_type = QUEST_CHUNK_TYPE;
    }
    else {
        chunk->hdr.dc.pkt_len = LE16(sizeof(qst_chunk));
        nptr = &chunk->hdr.dc.flags;
        *nptr = 0;

        if(qst_type & QUEST_TYPE_DOWNLOAD)
            chunk->hdr.dc.pkt_type = DL_QUEST_CHUNK_TYPE;
        else
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
    }

    while(!bindone || !datdone || !pvrdone) {
        /* First, do the bin file if we've got any more to read from it */
        if(!bindone) {
            /* Clear the data */
            memset(chunk->data, 0, 1024);

            /* Fill in what we need to */
            memcpy(chunk->filename, bfn, 16);
            amt = fread(chunk->data, 1, 0x400, bfp);
            chunk->length = LE32(((uint32_t)amt));

            /* Are we done with this file? */
            if(amt != 0x400)
                bindone = 1;

            if(amt < 0) {
                perror("Cannot read input file");
                return -1;
            }

            if(amt != 0) {
                printf("%s chunk %d (%d bytes)\n", bfn, (int)*nptr, (int)amt);

                if(fwrite(chunk, 1, chsz, qst) != chsz) {
                    perror("Cannot write to output file");
                    return -1;
                }
            }
            else {
                bindone = 1;
            }
        }

        /* Next, do the dat file if we've got any more to read from it */
        if(!datdone) {
            /* Clear the data */
            memset(chunk->data, 0, 1024);

            /* Fill in what we need to */
            memcpy(chunk->filename, dfn, 16);
            amt = fread(chunk->data, 1, 0x400, dfp);
            chunk->length = LE32(((uint32_t)amt));

            /* Are we done with this file? */
            if(amt != 0x400)
                datdone = 1;

            if(amt < 0) {
                perror("Cannot read input file");
                return -1;
            }

            if(amt != 0) {
                printf("%s chunk %d (%d bytes)\n", dfn, (int)*nptr, (int)amt);

                if(fwrite(chunk, 1, chsz, qst) != chsz) {
                    perror("Cannot write to output file");
                    return -1;
                }
            }
            else {
                printf("wut?\n");
                datdone = 1;
            }
        }

        /* Finally, if there's a .pvr file, do it if we have anything to do */
        if(!pvrdone) {
            /* Clear the data */
            memset(chunk->data, 0, 1024);

            /* Fill in what we need to */
            memcpy(chunk->filename, pfn, 16);
            amt = fread(chunk->data, 1, 0x400, pfp);
            chunk->length = LE32(((uint32_t)amt));

            /* Are we done with this file? */
            if(amt != 0x400)
                pvrdone = 1;

            if(amt < 0) {
                perror("Cannot read input file");
                return -1;
            }

            if(amt != 0) {
                printf("%s chunk %d (%d bytes)\n", pfn, (int)*nptr, (int)amt);

                if(fwrite(chunk, 1, chsz, qst) != chsz) {
                    perror("Cannot write to output file");
                    return -1;
                }
            }
            else {
                pvrdone = 1;
            }
        }

        ++*nptr;
    }

    return 0;
}

static int merge_chunks2(FILE *qst, const char *bfn, const char *dfn, FILE *bfp,
                         FILE *dfp, uint32_t qst_type, const char *pfn,
                         FILE *pfp) {
    bb_qst_chunk *chunk = (bb_qst_chunk *)buf;
    uint8_t *nptr;
    int bindone = 0, datdone = 0, pvrdone = 0;
    ssize_t amt;
    size_t chsz = sizeof(qst_chunk);
    uint32_t nil = 0;

    if(!pfn || !pfp)
        pvrdone = 1;

    /* Set up the header first. */
    chunk->hdr.pkt_len = LE16(sizeof(bb_qst_chunk));
    nptr = (uint8_t *)&chunk->hdr.flags;
    *nptr = 0;
    chunk->hdr.pkt_type = LE16(QUEST_CHUNK_TYPE);
    chsz = sizeof(bb_qst_chunk);

    while(!bindone || !datdone || !pvrdone) {
        /* First, do the dat file if we've got any more to read from it */
        if(!datdone) {
            /* Clear the data */
            memset(chunk->data, 0, 1024);

            /* Fill in what we need to */
            memcpy(chunk->filename, dfn, 16);
            amt = fread(chunk->data, 1, 0x400, dfp);
            chunk->length = LE32(((uint32_t)amt));

            /* Are we done with this file? */
            if(amt != 0x400)
                datdone = 1;

            if(amt < 0) {
                perror("Cannot read input file");
                return -1;
            }

            if(amt != 0) {
                printf("%s chunk %d (%d bytes)\n", dfn, (int)*nptr, (int)amt);

                if(fwrite(chunk, 1, chsz, qst) != chsz) {
                    perror("Cannot write to output file");
                    return -1;
                }
            }
            else {
                datdone = 1;
            }

            /* Sigh... */
            fwrite(&nil, 1, 4, qst);
        }

        /* Next, do the bin file if we've got any more to read from it */
        if(!bindone) {
            /* Clear the data */
            memset(chunk->data, 0, 1024);

            /* Fill in what we need to */
            memcpy(chunk->filename, bfn, 16);
            amt = fread(chunk->data, 1, 0x400, bfp);
            chunk->length = LE32(((uint32_t)amt));

            /* Are we done with this file? */
            if(amt != 0x400)
                bindone = 1;

            if(amt < 0) {
                perror("Cannot read input file");
                return -1;
            }

            if(amt != 0) {
                printf("%s chunk %d (%d bytes)\n", bfn, (int)*nptr, (int)amt);

                if(fwrite(chunk, 1, chsz, qst) != chsz) {
                    perror("Cannot write to output file");
                    return -1;
                }
            }
            else {
                bindone = 1;
            }

            /* Sigh... */
            fwrite(&nil, 1, 4, qst);
        }

        /* Finally, if there's a .pvr file, do it if we have anything to do */
        if(!pvrdone) {
            /* Clear the data */
            memset(chunk->data, 0, 1024);

            /* Fill in what we need to */
            memcpy(chunk->filename, pfn, 16);
            amt = fread(chunk->data, 1, 0x400, pfp);
            chunk->length = LE32(((uint32_t)amt));

            /* Are we done with this file? */
            if(amt != 0x400)
                pvrdone = 1;

            if(amt < 0) {
                perror("Cannot read input file");
                return -1;
            }

            if(amt != 0) {
                printf("%s chunk %d (%d bytes)\n", pfn, (int)*nptr, (int)amt);

                if(fwrite(chunk, 1, chsz, qst) != chsz) {
                    perror("Cannot write to output file");
                    return -1;
                }
            }
            else {
                pvrdone = 1;
            }

            /* Sigh... */
            fwrite(&nil, 1, 4, qst);
        }

        ++*nptr;
    }

    return 0;
}

static int bindat_to_qst(int argc, const char *argv[]) {
    FILE *bfp, *dfp, *qst;
    uint32_t qst_type;
    uint8_t bhbuf[sizeof(bb_qst_hdr)], dhbuf[sizeof(bb_qst_hdr)];
    dc_qst_hdr *hdr;
    bb_qst_hdr *bbhdr;
    char *qst_name;
    char *tmp, *bfn, *dfn;
    size_t hsz = sizeof(dc_qst_hdr);

    if(argc != 5 && argc != 7) {
        usage(argv);
        exit(EXIT_FAILURE);
    }

    /* Figure out what type of quest we have */
    if(!strcmp(argv[2], "dc"))
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_DC;
    else if(!strcmp(argv[2], "pc"))
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_PC;
    else if(!strcmp(argv[2], "gc"))
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_GC;
    else if(!strcmp(argv[2], "dcdl"))
        qst_type = QUEST_TYPE_DOWNLOAD | QUEST_VER_DC;
    else if(!strcmp(argv[2], "pcdl"))
        qst_type = QUEST_TYPE_DOWNLOAD | QUEST_VER_PC;
    else if(!strcmp(argv[2], "gcdl"))
        qst_type = QUEST_TYPE_DOWNLOAD | QUEST_VER_GC;
    else if(!strcmp(argv[2], "bb")) {
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_BB;
        hsz = sizeof(bb_qst_hdr);
    }
    else {
        fprintf(stderr, "Invalid quest type given!\n");
        return -1;
    }

    /* Try to open the bin/dat files */
    if(!(bfp = fopen(argv[3], "rb"))) {
        fprintf(stderr, "Error opening \"%s\": %s\n", argv[3], strerror(errno));
        return -1;
    }

    if(!(dfp = fopen(argv[4], "rb"))) {
        fprintf(stderr, "Error opening \"%s\": %s\n", argv[4], strerror(errno));
        fclose(bfp);
        return -1;
    }

    /* If we have header files, open them and read them in. */
    if(argc == 7) {
        /* First up is the .bin header file */
        if(read_hdr(argv[5], bhbuf, qst_type, &bfn))
            goto out;

        /* Then, do the .dat header file */
        if(read_hdr(argv[6], dhbuf, qst_type, &dfn))
            goto out;
    }
    else {
        /* We need to construct the headers ourselves, I guess... */
        if(strlen(argv[3]) > 16 || strlen(argv[4]) > 16) {
            fprintf(stderr, "Quest filenames too long without headers\n");
            goto out;
        }

        make_hdr(argv[3], bhbuf, qst_type, &bfn);
        make_hdr(argv[4], dhbuf, qst_type, &dfn);
    }

    /* Figure out the name of the .qst file */
    if(!(qst_name = (char *)malloc(strlen(argv[3]) + 5))) {
        perror("malloc");
        goto out;
    }

    strcpy(qst_name, argv[3]);

    if(!(tmp = strrchr(qst_name, '.')))
        tmp = qst_name + strlen(qst_name);

    *tmp++ = '.';
    *tmp++ = 'q';
    *tmp++ = 's';
    *tmp++ = 't';
    *tmp = 0;

    printf("Writing to %s\n", qst_name);

    if(!(qst = fopen(qst_name, "wb"))) {
        perror("Cannot open output file");
        free(qst_name);
        goto out;
    }

    free(qst_name);

    /* Write out the headers first. */
    if(hsz == sizeof(dc_qst_hdr)) {
        hdr = (dc_qst_hdr *)bhbuf;
        if(fseek(bfp, 0, SEEK_END)) {
            perror("fseek");
            goto out_qst;
        }

        /* Update the length in the .bin header, if needed */
        hdr->length = LE32(((uint32_t)ftell(bfp)));

        if(fseek(bfp, 0, SEEK_SET)) {
            perror("fseek");
            goto out_qst;
        }

        /* Write the .bin header */
        if(fwrite(bhbuf, 1, sizeof(dc_qst_hdr), qst) != sizeof(dc_qst_hdr)) {
            perror("Cannot write to output file");
            goto out_qst;
        }

        hdr = (dc_qst_hdr *)dhbuf;
        if(fseek(dfp, 0, SEEK_END)) {
            perror("fseek");
            goto out_qst;
        }

        /* Update the length in the .dat header, if needed */
        hdr->length = LE32(((uint32_t)ftell(dfp)));

        if(fseek(dfp, 0, SEEK_SET)) {
            perror("fseek");
            goto out_qst;
        }

        /* Write the .dat header */
        if(fwrite(dhbuf, 1, sizeof(dc_qst_hdr), qst) != sizeof(dc_qst_hdr)) {
            perror("Cannot write to output file");
            goto out_qst;
        }

        if(merge_chunks(qst, bfn, dfn, bfp, dfp, qst_type, NULL, NULL))
            goto out_qst;
    }
    else {
        /* Ugh... Qedit makes everything backwards for bb >_> */
        bbhdr = (bb_qst_hdr *)dhbuf;
        if(fseek(dfp, 0, SEEK_END)) {
            perror("fseek");
            goto out_qst;
        }

        /* Update the length in the .dat header, if needed */
        bbhdr->length = LE32(((uint32_t)ftell(dfp)));

        /* Set a name, if one isn't given. */
        if(bbhdr->name[0] == 0) {
            strncpy(bbhdr->name, argv[4], 24);
            bbhdr->name[23] = 0;
        }

        if(fseek(dfp, 0, SEEK_SET)) {
            perror("fseek");
            goto out_qst;
        }

        /* Write the .dat header */
        if(fwrite(dhbuf, 1, sizeof(bb_qst_hdr), qst) != sizeof(bb_qst_hdr)) {
            perror("Cannot write to output file");
            goto out_qst;
        }

        bbhdr = (bb_qst_hdr *)bhbuf;
        if(fseek(bfp, 0, SEEK_END)) {
            perror("fseek");
            goto out_qst;
        }

        /* Update the length in the .bin header, if needed */
        bbhdr->length = LE32(((uint32_t)ftell(bfp)));

        /* Set a name, if one isn't given. */
        if(bbhdr->name[0] == 0) {
            strncpy(bbhdr->name, argv[4], 24);
            bbhdr->name[23] = 0;
        }

        if(fseek(bfp, 0, SEEK_SET)) {
            perror("fseek");
            goto out_qst;
        }

        /* Write the .bin header */
        if(fwrite(bhbuf, 1, sizeof(bb_qst_hdr), qst) != sizeof(bb_qst_hdr)) {
            perror("Cannot write to output file");
            goto out_qst;
        }

        if(merge_chunks2(qst, bfn, dfn, bfp, dfp, qst_type, NULL, NULL))
            goto out_qst;
    }

    fclose(qst);
    fclose(dfp);
    fclose(bfp);
    return 0;

out_qst:
    fclose(qst);
out:
    fclose(dfp);
    fclose(bfp);
    return -1;
}

static int bindatpvr_to_qst(int argc, const char *argv[]) {
    FILE *bfp, *dfp, *pfp, *qst;
    uint32_t qst_type;
    uint8_t bhbuf[sizeof(dc_qst_hdr)], dhbuf[sizeof(dc_qst_hdr)];
    uint8_t phbuf[sizeof(dc_qst_hdr)];
    dc_qst_hdr *hdr;
    char *qst_name;
    char *tmp, *bfn, *dfn, *pfn;

    if(argc != 6 && argc != 9) {
        usage(argv);
        exit(EXIT_FAILURE);
    }

    /* Figure out what type of quest we have */
    if(!strcmp(argv[2], "dc"))
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_DC;
    else if(!strcmp(argv[2], "pc"))
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_PC;
    else if(!strcmp(argv[2], "gc"))
        qst_type = QUEST_TYPE_ONLINE | QUEST_VER_GC;
    else if(!strcmp(argv[2], "dcdl"))
        qst_type = QUEST_TYPE_DOWNLOAD | QUEST_VER_DC;
    else if(!strcmp(argv[2], "pcdl"))
        qst_type = QUEST_TYPE_DOWNLOAD | QUEST_VER_PC;
    else if(!strcmp(argv[2], "gcdl"))
        qst_type = QUEST_TYPE_DOWNLOAD | QUEST_VER_GC;
    else {
        fprintf(stderr, "Invalid quest type given!\n");
        return -1;
    }

    /* Try to open the bin/dat files */
    if(!(bfp = fopen(argv[3], "rb"))) {
        fprintf(stderr, "Error opening \"%s\": %s\n", argv[3], strerror(errno));
        return -1;
    }

    if(!(dfp = fopen(argv[4], "rb"))) {
        fprintf(stderr, "Error opening \"%s\": %s\n", argv[4], strerror(errno));
        fclose(bfp);
        return -1;
    }

    if(!(pfp = fopen(argv[5], "rb"))) {
        fprintf(stderr, "Error opening \"%s\": %s\n", argv[4], strerror(errno));
        fclose(dfp);
        fclose(bfp);
        return -1;
    }

    /* If we have header files, open them and read them in. */
    if(argc == 9) {
        /* First up is the .bin header file */
        if(read_hdr(argv[6], bhbuf, qst_type, &bfn))
            goto out;

        /* Then, do the .dat header file */
        if(read_hdr(argv[7], dhbuf, qst_type, &dfn))
            goto out;

        /* Then, do the .pvr header file */
        if(read_hdr(argv[8], phbuf, qst_type, &pfn))
            goto out;
    }
    else {
        /* We need to construct the headers ourselves, I guess... */
        if(strlen(argv[3]) > 16 || strlen(argv[4]) > 16 ||
           strlen(argv[5]) > 16) {
            fprintf(stderr, "Quest filenames too long without headers\n");
            goto out;
        }

        make_hdr(argv[3], bhbuf, qst_type, &bfn);
        make_hdr(argv[4], dhbuf, qst_type, &dfn);
        make_hdr(argv[5], phbuf, qst_type, &pfn);
    }

    /* Figure out the name of the .qst file */
    if(!(qst_name = (char *)malloc(strlen(argv[3]) + 5))) {
        perror("malloc");
        goto out;
    }

    strcpy(qst_name, argv[3]);

    if(!(tmp = strrchr(qst_name, '.')))
        tmp = qst_name + strlen(qst_name);

    *tmp++ = '.';
    *tmp++ = 'q';
    *tmp++ = 's';
    *tmp++ = 't';
    *tmp = 0;

    printf("Writing to %s\n", qst_name);

    if(!(qst = fopen(qst_name, "wb"))) {
        perror("Cannot open output file");
        free(qst_name);
        goto out;
    }

    free(qst_name);

    /* Write out the headers first. */
    hdr = (dc_qst_hdr *)bhbuf;
    if(fseek(bfp, 0, SEEK_END)) {
        perror("fseek");
        goto out_qst;
    }

    /* Update the length in the .bin header, if needed */
    hdr->length = LE32(((uint32_t)ftell(bfp)));

    if(fseek(bfp, 0, SEEK_SET)) {
        perror("fseek");
        goto out_qst;
    }

    /* Write the .bin header */
    if(fwrite(bhbuf, 1, sizeof(dc_qst_hdr), qst) != sizeof(dc_qst_hdr)) {
        perror("Cannot write to output file");
        goto out_qst;
    }

    hdr = (dc_qst_hdr *)dhbuf;
    if(fseek(dfp, 0, SEEK_END)) {
        perror("fseek");
        goto out_qst;
    }

    /* Update the length in the .dat header, if needed */
    hdr->length = LE32(((uint32_t)ftell(dfp)));

    if(fseek(dfp, 0, SEEK_SET)) {
        perror("fseek");
        goto out_qst;
    }

    /* Write the .dat header */
    if(fwrite(dhbuf, 1, sizeof(dc_qst_hdr), qst) != sizeof(dc_qst_hdr)) {
        perror("Cannot write to output file");
        goto out_qst;
    }

    hdr = (dc_qst_hdr *)phbuf;
    if(fseek(pfp, 0, SEEK_END)) {
        perror("fseek");
        goto out_qst;
    }

    /* Update the length in the .pvr header, if needed */
    hdr->length = LE32(((uint32_t)ftell(pfp)));

    if(fseek(pfp, 0, SEEK_SET)) {
        perror("fseek");
        goto out_qst;
    }

    /* Write the .dat header */
    if(fwrite(phbuf, 1, sizeof(dc_qst_hdr), qst) != sizeof(dc_qst_hdr)) {
        perror("Cannot write to output file");
        goto out_qst;
    }

    if(merge_chunks(qst, bfn, dfn, bfp, dfp, qst_type, pfn, pfp))
        goto out_qst;

    fclose(qst);
    fclose(pfp);
    fclose(dfp);
    fclose(bfp);
    return 0;

out_qst:
    fclose(qst);
out:
    fclose(pfp);
    fclose(dfp);
    fclose(bfp);
    return -1;
}

int main(int argc, const char *argv[]) {
    if(argc < 3) {
        usage(argv);
        exit(EXIT_FAILURE);
    }

    if(!strcmp(argv[1], "-x")) {
        if(qst_to_bindat(argv[2])) {
            fprintf(stderr, "Extraction failed.\n");
            exit(EXIT_FAILURE);
        }

        fprintf(stderr, "Successfully extracted quest\n");
    }
    else if(!strcmp(argv[1], "-m")) {
        if(bindat_to_qst(argc, argv)) {
            fprintf(stderr, "Merging failed.\n");
            exit(EXIT_FAILURE);
        }

        fprintf(stderr, "Successfully merged quest\n");
    }
    else if(!strcmp(argv[1], "-mp")) {
        if(bindatpvr_to_qst(argc, argv)) {
            fprintf(stderr, "Merging failed.\n");
            exit(EXIT_FAILURE);
        }

        fprintf(stderr, "Successfully merged quest\n");
    }
    else {
        usage(argv);
        exit(EXIT_FAILURE);
    }

    return 0;
}
