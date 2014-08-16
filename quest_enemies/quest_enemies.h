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

#ifndef QUEST_ENEMIES_H
#define QUEST_ENEMIES_H

#include <stdint.h>

#define CLIENT_VERSION_DC       0
#define CLIENT_VERSION_PC       1
#define CLIENT_VERSION_GC       2
#define CLIENT_VERSION_BB       3

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* Header for sections of the .dat files for quests. */
typedef struct quest_dat_hdr {
    uint32_t obj_type;
    uint32_t next_hdr;
    uint32_t area;
    uint32_t size;
    uint8_t data[];
} PACKED quest_dat_hdr_t;

/* Enemy data in the map files. This the same as the ENEMY_ENTRY struct from
   newserv. */
typedef struct map_enemy {
    uint32_t base;
    uint16_t reserved0;
    uint16_t num_clones;
    uint32_t reserved[11];
    uint32_t reserved12;
    uint32_t reserved13;
    uint32_t reserved14;
    uint32_t skin;
    uint32_t reserved15;
} PACKED map_enemy_t;

/* Object data in the map object files. */
typedef struct map_object {
    uint32_t skin;
    uint32_t unk1;
    uint32_t unk2;
    uint32_t obj_id;
    float x;
    float y;
    float z;
    uint32_t rpl;
    uint32_t rotation;
    uint32_t unk3;
    uint32_t unk4;
    /* Everything beyond this point depends on the object type. */
    union {
        float sp[6];
        uint32_t dword[6];
    };
} PACKED map_object_t;

#undef PACKED

/* In quests.c */
int is_qst(const char *fn, int ver);
uint8_t *read_dat(const char *fn, uint32_t *osz, int comp);
uint8_t *read_qst(const char *fn, uint32_t *osz, int ver);

int parse_map(map_enemy_t *en, int en_ct, int ep, int alt, int *idx, int map);
void parse_quest_objects(const uint8_t *data, uint32_t len, uint32_t *obj_cnt,
                         const quest_dat_hdr_t *ptrs[2][17]);

#endif /* !QUEST_ENEMIES_H */
