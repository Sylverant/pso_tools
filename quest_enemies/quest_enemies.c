/*
    Sylverant PSO Tools
    Quest Enemy Parser
    Copyright (C) 2012, 2013, 2014 Lawrence Sebald

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
#include <stdint.h>
#include <string.h>

#include "quest_enemies.h"
#include "packets.h"

static int version = CLIENT_VERSION_DC;
static int episode = 1;
static int compressed = 1;
static const char *filename;

/* Print information about this program to stdout. */
static void print_program_info(void) {
#if defined(VERSION)
    printf("Sylverant Quest Enemy Parser version %s\n", VERSION);
#elif defined(SVN_REVISION)
    printf("Sylverant Quest Enemy Parser SVN revision: %s\n", SVN_REVISION);
#else
    printf("Sylverant Quest Enemy Parser\n");
#endif
    printf("Copyright (C) 2012, 2013, 2014 Lawrence Sebald\n\n");
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
    printf("Usage: %s [arguments] quest_file\n"
           "-----------------------------------------------------------------\n"
           "--help          Print this help and exit\n"
           "--version       Print version info and exit\n"
           "--uncompressed  The .dat file specified is uncompressed. This\n"
           "                option is ignored when parsing a .qst file.\n"
           "--dc            Quest specified is for Dreamcast\n"
           "--pc            Quest specified is for PSO for PC\n"
           "--gc            Quest specified is for Gamecube\n"
           "--bb            Quest specified is for PSO Blue Burst\n"
           "--ep1           Quest specified is for Episode I\n"
           "--ep2           Quest specified is for Episode II\n\n"
           "If an episode is not specified, the quest is assumed to be for\n"
           "Episode I.\n"
           "If a version of the game is not specified, the quest is assumed\n"
           "to be for the Dreamcast version of the game.\n\n"
           "The quest file can be a Schtserv-style .qst file, a PRS\n"
           "compressed .dat file from the quest, or an uncompressed .dat\n"
           "file. If using an uncompressed .dat file, make sure to specify\n"
           "the relevant command line option to ensure the file is parsed\n"
           "correctly.\n", bin);
}

/* Parse any command-line arguments passed in. */
static void parse_command_line(int argc, char *argv[]) {
    int i;

    if(argc < 2) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    for(i = 1; i < argc - 1; ++i) {
        if(!strcmp(argv[i], "--version")) {
            print_program_info();
            exit(EXIT_SUCCESS);
        }
        else if(!strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else if(!strcmp(argv[i], "--dc")) {
            version = CLIENT_VERSION_DC;
        }
        else if(!strcmp(argv[i], "--pc")) {
            version = CLIENT_VERSION_PC;
        }
        else if(!strcmp(argv[i], "--gc")) {
            version = CLIENT_VERSION_GC;
        }
        else if(!strcmp(argv[i], "--bb")) {
            version = CLIENT_VERSION_BB;
        }
        else if(!strcmp(argv[i], "--ep1")) {
            episode = 1;
        }
        else if(!strcmp(argv[i], "--ep2")) {
            episode = 2;
        }
        else if(!strcmp(argv[i], "--uncompressed")) {
            compressed = 0;
        }
        else {
            printf("Illegal command line argument: %s\n", argv[i]);
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Save the file we'll be working with. */
    filename = argv[argc - 1];
}

int main(int argc, char *argv[]) {
    uint8_t *dat = NULL;
    uint32_t sz, ocnt, area;
    int alt, idx = 0, i, type = 0;
    const quest_dat_hdr_t *ptrs[2][18] = { { 0 } };
    const quest_dat_hdr_t *hdr;

    /* Parse the command line... */
    parse_command_line(argc, argv);

    /* See if we got a .qst file a .dat file. */
    type = is_qst(filename, version);

    if(type == 1) {
        dat = read_qst(filename, &sz, version);
    }
    else if(!type) {
        dat = read_dat(filename, &sz, compressed);
    }

    if(!dat) {
        printf("Confused by earlier errors, bailing out.\n");
        return -1;
    }

    parse_quest_objects(dat, sz, &ocnt, ptrs);
    printf("Found %d objects\n", (int)ocnt);

    for(i = 0; i < 18; ++i) {
        if((hdr = ptrs[1][i])) {
            /* XXXX: Ugly! */
            sz = LE32(hdr->size);
            area = LE32(hdr->area);
            alt = 0;

            if((episode == 3 && area > 5) || (episode == 2 && area > 15))
                alt = 1;

            if(parse_map((map_enemy_t *)(hdr->data), sz / sizeof(map_enemy_t),
                         episode, alt, &idx, (int)area)) {
                printf("Cannot parse map!\n");
                return -4;
            }
        }
    }

    return 0;
}
