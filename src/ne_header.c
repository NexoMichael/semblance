/*
 * Functions for parsing the NE header
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
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>

#include "semblance.h"
#include "ne.h"

#pragma pack(1)

struct header_ne {
    word  ne_magic;             /* 00 NE signature 'NE' */
    byte  ne_ver;               /* 02 Linker version number */
    byte  ne_rev;               /* 03 Linker revision number */
    word  ne_enttab;            /* 04 Offset to entry table relative to NE */
    word  ne_cbenttab;          /* 06 Length of entry table in bytes */
    dword ne_crc;               /* 08 Checksum */
    word  ne_flags;             /* 0c Flags about segments in this file */
    byte  ne_autodata;          /* 0e Automatic data segment number */
    byte  ne_unused;            /* 0f */
    word  ne_heap;              /* 10 Initial size of local heap */
    word  ne_stack;             /* 12 Initial size of stack */
    word  ne_ip;                /* 14 Initial IP */
    word  ne_cs;                /* 16 Initial CS */
    word  ne_sp;                /* 18 Initial SP */
    word  ne_ss;                /* 1a Initial SS */
    word  ne_cseg;              /* 1c # of entries in segment table */
    word  ne_cmod;              /* 1e # of entries in module reference tab. */
    word  ne_cbnrestab;         /* 20 Length of nonresident-name table */
    word  ne_segtab;            /* 22 Offset to segment table */
    word  ne_rsrctab;           /* 24 Offset to resource table */
    word  ne_restab;            /* 26 Offset to resident-name table */
    word  ne_modtab;            /* 28 Offset to module reference table */
    word  ne_imptab;            /* 2a Offset to imported name table */
    dword ne_nrestab;           /* 2c Offset to nonresident-name table */
    word  ne_cmovent;           /* 30 # of movable entry points */
    word  ne_align;             /* 32 Logical sector alignment shift count */
    word  ne_cres;              /* 34 # of resource segments */
    byte  ne_exetyp;            /* 36 Flags indicating target OS */
    byte  ne_flagsothers;       /* 37 Additional information flags */
    word  ne_pretthunks;        /* 38 Offset to return thunks */
    word  ne_psegrefbytes;      /* 3a Offset to segment ref. bytes */
    word  ne_swaparea;          /* 3c Reserved by Microsoft */
    byte  ne_expver_min;        /* 3e Expected Windows version number (minor) */
    byte  ne_expver_maj;        /* 3f Expected Windows version number (major) */
};

STATIC_ASSERT(sizeof(struct header_ne) == 0x40);

static void print_flags(word flags){
    char buffer[1024];
    
    if      ((flags & 0x0003) == 0) strcpy(buffer, "no DGROUP");
    else if ((flags & 0x0003) == 1) strcpy(buffer, "single DGROUP");
    else if ((flags & 0x0003) == 2) strcpy(buffer, "multiple DGROUPs");
    else if ((flags & 0x0003) == 3) strcpy(buffer, "(unknown DGROUP type 3)");
    if (flags & 0x0004) strcat(buffer, ", global initialization");
    if (flags & 0x0008) strcat(buffer, ", protected mode only");
    if (flags & 0x0010) strcat(buffer, ", 8086");
    if (flags & 0x0020) strcat(buffer, ", 80286");
    if (flags & 0x0040) strcat(buffer, ", 80386");
    if (flags & 0x0080) strcat(buffer, ", 80x87");
    if      ((flags & 0x0700) == 0x0100) strcat(buffer, ", fullscreen"); /* FRAMEBUF */
    else if ((flags & 0x0700) == 0x0200) strcat(buffer, ", console"); /* API compatible */
    else if ((flags & 0x0700) == 0x0300) strcat(buffer, ", GUI"); /* uses API */
    else if ((flags & 0x0700) == 0)      strcat(buffer, ", (no subsystem)"); /* none? */
    else sprintf(buffer+strlen(buffer), ", (unknown application type %d)", (flags & 0x0700) >> 8);
    if (flags & 0x0800) strcat(buffer, ", self-loading"); /* OS/2 family */
    if (flags & 0x1000) strcat(buffer, ", (unknown flag 0x1000)");
    if (flags & 0x2000) strcat(buffer, ", contains linker errors");
    if (flags & 0x4000) strcat(buffer, ", non-conforming program");
    if (flags & 0x8000) strcat(buffer, ", library");
    
    printf("Flags: 0x%04x (%s)\n", flags, buffer);
}

