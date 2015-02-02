/*
    Sylverant PSO Tools
    Quest Enemy Parser
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

/* This file was borrowed from the ship server code (although with much of the
   functionality stripped from it). */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <sys/stat.h>

#include <sylverant/debug.h>
#include <sylverant/prs.h>

#include "packets.h"
#include "quest_enemies.h"

static uint8_t *decompress_dat(uint8_t *inbuf, uint32_t insz, uint32_t *osz) {
    uint8_t *rv;
    int sz;

    if((sz = prs_decompress_buf(inbuf, &rv, (size_t)insz)) < 0) {
        debug(DBG_WARN, "Cannot decompress data: %s\n", strerror(-sz));
        return NULL;
    }

    *osz = (uint32_t)sz;
    return rv;
}

uint8_t *read_dat(const char *fn, uint32_t *osz, int comp) {
    FILE *fp;
    off_t sz;
    uint8_t *buf, *rv;

    /* Read the file in. */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_WARN, "Cannot open quest file \"%s\": %s\n", fn,
              strerror(errno));
        return NULL;
    }

    fseeko(fp, 0, SEEK_END);
    sz = ftello(fp);
    fseeko(fp, 0, SEEK_SET);

    if(!(buf = (uint8_t *)malloc(sz))) {
        debug(DBG_WARN, "Cannot allocate memory to read dat: %s\n",
              strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(fread(buf, 1, sz, fp) != sz) {
        debug(DBG_WARN, "Cannot read dat: %s\n", strerror(errno));
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    if(comp) {
        /* Return it decompressed. */
        rv = decompress_dat(buf, (uint32_t)sz, osz);
        free(buf);
        return rv;
    }

    return buf;
}

static uint32_t qst_dat_size(const uint8_t *buf, int ver) {
    const dc_quest_file_pkt *dchdr = (const dc_quest_file_pkt *)buf;
    const pc_quest_file_pkt *pchdr = (const pc_quest_file_pkt *)buf;
    const bb_quest_file_pkt *bbhdr = (const bb_quest_file_pkt *)buf;
    char fn[32];
    char *ptr;

    /* Figure out the size of the .dat portion. */
    switch(ver) {
        case CLIENT_VERSION_DC:
            /* Check the first file to see if it is the dat. */
            strncpy(fn, dchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(dchdr->length);

            /* Try the second file in the qst */
            dchdr = (const dc_quest_file_pkt *)(buf + 0x3C);
            strncpy(fn, dchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(dchdr->length);

            /* Didn't find it, punt. */
            return 0;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_PC:
            /* Check the first file to see if it is the dat. */
            strncpy(fn, pchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(pchdr->length);

            /* Try the second file in the qst */
            pchdr = (const pc_quest_file_pkt *)(buf + 0x3C);
            strncpy(fn, pchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(pchdr->length);

            /* Didn't find it, punt. */
            return 0;

        case CLIENT_VERSION_BB:
            /* Check the first file to see if it is the dat. */
            strncpy(fn, bbhdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(bbhdr->length);

            /* Try the second file in the qst */
            bbhdr = (const bb_quest_file_pkt *)(buf + 0x58);
            strncpy(fn, bbhdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(bbhdr->length);

            /* Didn't find it, punt. */
            return 0;
    }

    return 0;
}

static int copy_dc_qst_dat(const uint8_t *buf, uint8_t *rbuf, off_t sz,
                           uint32_t dsz) {
    const dc_quest_chunk_pkt *ck;
    uint32_t ptr = 120, optr = 0;
    char fn[32];
    char *cptr;
    uint32_t clen;

    while(ptr < sz) {
        ck = (const dc_quest_chunk_pkt *)(buf + ptr);

        /* Check the chunk for validity. */
        if(ck->hdr.dc.pkt_type != QUEST_CHUNK_TYPE ||
           ck->hdr.dc.pkt_len != LE16(0x0418)) {
            debug(DBG_WARN, "Unknown or damaged quest chunk!\n");
            return -1;
        }

        /* Grab the vitals... */
        strncpy(fn, ck->filename, 16);
        fn[16] = 0;
        clen = LE32(ck->length);
        cptr = strrchr(fn, '.');

        /* Sanity check... */
        if(clen > 1024 || !cptr) {
            debug(DBG_WARN, "Damaged quest chunk!\n");
            return -1;
        }

        /* See if this is part of the .dat file */
        if(!strcmp(cptr, ".dat")) {
            if(optr + clen > dsz) {
                debug(DBG_WARN, "Quest file appears to be corrupted!\n");
                return -1;
            }

            memcpy(rbuf + optr, ck->data, clen);
            optr += clen;
        }

        ptr += 0x0418;
    }

    if(optr != dsz) {
        debug(DBG_WARN, "Quest file appears to be corrupted!\n");
        return -1;
    }

    return 0;
}

static int copy_pc_qst_dat(const uint8_t *buf, uint8_t *rbuf, off_t sz,
                           uint32_t dsz) {
    const dc_quest_chunk_pkt *ck;
    uint32_t ptr = 120, optr = 0;
    char fn[32];
    char *cptr;
    uint32_t clen;

    while(ptr < sz) {
        ck = (const dc_quest_chunk_pkt *)(buf + ptr);

        /* Check the chunk for validity. */
        if(ck->hdr.pc.pkt_type != QUEST_CHUNK_TYPE ||
           ck->hdr.pc.pkt_len != LE16(0x0418)) {
            debug(DBG_WARN, "Unknown or damaged quest chunk!\n");
            return -1;
        }

        /* Grab the vitals... */
        strncpy(fn, ck->filename, 16);
        fn[16] = 0;
        clen = LE32(ck->length);
        cptr = strrchr(fn, '.');

        /* Sanity check... */
        if(clen > 1024 || !cptr) {
            debug(DBG_WARN, "Damaged quest chunk!\n");
            return -1;
        }

        /* See if this is part of the .dat file */
        if(!strcmp(cptr, ".dat")) {
            if(optr + clen > dsz) {
                debug(DBG_WARN, "Quest file appears to be corrupted!\n");
                return -1;
            }

            memcpy(rbuf + optr, ck->data, clen);
            optr += clen;
        }

        ptr += 0x0418;
    }

    if(optr != dsz) {
        debug(DBG_WARN, "Quest file appears to be corrupted!\n");
        return -1;
    }

    return 0;
}

static int copy_bb_qst_dat(const uint8_t *buf, uint8_t *rbuf, off_t sz,
                           uint32_t dsz) {
    const bb_quest_chunk_pkt *ck;
    uint32_t ptr = 176, optr = 0;
    char fn[32];
    char *cptr;
    uint32_t clen;

    while(ptr < sz) {
        ck = (const bb_quest_chunk_pkt *)(buf + ptr);

        /* Check the chunk for validity. */
        if(ck->hdr.pkt_type != LE16(QUEST_CHUNK_TYPE) ||
           ck->hdr.pkt_len != LE16(0x041C)) {
            debug(DBG_WARN, "Unknown or damaged quest chunk!\n");
            return -1;
        }

        /* Grab the vitals... */
        strncpy(fn, ck->filename, 16);
        fn[16] = 0;
        clen = LE32(ck->length);
        cptr = strrchr(fn, '.');

        /* Sanity check... */
        if(clen > 1024 || !cptr) {
            debug(DBG_WARN, "Damaged quest chunk!\n");
            return -1;
        }

        /* See if this is part of the .dat file */
        if(!strcmp(cptr, ".dat")) {
            if(optr + clen > dsz) {
                debug(DBG_WARN, "Quest file appears to be corrupted!\n");
                return -1;
            }

            memcpy(rbuf + optr, ck->data, clen);
            optr += clen;
        }

        ptr += 0x0420;
    }

    if(optr != dsz) {
        debug(DBG_WARN, "Quest file appears to be corrupted!\n");
        return -1;
    }

    return 0;
}

int is_qst(const char *fn, int ver) {
    FILE *fp;
    off_t sz;
    uint8_t *buf, *buf2, *rv;
    uint32_t dsz;

    /* Read the file in. */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_WARN, "Cannot open quest file \"%s\": %s\n", fn,
              strerror(errno));
        return -1;
    }

    fseeko(fp, 0, SEEK_END);
    sz = ftello(fp);
    fseeko(fp, 0, SEEK_SET);

    /* Make sure the file's size is sane. */
    if(sz < 120) {
        fclose(fp);
        return 0;
    }

    if(!(buf = (uint8_t *)malloc(sz))) {
        debug(DBG_WARN, "Cannot allocate memory to read quest: %s\n",
              strerror(errno));
        fclose(fp);
        return -1;
    }

    if(fread(buf, 1, sz, fp) != sz) {
        debug(DBG_WARN, "Cannot read quest file: %s\n", strerror(errno));
        free(buf);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    /* Figure out how big the .dat portion is. */
    if(!(dsz = qst_dat_size(buf, ver))) {
        /* If we don't find it, then it isn't a .qst file. */
        free(buf);
        return 0;
    }

    /* We found it, so hopefully we actually have a .qst file. */
    free(buf);
    return 1;
}

uint8_t *read_qst(const char *fn, uint32_t *osz, int ver) {
    FILE *fp;
    off_t sz;
    uint8_t *buf, *buf2, *rv;
    uint32_t dsz;

    /* Read the file in. */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_WARN, "Cannot open quest file \"%s\": %s\n", fn,
              strerror(errno));
        return NULL;
    }

    fseeko(fp, 0, SEEK_END);
    sz = ftello(fp);
    fseeko(fp, 0, SEEK_SET);

    /* Make sure the file's size is sane. */
    if(sz < 120) {
        debug(DBG_WARN, "Quest file \"%s\" too small\n", fn);
        fclose(fp);
        return NULL;
    }

    if(!(buf = (uint8_t *)malloc(sz))) {
        debug(DBG_WARN, "Cannot allocate memory to read qst: %s\n",
              strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(fread(buf, 1, sz, fp) != sz) {
        debug(DBG_WARN, "Cannot read qst: %s\n", strerror(errno));
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    /* Figure out how big the .dat portion is. */
    if(!(dsz = qst_dat_size(buf, ver))) {
        debug(DBG_WARN, "Cannot find dat size in qst \"%s\"\n", fn);
        free(buf);
        return NULL;
    }

    /* Allocate space for it. */
    if(!(buf2 = (uint8_t *)malloc(dsz))) {
        debug(DBG_WARN, "Cannot allocate memory to decode qst: %s\n",
              strerror(errno));
        free(buf);
        return NULL;
    }

    switch(ver) {
        case CLIENT_VERSION_DC:
        case CLIENT_VERSION_GC:
            if(copy_dc_qst_dat(buf, buf2, sz, dsz)) {
                debug(DBG_WARN, "Error decoding qst \"%s\", see above.\n", fn);
                free(buf2);
                free(buf);
                return NULL;
            }

            break;

        case CLIENT_VERSION_PC:
            if(copy_pc_qst_dat(buf, buf2, sz, dsz)) {
                debug(DBG_WARN, "Error decoding qst \"%s\", see above.\n", fn);
                free(buf2);
                free(buf);
                return NULL;
            }

            break;

        case CLIENT_VERSION_BB:
            if(copy_bb_qst_dat(buf, buf2, sz, dsz)) {
                debug(DBG_WARN, "Error decoding qst \"%s\", see above.\n", fn);
                free(buf2);
                free(buf);
                return NULL;
            }

            break;

        default:
            free(buf2);
            free(buf);
            return NULL;
    }

    /* We're done with the first buffer, so clean it up. */
    free(buf);

    /* Return the dat decompressed. */
    rv = decompress_dat(buf2, (uint32_t)dsz, osz);
    free(buf2);
    return rv;
}

static const char *booma_names[3] = { "Booma", "Gobooma", "Gigobooma" };
static const char *shark_names[3] = { "Evil Shark", "Pal Shark", "Guil Shark" };
static const char *dimenian_names[3] = { "Dimenian", "La Dimenian",
                                         "So Dimenian" };

int parse_map(map_enemy_t *en, int en_ct, int ep, int alt, int *idx, int map) {
    int i, j, k = *idx;
    void *tmp;
    uint32_t count = 0;
    uint16_t n_clones;
    int acc, rt, bp, rtc = -1, bpc = -1;
    const char *name, *namec;

    printf("Enemies on Map %d\n", map);
    printf("Map Idx. | Global Idx. | PT/RT Idx. | BP Entry | Name\n");

    /* Parse each enemy. */
    for(i = 0; i < en_ct; ++i, ++k) {
        n_clones = en[i].num_clones;

        switch(en[i].base & 0xFFFF) {
            case 0x0040:    /* Hildebear & Hildeblue */
                acc = en[i].skin & 0x01;
                bp = 0x49 + acc;
                rt = 0x01 + acc;

                if(acc)
                    name = "Hildeblue";
                else
                    name = "Hildebear";
                break;

            case 0x0041:    /* Rappies */
                acc = en[i].skin & 0x01;
                if(ep == 3) {   /* Del Rappy & Sand Rappy */
                    if(alt) {
                        bp = 0x17 + acc;
                        rt = 0x11 + acc;
                        name = "Del Rappy";
                    }
                    else {
                        bp = 0x05 + acc;
                        rt = 0x11 + acc;
                        name = "Sand Rappy";
                    }
                }
                else {
                    if(acc) {
                        bp = 0x19;

                        if(ep == 1) {
                            rt = 0x06;
                            name = "Al Rappy";
                        }
                        else {
                            /* We need to fill this in when we make the lobby,
                               since it's dependent on the event. */
                            rt = 51;
                            name = "Love Rappy";
                        }
                    }
                    else {
                        bp = 0x18;
                        rt = 0x05;
                        name = "Rag Rappy";
                    }
                }
                break;

            case 0x0042:    /* Monest + 30 Mothmants */
                bp = 0x01;
                rt = 0x04;
                name = "Monest";

                n_clones = 30;
                bpc = 0x00;
                rtc = 0x03;
                namec = "Mothmant";
                break;

            case 0x0043:    /* Savage Wolf & Barbarous Wolf */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                bp = 0x02 + acc;
                rt = 0x07 + acc;

                if(acc)
                    name = "Barbarous Wolf";
                else
                    name = "Savage Wolf";
                break;

            case 0x0044:    /* Booma family */
                acc = en[i].skin % 3;
                bp = 0x4B + acc;
                rt = 0x09 + acc;
                name = booma_names[acc];
                break;

            case 0x0060:    /* Grass Assassin */
                bp = 0x4E;
                rt = 0x0C;
                name = "Grass Assassin";
                break;

            case 0x0061:    /* Del Lily, Poison Lily, Nar Lily */
                if(ep == 2 && alt) {
                    bp = 0x25;
                    rt = 0x53;
                    name = "Del Lily";
                }
                else {
                    acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                    bp = 0x04 + acc;
                    rt = 0x0D + acc;

                    if(acc)
                        name = "Nar Lily";
                    else
                        name = "Poison Lily";
                }
                break;

            case 0x0062:    /* Nano Dragon */
                bp = 0x1A;
                rt = 0x0E;
                name = "Nano Dragon";
                break;

            case 0x0063:    /* Shark Family */
                acc = en[i].skin % 3;
                bp = 0x4F + acc;
                rt = 0x10 + acc;
                name = shark_names[acc];
                break;

            case 0x0064:    /* Slime + 4 clones */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                bp = 0x30 - acc;
                rt = 0x13 + acc;

                if(acc) {
                    name = "Pouilly Slime";
                    namec = "Pouilly Slime (Clone)";
                }
                else {
                    name = "Pofuilly Slime";
                    namec = "Pofuilly Slime (Clone)";
                }

                n_clones = 4;
                bpc = 0x30;
                rtc = 0x13;
                break;

            case 0x0065:    /* Pan Arms, Migium, Hidoom */
                printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k++, 0x15, 0x31,
                       "Pan Arms");
                printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k++, 0x16, 0x32,
                       "Migium");
                printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k, 0x17, 0x33,
                       "Hidoom");
                continue;

            case 0x0080:    /* Dubchic & Gilchic */
                acc = en[i].skin & 0x01;
                bp = 0x1B + acc;
                rt = (0x18 + acc) << acc;

                if(acc)
                    name = "Gilchic";
                else
                    name = "Dubchic";
                break;

            case 0x0081:    /* Garanz */
                bp = 0x1D;
                rt = 0x19;
                name = "Garanz";
                break;

            case 0x0082:    /* Sinow Beat & Sinow Gold */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                if(acc) {
                    bp = 0x13;
                    rt = 0x1B;
                    name = "Sinow Gold";
                }
                else {
                    bp = 0x06;
                    rt = 0x1A;
                    name = "Sinow Beat";
                }

                if(!n_clones)
                    n_clones = 4;

                bpc = bp;
                rtc = rt;

                if(acc)
                    namec = "Sinow Gold (Clone)";
                else
                    namec = "Sinow Beat (Clone)";
                break;

            case 0x0083:    /* Canadine */
                bp = 0x07;
                rt = 0x1C;
                name = "Canadine";
                break;

            case 0x0084:    /* Canadine Group */
                bp = 0x09;
                rt = 0x1D;
                name = "Canane";

                n_clones = 8;
                bpc = 0x08;
                rtc = 0x1C;
                namec = "Canadine (Grouped)";
                break;

            case 0x0085:    /* Dubwitch */
                bp = -1;
                rt = -1;
                name = "Dubwitch";
                break;

            case 0x00A0:    /* Delsaber */
                bp = 0x52;
                rt = 0x1E;
                name = "Delsaber";
                break;

            case 0x00A1:    /* Chaos Sorcerer + 2 Bits */
                bp = 0x0A;
                rt = 0x1F;
                name = "Chaos Sorcerer";

                n_clones = 2;
                namec = "Bee";
                bpc = -1;
                rtc = -1;
                break;

            case 0x00A2:    /* Dark Gunner */
                bp = 0x1E;
                rt = 0x22;
                name = "Dark Gunner";
                break;

            case 0x00A3:    /* Death Gunner? */
                bp = -1;
                rt = -1;
                name = "Death Gunner";
                break;

            case 0x00A4:    /* Chaos Bringer */
                bp = 0x0D;
                rt = 0x24;
                name = "Chaos Bringer";
                break;

            case 0x00A5:    /* Dark Belra */
                bp = 0x0E;
                rt = 0x25;
                name = "Dark Belra";
                break;

            case 0x00A6:    /* Dimenian Family */
                acc = en[i].skin % 3;
                bp = 0x53 + acc;
                rt = 0x29 + acc;
                name = dimenian_names[acc];
                break;

            case 0x00A7:    /* Bulclaw + 4 Claws */
                bp = 0x1F;
                rt = 0x28;
                name = "Bulk";

                n_clones = 4;
                bpc = 0x20;
                rtc = 0x26;
                namec = "Claw";
                break;

            case 0x00A8:    /* Claw */
                bp = 0x20;
                rt = 0x26;
                name = "Claw";
                break;

            case 0x00C0:    /* Dragon or Gal Gryphon */
                if(ep == 1) {
                    bp = 0x12;
                    rt = 0x2C;
                    name = "Dragon";
                }
                else {
                    bp = 0x1E;
                    rt = 0x4D;
                    name = "Gal Gryphon";
                }
                break;

            case 0x00C1:    /* De Rol Le */
                bp = 0x0F;
                rt = 0x2D;
                name = "De Rol Le";
                break;

            case 0x00C2:    /* Vol Opt (form 1) */
                bp = -1;
                rt = -1;
                name = "Vol Opt (form 1)";
                break;

            case 0x00C5:    /* Vol Opt (form 2) */
                bp = 0x25;
                rt = 0x2E;
                name = "Vol Opt (form 2)";
                break;

            case 0x00C8:    /* Dark Falz (3 forms) + 510 Darvants */
                /* 510 Darvants come first. */
                for(j = 0; j < 510; ++j) {
                    printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k++, -1,
                           0x35, "Darvant");
                }

                /* Deal with all 3 forms of Falz himself. */
                printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k++, 0x2F, 0x38,
                       "Dark Falz (final form)");
                printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k++, 0x2F, 0x37,
                       "Dark Falz (second form)");
                printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k, 0x2F, 0x36,
                       "Dark Falz (first form)");
                continue;

            case 0x00CA:    /* Olga Flow */
                bp = 0x2C;
                rt = 0x4E;
                name = "Olga Flow";

                n_clones = 512;
                bpc = -1;
                rtc = -1;
                namec = "Olga Flow (Clone)";
                break;

            case 0x00CB:    /* Barba Ray */
                bp = 0x0F;
                rt = 0x49;
                name = "Barba Ray";

                n_clones = 47;
                bpc = -1;
                rtc = -1;
                namec = "Barba Ray (Clone)";
                break;

            case 0x00CC:    /* Gol Dragon */
                bp = 0x12;
                rt = 0x4C;
                name = "Gol Dragon";

                n_clones = 5;
                bpc = -1;
                rtc = -1;
                namec = "Gol Dragon (Clone)";
                break;

#if 0
            case 0x00D4:    /* Sinow Berill & Spigell */
                /* XXXX: How to do rare? Tethealla looks at skin, Newserv at the
                   reserved[10] value... */
                acc = en[i].skin >= 0x01 ? 1 : 0;
                if(acc) {
                    gen[count].bp_entry = 0x13;
                    gen[count].rt_index = 0x3F;
                }
                else {
                    gen[count].bp_entry = 0x06;
                    gen[count].rt_index = 0x3E;
                }

                count += 4; /* Add 4 clones which are never used... */
                break;

            case 0x00D5:    /* Merillia & Meriltas */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x4B + acc;
                gen[count].rt_index = 0x34 + acc;
                break;

            case 0x00D6:    /* Mericus, Merikle, or Mericarol */
                acc = en[i].skin % 3;
                if(acc)
                    gen[count].bp_entry = 0x44 + acc;
                else
                    gen[count].bp_entry = 0x3A;

                gen[count].rt_index = 0x38 + acc;
                break;

            case 0x00D7:    /* Ul Gibbon & Zol Gibbon */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x3B + acc;
                gen[count].rt_index = 0x3B + acc;
                break;

            case 0x00D8:    /* Gibbles */
                gen[count].bp_entry = 0x3D;
                gen[count].rt_index = 0x3D;
                break;

            case 0x00D9:    /* Gee */
                gen[count].bp_entry = 0x07;
                gen[count].rt_index = 0x36;
                break;

            case 0x00DA:    /* Gi Gue */
                gen[count].bp_entry = 0x1A;
                gen[count].rt_index = 0x37;
                break;

            case 0x00DB:    /* Deldepth */
                gen[count].bp_entry = 0x30;
                gen[count].rt_index = 0x47;
                break;

            case 0x00DC:    /* Delbiter */
                gen[count].bp_entry = 0x0D;
                gen[count].rt_index = 0x48;
                break;

            case 0x00DD:    /* Dolmolm & Dolmdarl */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x4F + acc;
                gen[count].rt_index = 0x40 + acc;
                break;

            case 0x00DE:    /* Morfos */
                gen[count].bp_entry = 0x41;
                gen[count].rt_index = 0x42;
                break;

            case 0x00DF:    /* Recobox & Recons */
                gen[count].bp_entry = 0x41;
                gen[count].rt_index = 0x43;

                for(j = 1; j <= n_clones; ++j) {
                    gen[++count].bp_entry = 0x42;
                    gen[count].rt_index = 0x44;
                }

                /* Don't double count them. */
                n_clones = 0;
                break;

            case 0x00E0:    /* Epsilon, Sinow Zoa & Zele */
                if(ep == 2 && alt) {
                    gen[count].bp_entry = 0x23;
                    gen[count].rt_index = 0x54;
                    count += 4;
                }
                else {
                    acc = en[i].skin & 0x01;
                    gen[count].bp_entry = 0x43 + acc;
                    gen[count].rt_index = 0x45 + acc;
                }
                break;

            case 0x00E1:    /* Ill Gill */
                gen[count].bp_entry = 0x26;
                gen[count].rt_index = 0x52;
                break;

            case 0x0110:    /* Astark */
                gen[count].bp_entry = 0x09;
                gen[count].rt_index = 0x01;
                break;

            case 0x0111:    /* Satellite Lizard & Yowie */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                if(alt)
                    gen[count].bp_entry = 0x0D + acc + 0x10;
                else
                    gen[count].bp_entry = 0x0D + acc;

                gen[count].rt_index = 0x02 + acc;
                break;

            case 0x0112:    /* Merissa A/AA */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x19 + acc;
                gen[count].rt_index = 0x04 + acc;
                break;

            case 0x0113:    /* Girtablulu */
                gen[count].bp_entry = 0x1F;
                gen[count].rt_index = 0x06;
                break;

            case 0x0114:    /* Zu & Pazuzu */
                acc = en[i].skin & 0x01;
                if(alt)
                    gen[count].bp_entry = 0x07 + acc + 0x14;
                else
                    gen[count].bp_entry = 0x07 + acc;

                gen[count].rt_index = 7 + acc;
                break;

            case 0x0115:    /* Boota Family */
                acc = en[i].skin % 3;
                gen[count].rt_index = 0x09 + acc;
                if(en[i].skin & 0x02)
                    gen[count].bp_entry = 0x03;
                else
                    gen[count].bp_entry = 0x00 + acc;
                break;

            case 0x0116:    /* Dorphon & Eclair */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x0F + acc;
                gen[count].rt_index = 0x0C + acc;
                break;

            case 0x0117:    /* Goran Family */
                acc = en[i].skin % 3;
                gen[count].bp_entry = 0x11 + acc;
                if(en[i].skin & 0x02)
                    gen[count].rt_index = 0x0F;
                else if(en[i].skin & 0x01)
                    gen[count].rt_index = 0x10;
                else
                    gen[count].rt_index = 0x0E;
                break;

            case 0x119: /* Saint Million, Shambertin, & Kondrieu */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x22;
                if(en[i].reserved[10] & 0x800000)
                    gen[count].rt_index = 0x15;
                else
                    gen[count].rt_index = 0x13 + acc;
                break;
#endif

            default:
                if((en[i].base & 0xFFFF) < 0x40) {
                    rt = -1;
                    bp = -1;
                    name = "NPC";
                }
                else {
                    debug(DBG_WARN, "Unknown enemy ID: %04X\n", en[i].base);
                    debug(DBG_WARN, "Everything after this point may be "
                          "completely wrong.\n");
                }
        }

        printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k, rt, bp, name);

        /* Increment the counter, as needed */
        if(n_clones) {
            if(bpc == -1) {
                rtc = rt;
                bpc = bp;
                namec = NULL;
            }

            ++k;

            for(j = 0; j < n_clones; ++j, ++k) {
                if(namec)
                    printf("%-8d   %-11d   %-10d   %-8d   %s\n", i, k, rtc, bpc,
                           namec);
                else
                    printf("%-8d   %-11d   %-10d   %-8d   %s (Clone)\n", i, k,
                           rtc, bpc, name);
            }

            --k;
        }
    }

    *idx = k;
    printf("\n\n");

    return 0;
}

void parse_quest_objects(const uint8_t *data, uint32_t len, uint32_t *obj_cnt,
                         const quest_dat_hdr_t *ptrs[2][18]) {
    const quest_dat_hdr_t *hdr = (const quest_dat_hdr_t *)data;
    uint32_t ptr = 0;
    uint32_t obj_count = 0;

    while(ptr < len) {
        switch(LE32(hdr->obj_type)) {
            case 0x01:                      /* Objects */
                ptrs[0][LE32(hdr->area)] = hdr;
                obj_count += LE32(hdr->size) / sizeof(map_object_t);
                ptr += hdr->next_hdr;
                hdr = (const quest_dat_hdr_t *)(data + ptr);
                break;

            case 0x02:                      /* Enemies */
                ptrs[1][LE32(hdr->area)] = hdr;
                ptr += hdr->next_hdr;
                hdr = (const quest_dat_hdr_t *)(data + ptr);
                break;

            case 0x03:                      /* ??? - Skip */
                ptr += hdr->next_hdr;
                hdr = (const quest_dat_hdr_t *)(data + ptr);
                break;

            default:
                /* Padding at the end of the file... */
                ptr = len;
                break;
        }
    }

    *obj_cnt = obj_count;
}
