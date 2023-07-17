/*
    Sylverant PSO Tools
    Xbox Download Quest Converter
    Copyright (C) 2021, 2023 Lawrence Sebald

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

/* This program will convert a QEdit .qst file in Gamecube downloadable quest
   format for use on the Xbox version of the game. */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
#define LE16(x) (((x >> 8) & 0xFF) | ((x & 0xFF) << 8))
#define LE32(x) (((x >> 24) & 0x00FF) | \
                 ((x >>  8) & 0xFF00) | \
                 ((x & 0xFF00) <<  8) | \
                 ((x & 0x00FF) << 24))
#define LE64(x) (((x >> 56) & 0x000000FF) | \
                 ((x >> 40) & 0x0000FF00) | \
                 ((x >> 24) & 0x00FF0000) | \
                 ((x >>  8) & 0xFF000000) | \
                 ((x & 0xFF000000) <<  8) | \
                 ((x & 0x00FF0000) << 24) | \
                 ((x & 0x0000FF00) << 40) | \
                 ((x & 0x000000FF) << 56))
#else
#define LE16(x) x
#define LE32(x) x
#define LE64(x) x
#endif

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

typedef struct dc_pkt_hdr {
    uint8_t pkt_type;
    uint8_t flags;
    uint16_t pkt_len;
} PACKED dc_pkt_hdr_t;

typedef struct gc_quest_file {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint16_t unused;
    uint16_t flags;
    char filename[16];
    uint32_t length;
} PACKED gc_quest_file_pkt;

typedef struct xbox_quest_file {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint16_t quest_id;
    uint16_t flags;
    char filename[16];
    uint32_t length;
    char xbox_filename[16];
    uint16_t quest_id2;
    uint16_t flags2;                /* ??? 0x00 0x30 */
    uint32_t unused2;
} PACKED xbox_quest_file_pkt;

typedef struct dc_quest_chunk {
    dc_pkt_hdr_t hdr;
    char filename[16];
    char data[1024];
    uint32_t length;
} PACKED dc_quest_chunk_pkt;

static int write_xbox_hdr(FILE *fp, gc_quest_file_pkt *gc,
                          unsigned long quest_id, unsigned long ep,
                          char lang) {
    xbox_quest_file_pkt xb;
    int isbin = 0;

    if(ep == 2)
        ep = 256;
    else
        ep = 0;

    if(strstr(gc->filename, ".bin"))
        isbin = 1;

    memset(&xb, 0, sizeof(xbox_quest_file_pkt));
    xb.hdr.pkt_type = 0xA6;
    xb.hdr.flags = (uint8_t)quest_id;
    xb.hdr.pkt_len = LE16(0x0054);

    memcpy(xb.name, gc->name, 32);
    xb.quest_id = LE16(quest_id | ep);
    if(isbin)
        sprintf(xb.filename, "quest%lu.bin", quest_id | ep);
    else
        sprintf(xb.filename, "quest%lu.dat", quest_id | ep);
    xb.length = gc->length;

    sprintf(xb.xbox_filename, "quest%lu_%c.dat", quest_id | ep, lang);
    xb.quest_id2 = LE16(quest_id | ep);
    xb.flags2 = LE16(0x3000);

    return fwrite(&xb, sizeof(xbox_quest_file_pkt), 1, fp);
}

static int copy_chunks(FILE *in, FILE *out, unsigned long qid,
                       unsigned long ep) {
    dc_quest_chunk_pkt pkt;
    char fn[17];
    uint16_t len;

    if(ep == 2)
        ep = 256;
    else
        ep = 0;

    memset(fn, 0, sizeof(fn));

    while(fread(&pkt.hdr, 1, sizeof(dc_pkt_hdr_t), in) == sizeof(dc_pkt_hdr_t)) {
        len = LE16(pkt.hdr.pkt_len) - sizeof(dc_pkt_hdr_t);
        if(fread(pkt.filename, 1, len, in) != (ssize_t)len) {
            perror("Error reading file data");
            return -1;
        }

        memcpy(fn, pkt.filename, 16);
        if(strstr(fn, ".bin"))
            sprintf(pkt.filename, "quest%lu.bin", qid | ep);
        else
            sprintf(pkt.filename, "quest%lu.dat", qid | ep);

        len += sizeof(dc_pkt_hdr_t);
        if(fwrite(&pkt, 1, len, out) != (ssize_t)len) {
            perror("Error copying file data");
            return -1;
        }
    }

    return 0;
}

static int copy_data(FILE *in, FILE *out) {
    uint8_t block[512];
    size_t len;

    while((len = fread(block, 1, 512, in))) {
        if(fwrite(block, 1, len, out) != len) {
            perror("Error copying file data");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    FILE *in, *out;
    gc_quest_file_pkt gc;
    char lang;
    unsigned long qid, ep;
    int rv;

    if(argc != 6) {
        printf("Usage: %s input output quest_id episode l\n", argv[0]);
        printf("Where l is letter representing a language (j, e, f, s, g)\n");
        return 1;
    }

    errno = 0;
    qid = strtoul(argv[3], NULL, 0);
    if(errno) {
        perror("Cannot read quest id");
        return 1;
    }

    if(qid > 255) {
        printf("Quest ID '%s' is invalid, must be less than 255.", argv[3]);
        return 1;
    }

    errno = 0;
    ep = strtoul(argv[4], NULL, 0);
    if(errno) {
        perror("Cannot read episode");
        return 1;
    }

    if(ep != 1 && ep != 2) {
        printf("Episode '%s' is invalid, must be 1 or 2.", argv[4]);
        return 1;
    }

    if(strlen(argv[5]) != 1) {
        printf("Language code '%s' is invalid\n", argv[4]);
        return 1;
    }

    if(argv[5][0] != 'j' && argv[5][0] != 'e' && argv[5][0] != 'f' &&
       argv[5][0] != 's' && argv[5][0] != 'g') {
        printf("Language code '%s' is invalid\n", argv[5]);
        return 1;
    }

    lang = argv[5][0];

    if(!(in = fopen(argv[1], "rb"))) {
        perror("Cannot open input file");
        return 1;
    }

    if(!(out = fopen(argv[2], "wb"))) {
        perror("Cannot open output file");
        fclose(in);
        return 1;
    }

    if(fread(&gc, sizeof(gc_quest_file_pkt), 1, in) != 1) {
        printf("Cannot read from input file\n");
        fclose(out);
        fclose(in);
        return 1;
    }

    if(write_xbox_hdr(out, &gc, qid, ep, lang) != 1) {
        printf("Cannot write to output file\n");
        fclose(out);
        fclose(in);
        return 1;
    }

    if(fread(&gc, sizeof(gc_quest_file_pkt), 1, in) != 1) {
        printf("Cannot read from input file\n");
        fclose(out);
        fclose(in);
        return 1;
    }

    if(write_xbox_hdr(out, &gc, qid, ep, lang) != 1) {
        printf("Cannot write to output file\n");
        fclose(out);
        fclose(in);
        return 1;
    }

    rv = copy_chunks(in, out, qid, ep);
    fclose(out);
    fclose(in);

    return rv;
}