static void print_os2flags(word flags){
    char buffer[1024];

    buffer[0] = 0;
    if (flags & 0x0001) strcat(buffer, ", long filename support");
    if (flags & 0x0002) strcat(buffer, ", 2.x protected mode");
    if (flags & 0x0004) strcat(buffer, ", 2.x proportional fonts");
    if (flags & 0x0008) strcat(buffer, ", fast-load area"); /* gangload */
    if (flags & 0xfff0)
        sprintf(buffer+strlen(buffer), ", (unknown flags 0x%04x)", flags & 0xfff0);

    if(buffer[0])
        printf("OS/2 flags: 0x%04x (%s)\n", flags, buffer+2);
    else
        printf("OS/2 flags: 0x0000\n");
}

static const char *const exetypes[] = {
    "unknown",                  /* 0 */
    "OS/2",                     /* 1 */
    "Windows (16-bit)",         /* 2 */
    "European Dos 4.x",         /* 3 */
    "Windows 386 (32-bit)",     /* 4 */
    "BOSS",                     /* 5 */
    0
};

static void print_header(struct header_ne *header){
    /* Still need to deal with:
     *
     * 34 - number of resource segments (all of my testcases return 0)
     * 38 - offset to return thunks (have testcases)
     * 3a - offset to segment ref. bytes (same)
     */

    printf("Linker version: %d.%d\n", header->ne_ver, header->ne_rev); /* 02 */
    printf("Checksum: %08x\n", header->ne_crc); /* 08 */
    print_flags(header->ne_flags); /* 0c */
    printf("Automatic data segment: %d\n", header->ne_autodata);
    if (header->ne_unused != 0)
        warn("Header byte at position 0f has value 0x%02x.\n", header->ne_unused);
    printf("Heap size: %d bytes\n", header->ne_heap); /* 10 */
    printf("Stack size: %d bytes\n", header->ne_stack); /* 12 */
    printf("Program entry point: %d:%04x\n", header->ne_cs, header->ne_ip); /* 14 */
    printf("Initial stack location: %d:%04x\n", header->ne_ss, header->ne_sp); /* 18 */
    if (header->ne_exetyp <= 5) /* 36 */
        printf("Target OS: %s\n", exetypes[header->ne_exetyp]);
    else
        printf("Target OS: (unknown value %d)\n", header->ne_exetyp);
    print_os2flags(header->ne_flagsothers); /* 37 */
    printf("Swap area: %d\n", header->ne_swaparea); /* 3c */
    printf("Expected Windows version: %d.%d\n", /* 3e */
           header->ne_expver_maj, header->ne_expver_min);
    printf("\n");
}

static void print_export(entry *entry_table, int count) {
    int i;

    for (i = 0; i < count; i++)
        if (entry_table[i].segment == 0xfe)
            /* absolute value */
            printf("\t%5d\t   %04x\t%s\n", i+1, entry_table[i].offset, entry_table[i].name ? entry_table[i].name : "<no name>");
        else if (entry_table[i].segment)
            printf("\t%5d\t%2d:%04x\t%s\n", i+1, entry_table[i].segment,
                entry_table[i].offset, entry_table[i].name ? entry_table[i].name : "<no name>");
    putchar('\n');
}

static void print_specfile(char *module_name, entry *entry_table, int count) {
    int i;
    FILE *specfile;
    char spec_name[13];
    sprintf(spec_name, "%.8s.ORD", module_name);
    specfile = fopen(spec_name, "w");
    if (!specfile) {
	perror("Couldn't open %s");
	return;
    }

    fprintf(specfile, "# Generated by dumpne -o\n");
    for (i = 0; i < count; i++) {
        if (entry_table[i].name)
            fprintf(specfile, "%d\t%s\n", i+1, entry_table[i].name);
        else if (entry_table[i].segment)
            fprintf(specfile, "%d\n", i+1);
    }

    fclose(specfile);
}

