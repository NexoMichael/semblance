/*
 * Entry point of the "dump" program
 *
 * Copyright 2017-2018 Zebediah Figura
 *
 * This file is part of Semblance.
 *
 * Semblance is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Semblance is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Semblance; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <string.h>
#include <getopt.h>

#include "semblance.h"

static void dump_file(char *file){
    word magic;
    long offset = 0;

    f = fopen(file, "r");
    if (!f) {
        perror("Cannot open %s");
        return;
    }

    magic = read_word();

    printf("File: %s\n", file);
    if (magic == 0x5a4d){ /* MZ */
        fseek(f, 0x3c, SEEK_SET);
        offset = read_dword();
        fseek(f, offset, SEEK_SET);
        magic = read_word();

        if (magic == 0x4550)
            dumppe(offset);
        else if (magic == 0x454e)
            dumpne(offset);
        else
            dumpmz();
    } else
        fprintf(stderr, "File format not recognized\n");

    fclose(f);
    fflush(stdout);
    return;
}

static const char help_message[] =
"dump: tool to disassemble and print information from executable files.\n"
"Usage: dump [options] <file(s)>\n"
"Available options:\n"
"\t-a, --resource                       Print embedded resources.\n"
"\t-c, --compilable                     Produce output that can be compiled.\n"
"\t-C, --demangle                       Demangle C++ function names.\n"
"\t-d, --disassemble                    Print disassembled machine code.\n"
"\t-e, --exports                        Print exported functions.\n"
"\t-f, --file-headers                   Print contents of the file header.\n"
"\t-h, --help                           Display this help message.\n"
"\t-i, --imports                        Print imported modules.\n"
"\t-M, --disassembler-options=[...]     Extended options for disassembly.\n"
"\t\tatt        Alias for `gas'.\n"
"\t\tgas        Use GAS syntax for disassembly.\n"
"\t\tintel      Alias for `masm'.\n"
"\t\tmasm       Use MASM syntax for disassembly.\n"
"\t\tnasm       Use NASM syntax for disassembly.\n"
"\t-o, --specfile                       Create a specfile from exports.\n"
"\t-s, --full-contents                  Display full contents of all sections.\n"
"\t-v, --version                        Print the version number of semblance.\n"
"\t-x, --all-headers                    Print all headers.\n"
"\t--no-show-addresses                  Don't print instruction addresses.\n"
"\t--no-show-raw-insn                   Don't print raw instruction hex code.\n"
;

static const struct option long_options[] = {
    {"resource",                optional_argument,  NULL, 'a'},
    {"compilable",              no_argument,        NULL, 'c'},
    {"demangle",                no_argument,        NULL, 'C'},
    {"disassemble",             no_argument,        NULL, 'd'},
    {"disassemble-all",         no_argument,        NULL, 'D'},
    {"exports",                 no_argument,        NULL, 'e'},
    {"file-headers",            no_argument,        NULL, 'f'},
//  {"gas",                     no_argument,        NULL, 'G'},
    {"help",                    no_argument,        NULL, 'h'},
    {"imports",                 no_argument,        NULL, 'i'},
//  {"masm",                    no_argument,        NULL, 'I'}, /* for "Intel" */
    {"disassembler-options",    required_argument,  NULL, 'M'},
//  {"nasm",                    no_argument,        NULL, 'N'},
    {"specfile",                no_argument,        NULL, 'o'},
    {"full-contents",           no_argument,        NULL, 's'},
    {"version",                 no_argument,        NULL, 'v'},
    {"all-headers",             no_argument,        NULL, 'x'},
    {"no-show-raw-insn",        no_argument,        NULL, NO_SHOW_RAW_INSN},
    {"no-prefix-addresses",     no_argument,        NULL, NO_SHOW_ADDRESSES},
    {0}
};

int main(int argc, char *argv[]){
    int opt;

    mode = 0;
    opts = 0;
    asm_syntax = NASM;

    while ((opt = getopt_long(argc, argv, "a::cCdDefhiM:osvx", long_options, NULL)) >= 0){
        switch (opt) {
        case NO_SHOW_RAW_INSN:
            opts |= NO_SHOW_RAW_INSN;
            break;
        case NO_SHOW_ADDRESSES:
            opts |= NO_SHOW_ADDRESSES;
            break;
        case 'a': /* dump resources only */
        {
            int ret;
            char type[256];
            unsigned i;
            mode |= DUMPRSRC;
            if (optarg){
                if (resource_count == MAXARGS){
                    fprintf(stderr, "Too many resources specified\n");
                    return 1;
                }
                if (0 >= (ret = sscanf(optarg, "%s %hu", type, &resource_id[resource_count])))
                    /* empty argument, so do nothing */
                    break;
                if (ret == 1)
                    resource_id[resource_count] = 0;

                /* todo(?): let the user specify string [exe-defined] types, and also
                 * string id names */
                if (!sscanf(type, "%hu", &resource_type[resource_count])){
                    resource_type[resource_count] = 0;
                    for (i=1;i<rsrc_types_count;i++){
                        if(rsrc_types[i] && !strcasecmp(rsrc_types[i], type)){
                            resource_type[resource_count] = 0x8000|i;
                            break;
                        }
                    }
                    if(!resource_type[resource_count]){
                        fprintf(stderr, "Unrecognized resource type '%s'\n", type);
                        return 1;
                    }
                }
                resource_count++;
            }
            break;
        }
        case 'c': /* compilable */
            opts |= COMPILABLE|NO_SHOW_ADDRESSES|NO_SHOW_RAW_INSN;
            break;
        case 'C': /* demangle */
            opts |= DEMANGLE;
            break;
        case 'd': /* disassemble only */
            mode |= DISASSEMBLE;
            break;
        case 'D': /* disassemble all */
            opts |= DISASSEMBLE_ALL;
            break;
        case 'e': /* exports */
            mode |= DUMPEXPORT;
            break;
        case 'f': /* dump header only */
            mode |= DUMPHEADER;
            break;
        case 'h': /* help */
            printf(help_message);
            return 0;
        case 'i': /* imports */
            /* FIXME: should also list imported functions (?) */
            mode |= DUMPIMPORTMOD;
            break;
        case 'M': /* additional options */
            if (!strcmp(optarg, "att") || !strcmp(optarg, "gas"))
                asm_syntax = GAS;
            else if (!strcmp(optarg, "intel") || !strcmp(optarg, "masm"))
                asm_syntax = MASM;
            else if (!strcmp(optarg, "nasm"))
                asm_syntax = NASM;
            else {
                fprintf(stderr, "Unrecognized disassembly option `%s'.\n", optarg);
                return 1;
            }
            break;
        case 'o': /* make a specfile */
            mode = SPECFILE;
            break;
        case 'v': /* version */
            printf("semblance version " VERSION "\n");
            return 0;
        case 's': /* full contents */
            opts |= FULL_CONTENTS;
            break;
        case 'x': /* all headers */
            mode |= DUMPHEADER | DUMPEXPORT | DUMPIMPORTMOD;
        default: /* '?' */
            fprintf(stderr, "Usage: dumpne [options] <file>\n");
            return 1;
        }
    }

    if (mode == 0)
        mode = ~0;

    if (optind == argc)
        printf(help_message);

    while (optind < argc){
        dump_file(argv[optind++]);
        if (optind < argc)
            printf("\n\n");
    }

    return 0;
}
