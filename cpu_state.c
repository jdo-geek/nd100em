/*
 * nd100em - ND100 Virtual Machine
 *
 * Copyright (c) 2024 Heiko Bobzin
 *
 * This file was added in 2024 to save and load the state of the virtual machine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include "nd100lib.h"
#include "nd100.h"
#include "cpu.h"

#include "cpu_state.h"

const char *CPUSTATE_FILE_NAME = "cpustate.bin";

typedef struct _state {
    FILE *fp;
    bool load;
    char line[2048];
} state;

static void next(state *s) {
    if (s->load) {
        fgets(s->line,sizeof(s->line),s->fp);
    }
}
/**
  * matches, if prefix is exactly found in the next line.
 */
static char *matches(state *s, const char *prefix) {
    if (s->load) {
        ulong i = strlen(prefix);
        if (!strncmp(s->line,prefix,i) && s->line[i] == '=') {
            return &s->line[i+1];
        };
    }
    return NULL;
}

static void state_ulong(state *s, const char *key, unsigned long *value)
{
    if (s->load) {
        char *str = matches(s,key);
        if (str) {
            sscanf(str,"%li",value);
            next(s);
        }
    } else {
        fprintf(s->fp, "%s=%ld\n", key, *value);
    }
}

static void state_int(state *s, const char *key, int *value)
{
    if (s->load) {
        char *str = matches(s,key);
        if (str) {
            sscanf(str,"%i",value);
            next(s);
        }
    } else {
        fprintf(s->fp, "%s=%d\n", key, *value);
    }
}

static void state_word(state *s, const char *key, ushort *value)
{
    if (s->load) {
        char *str = matches(s,key);
        if (str) {
            int tmp;
            sscanf(str,"%o",&tmp);
            *value = tmp & 0xffff;
            next(s);
        }
    } else {
        unsigned int tmp = *value;
        fprintf(s->fp, "%s=0%o\n", key, tmp);
    }
}

static void state_bool(state *s, const char *key, bool *value)
{
    if (s->load) {
        char *str = matches(s,key);
        if (str) {
            int tmp;
            sscanf(str,"d%d",&tmp);
            *value = tmp != 0;
            next(s);
        }
    } else {
        unsigned int tmp = *value;
        fprintf(s->fp, "%s=%d\n", key, *value ? 1 : 0);
    }
}


static bool isMemEmpty(unsigned char *mem, int size) {
    for (int i = 0; i < size; i++) {
        if (*mem++) return false;
    }
    return true;
}

static void state_block(state *s, const char *key_prefix, unsigned char *value, int size)
{
    char key[256];
    const int blockLen = 512;
    if (s->load) {
        ulong i = strlen(key_prefix);
        while (!strncmp(s->line,key_prefix,i)) {
            char *p = s->line + i;
            if (*p == '-') {
                p++;
                int offset = 0;
                int n = sscanf(p,"%08x",&offset);
                if (n == 1) {
                    while (*p && *p != '=') p++;
                    if (*p == '=') p++;
                    for (int j = 0; j < blockLen && *p; j++) {
                        char scan[3];
                        scan[0] = *p++;
                        if (*p) {
                            scan[1] = *p++;
                            scan[2] = 0;
                            value[offset + j] = strtoul(scan,NULL,16);
                        }
                    }
                }
            }
            next(s);
        };

    } else {
        for (int i = 0; i < size; i += blockLen) {
            if (!isMemEmpty(&value[i],blockLen)) {
                fprintf(s->fp,"%s-%08x=",key_prefix,i);
                for (int j = 0; j < blockLen; j++) {
                    fprintf(s->fp,"%02x",value[i+j]);
                }
                fprintf(s->fp,"\n");
            }
        }
    }
}

bool cpustate_is_loadable(void) {
    return access(CPUSTATE_FILE_NAME, S_IRUSR) == 0;
}