static int demangle_protection(char *buffer, char *start, char *prot, char *func) {
    if (*start >= 'A' && *start <= 'V') {
        if ((*start-'A') & 2)
            strcat(buffer, "static ");
        if ((*start-'A') & 4)
            strcat(buffer, "virtual ");
        if (!((*start-'A') & 1))
            strcat(buffer, "near ");
        if (((*start-'A') & 24) == 0)
            strcat(buffer, "private ");
        else if (((*start-'A') & 24) == 8)
            strcat(buffer, "protected ");
        else if (((*start-'A') & 24) == 16)
            strcat(buffer, "public ");
        *prot = *start;
    } else if (*start == 'Y') {
        strcat(buffer, "near ");
    } else if (*start == 'Z') {
        /* normally we'd mark far and not near, but most functions which
         * are going to have an exported name will be far. */
    } else if (*start == 'X') {
        /* It's not clear what this means, but it always seems to be
         * followed by either a number, or a string of text and then @. */
        *prot = 'V'; /* just pretend that for now */
        if (start[1] >= '0' && start[1] <= '9') {
            strcat(buffer, "(X0) ");
            buffer[strlen(buffer)-3] = start[1];
            return 2;
        } else {
            return (strchr(start, '@')+1)-start;
        }
    } else if (*start == '_' && start[1] != '$') {
#if 1
        /* Same as above, but there is an extra character first (which
         * is often V, so is likely to be the protection/etc), and then
         * a number (often 7 or 3). */
        demangle_protection(buffer, start+1, prot, func);
        if (start[3] >= '0' && start[3] <= '9') {
            strcat(buffer, "(_00) ");
            buffer[strlen(buffer)-4] = start[2];
            buffer[strlen(buffer)-3] = start[3];
            return 4;
        } else {
            return (strchr(start, '@')+1)-start;
        }
#else
        return 0;
#endif
    } else {
        warn("Unknown modifier %c for function %s\n", *start, func);
        return 0;
    }
    return 1;
}

static const char *int_types[] = {
    "signed char",      /* C */
    "char",             /* D */
    "unsigned char",    /* E */
    "short",            /* F */
    "unsigned short",   /* G */
    "int",              /* H */
    "unsigned int",     /* I */
    "long",             /* J */
    "unsigned long",    /* K */
};

/* Returns the number of characters processed. */
static int demangle_type(char *buffer, char *type) {
    if (*type >= 'C' && *type <= 'K') {
        strcat(buffer, int_types[*type-'C']);
        strcat(buffer, " ");
        return 1;
    }

    switch (*type) {
    case 'A':
    case 'P':
    {
        int ret;
        if ((type[1]-'A') & 1)
            strcat(buffer, "const ");
        if ((type[1]-'A') & 2)
            strcat(buffer, "volatile ");
        ret = demangle_type(buffer, type+2);
        if (!((type[1]-'A') & 4))
            strcat(buffer, "near ");
        strcat(buffer, (*type == 'A') ? "&" : "*");
        return ret+2;
    }
    case 'M': strcat(buffer, "float "); return 1;
    case 'N': strcat(buffer, "double "); return 1;
    case 'U':
    case 'V':
    {
        /* These represent structs (U) or types (V), but the name given
         * doesn't seem to need a qualifier. */
        char *end = strstr(type, "@@");
        if (!end) {
            /* something can go between the at signs, but what does it mean? */
            end = strchr(type, '@')+1;
            end = strchr(type, '@');
        }
        strncat(buffer, type+1, end-(type+1));
        strcat(buffer, " ");
        return end-type;
    }
    case 'X': strcat(buffer, "void "); return 1;
    default: return 0;
    }
}

/* Demangle a C++ function name. The scheme used seems to be mostly older
 * than any documented, but I was able to find documentation that is at
 * least close in Agner Fog's manual. */
static char *demangle(char *func) {
    char buffer[1024];
    char *p, *start, *end;
    char prot = 0;
    int len;

    /* Figure out the modifiers and calling convention. */
    buffer[0] = 0;
    p = strstr(func, "@@") + 2;
    len = demangle_protection(buffer, p, &prot, func);
    if (!len) {
        return func;
    }
    p += len;

    /* The next one seems to always be E or F. No idea why. */
    if (prot >= 'A' && prot <= 'V' && !((prot-'A') & 2)) {
        if (*p != 'E' && *p != 'F')
            warn("Unknown modifier %c for function %s\n", *p, func);
        p++;
    }

    /* This should mark the calling convention. Always seems to be A,
     * but this corroborates the function body which uses CDECL. */
    if (*p == 'A'); /* strcat(buffer, "__cdecl "); */
    else if (*p == 'C') strcat(buffer, "__pascal ");
    else warn("Unknown calling convention %c for function %s\n", *p, func);

    /* This marks the return value. */
    p++;
    len = demangle_type(buffer, p);
    if (!len) {
        warn("Unknown return type %c for function %s\n", *p, func);
        len = 1;
    }
    p += len;

    /* Get the classname. This is in reverse order, so
     * find the first @@ and work backwards from there. */
    start = end = strstr(func, "@@");
    while (1) {
        start--;
        while (*start != '?' && *start != '@') start--;
        strncat(buffer, start+1, end-(start+1));
        if (*start == '?') break;
        strcat(buffer, "::");
        end = start;
    }

    /* Print the arguments. */
    if (*p == 'X') {
        strcat(buffer, "(void)");
    } else {
        strcat(buffer, "(");
        while (*p != '@') {
            len = demangle_type(buffer, p);
            if (!len) {
                warn("Unknown argument type %c for function %s\n", *p, func);
                len = 1;
            }
            if (buffer[strlen(buffer)-1] == ' ')
                buffer[strlen(buffer)-1] = 0;
            p += len;
            strcat(buffer, ", ");
        }
        buffer[strlen(buffer)-2] = ')';
        buffer[strlen(buffer)-1] = 0;
    }

    func = realloc(func, (strlen(buffer)+1)*sizeof(char));
    strcpy(func, buffer);
    return func;
}

/* return the first entry (module name/desc) */
static char *read_res_name_table(long start, entry *entry_table){
    /* reads (non)resident names into our entry table */
    byte length;
    char *first;
    char *name;
    word ordinal;

    fseek(f, start, SEEK_SET);

    length = read_byte();
    first = malloc((length+1)*sizeof(char));
    fread(first, sizeof(char), length, f);
    first[length] = 0;
    fseek(f, sizeof(word), SEEK_CUR);

    while ((length = read_byte())){
        name = malloc((length+1)*sizeof(char));
        fread(name, sizeof(char), length, f);
        name[length] = 0;   /* null term */
        ordinal = read_word();

        if ((opts & DEMANGLE) && name[0] == '?')
            name = demangle(name);

        entry_table[ordinal-1].name = name;
    }

    return first;
}

static unsigned get_entry_table(long start, entry **table) {
    byte length, index;
    int count = 0;
    entry *ret = NULL;
    unsigned i;
    word w;

    /* get a count */
    fseek(f, start, SEEK_SET);
    while ((length = read_byte())) {
        index = read_byte();
        count += length;
        if (index != 0)
            fseek(f, ((index == 0xff) ? 6 : 3) * length, SEEK_CUR);
    }
    ret = calloc(sizeof(entry), count);

    fseek(f, start, SEEK_SET);
    count = 0;
    while ((length = read_byte())) {
        index = read_byte();
        for (i = 0; i < length; i++) {
            if (index == 0xff) {
                ret[count].flags = read_byte();
                if ((w = read_word()) != 0x3fcd)
                    warn("Entry %d has interrupt bytes %02x %02x (expected 3f cd).\n", count+1, w & 0xff, w >> 16);
                ret[count].segment = read_byte();
                ret[count].offset = read_word();
            } else if (index == 0x00) {
                /* no entries, just here to skip ordinals */
            } else {
                ret[count].flags = read_byte();
                ret[count].segment = index;
                ret[count].offset = read_word();
            }
            count++;
        }
    }

    *table = ret;
    return count;
}

static void load_exports(import_module *module) {
    FILE *specfile;
    char spec_name[18];
    char line[300], *p;
    int count;
    word ordinal;

    sprintf(spec_name, "%.8s.ORD", module->name);
    specfile = fopen(spec_name, "r");
    if (!specfile) {
        sprintf(spec_name, "spec/%.8s.ORD", module->name);
        specfile = fopen(spec_name, "r");
        if (!specfile) {
            fprintf(stderr, "Note: couldn't find specfile for module %s; exported names won't be given.\n", module->name);
            fprintf(stderr, "      To create a specfile, run `dumpne -o <module.dll>'.\n");
            module->exports = NULL;
            module->export_count = 0;
            return;
        }
    }

    /* first grab a count */
    count = 0;
    while (fgets(line, sizeof(line), specfile)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        count++;
    }

    module->exports = malloc(count * sizeof(export));

    fseek(specfile, 0, SEEK_SET);
    count = 0;
    while (fgets(line, sizeof(line), specfile)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if ((p = strchr(line, '\n'))) *p = 0;   /* kill final newline */
        if (sscanf(line, "%hu", &ordinal) != 1) {
            fprintf(stderr, "Error reading specfile near line: `%s'\n", line);
            continue;
        }
        module->exports[count].ordinal = ordinal;

        p = strchr(line, '\t');
        if (p) {
            p++;
            module->exports[count].name = strdup(p);
    
            if ((opts & DEMANGLE) && module->exports[count].name[0] == '?')
                module->exports[count].name = demangle(module->exports[count].name);
        } else {
            module->exports[count].name = NULL;
        }
        count++;
    }

    module->export_count = count;

    fclose(specfile);
}