void cpustate(bool load,
            ulong *instr_counter,
            _NDRAM_      *VolatileMemory,
            struct CpuRegs *gReg,
            union NewPT *gPT,
            struct IdentChain **gIdentChain)
{
    state s;
    s.fp = fopen(CPUSTATE_FILE_NAME,load ? "r" : "w");
    if (s.fp == NULL) {
        fprintf(stderr,"cpustate: Failed to open %s, error %s\n",CPUSTATE_FILE_NAME,strerror(errno));
        return;
    }
    s.load = load;
    if (s.load) {
        next(&s);   // read first line.
    }
    state_ulong(&s,"INSTR_COUNT",instr_counter);
    state_block(&s,"MEM",&VolatileMemory->c_Array[0],sizeof(VolatileMemory->c_Array));
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            char key[80];
            sprintf(key,"REG%02d.%02d",i,j);
            state_word(&s,key,&gReg->reg[i][j]);
        }
    }
    state_bool(&s,"MIIC",&gReg->mylock_IIC);
    state_bool(&s,"MPEA",&gReg->mylock_PEA);
    state_bool(&s,"MPES",&gReg->mylock_PES);
    state_bool(&s,"MPGS",&gReg->mylock_PGS);
    state_bool(&s,"HBRK",&gReg->has_breakpoint);
    state_bool(&s,"HICN",&gReg->has_instr_cntr);
    state_word(&s,"INCT",&gReg->instructioncounter);
    state_word(&s,"MIR",&gReg->myreg_IR);
    state_word(&s,"MPK",&gReg->myreg_PK);
    state_word(&s,"MPFB",&gReg->myreg_PFB);
    state_word(&s,"PANS",&gReg->reg_PANS);
    state_word(&s,"PANC",&gReg->reg_PANC);
    state_word(&s,"OPR",&gReg->reg_OPR);
    state_word(&s,"LMP",&gReg->reg_LMP);
    state_word(&s,"PGS",&gReg->reg_PGS);
    for (int j = 0; j < 16; j++) {
        char key[80];
        sprintf(key,"PCR%02d",j);
        state_word(&s,key,&gReg->reg_PCR[j]);
    }
    state_word(&s,"PVL",&gReg->reg_PVL);
    state_word(&s,"IID",&gReg->reg_IID);
    state_word(&s,"IIE",&gReg->reg_IIE);
    state_word(&s,"PID",&gReg->reg_PID);
    state_word(&s,"PIE",&gReg->reg_PIE);
    state_word(&s,"CSR",&gReg->reg_CSR);
    state_word(&s,"CCL",&gReg->reg_CCL);
    state_word(&s,"ACTL",&gReg->reg_ACTL);
    state_word(&s,"LCIL",&gReg->reg_LCIL);
    state_word(&s,"ALD",&gReg->reg_ALD);
    state_word(&s,"UCIL",&gReg->reg_UCIL);
    state_word(&s,"PES",&gReg->reg_PES);
    state_word(&s,"PGC",&gReg->reg_PGC);
    state_word(&s,"PEA",&gReg->reg_PEA);
    state_word(&s,"ECCR",&gReg->reg_ECCR);
    for (int i = 0; i < sizeof(gPT->pt_arr)/(sizeof(ulong));i++) {
        char key[80];
        sprintf(key,"PT%03d",i);
        state_ulong(&s, key, &gPT->pt_arr[i]);
    }
    if (load) {
    } else {
        struct IdentChain *p = *gIdentChain;
        int i = 0;
        while (p != NULL) {
            char key[80];
            sprintf(key,"IDC%03d",i);
            state_int(&s,key,&p->callerid);
            sprintf(key,"IDL%03d",i);
            state_int(&s,key,&p->level);
            sprintf(key,"IDI%03d",i);
            state_word(&s,key,&p->identcode);
            p = p->next;
            i++;
        }
    }
    fclose(s.fp);
    
    // Hack the date in 0000125...
    {
        time_t seconds = time(NULL);
        struct tm now;
        localtime_r(&seconds,&now);
        ushort *time = &VolatileMemory->n_Array[0125];
        time[0] = now.tm_sec;
        time[1] = now.tm_min;
        time[2] = now.tm_hour;
        time[3] = now.tm_mday;
        time[4] = now.tm_mon+1;
        time[5] = now.tm_year+1900;
    }

}