static void get_import_module_table(long start, import_module **table, word count) {
    import_module *ret = NULL;
    word offset;
    byte length;
    unsigned i;

    fseek(f, start, SEEK_SET);
    ret = malloc(count*sizeof(import_module));
    for (i = 0; i < count; i++) {
        offset = read_word();
        length = import_name_table[offset];
        ret[i].name = malloc((length+1)*sizeof(char));
        memcpy(ret[i].name, &import_name_table[offset+1], length);
        ret[i].name[length] = 0;

        if (mode & DISASSEMBLE)
            load_exports(&ret[i]);
        else {
            ret[i].exports = NULL;
            ret[i].export_count = 0;
        }
    }

    *table = ret;
}

static void free_entry_table(entry *table, int count) {
    int i;
    if (table) {
        for (i = 0; i < count; i++)
            free(table[i].name);
        free(table);
    }
}

static void free_import_module_table(import_module *table, word count) {
    unsigned i, j;
    if (table) {
        for (i=0;i<count;i++) {
            free(table[i].name);
            for (j = 0; j < table[i].export_count; j++)
                free(table[i].exports[j].name);
            free(table[i].exports);
        }
        free(table);
    }
}

word resource_type[MAXARGS] = {0};
word resource_id[MAXARGS] = {0};
word resource_count = 0;

void dumpne(long offset_ne)
{
    struct header_ne header;
    char *module_name = NULL;
    char *module_desc = NULL;
    int i;

    fseek(f, offset_ne, SEEK_SET);
    fread(&header, sizeof(header), 1, f);

    /* read our various tables */
    entry_count = get_entry_table(offset_ne + header.ne_enttab, &entry_table);
    module_name = read_res_name_table(offset_ne + header.ne_restab, entry_table);
    module_desc = read_res_name_table(header.ne_nrestab, entry_table);
    fseek(f, offset_ne + header.ne_imptab, SEEK_SET);
    import_name_table = malloc(header.ne_enttab - header.ne_imptab);
    fread(import_name_table, sizeof(byte), header.ne_enttab - header.ne_imptab, f);
    get_import_module_table(offset_ne + header.ne_modtab, &import_module_table, header.ne_cmod);

    if (mode == SPECFILE) {
        print_specfile(module_name, entry_table, entry_count);
    }

    printf("Module type: NE (New Executable)\n");
    printf("Module name: %s\n", module_name);
    printf("Module description: %s\n\n", module_desc);

    if (mode & DUMPHEADER)
        print_header(&header);

    if (mode & DUMPEXPORT) {
        printf("Exports:\n");
        print_export(entry_table, entry_count);
    }

    if (mode & DUMPIMPORTMOD) {
        printf("Imported modules:\n");
        for (i = 0; i < header.ne_cmod; i++)
            printf("\t%s\n", import_module_table[i].name);
        putchar('\n');
    }

    if (mode & DISASSEMBLE){
        fseek(f, offset_ne + header.ne_segtab, SEEK_SET);
        print_segments(header.ne_cseg, header.ne_align, header.ne_cs, header.ne_ip);
    }

    if (mode & DUMPRSRC){
        fseek(f, offset_ne + header.ne_rsrctab, SEEK_SET);
        if (header.ne_rsrctab != header.ne_restab)
            print_rsrc(offset_ne + header.ne_rsrctab);
        else
            printf("No resource table\n");
    }

    free_entry_table(entry_table, entry_count);
    free(import_name_table);
    free_import_module_table(import_module_table, header.ne_cmod);
    free(module_name);
    free(module_desc);
}
