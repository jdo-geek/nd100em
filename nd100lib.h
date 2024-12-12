/*
 * nd100em - ND100 Virtual Machine
 *
 * Copyright (c) 2006-2011 Roger Abrahamsson
 * Copyright (c) 2024 Heiko Bobzin
 *
 * This file is originated from the nd100em project.
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
#include <sys/termios.h>
#include "nd100.h"
extern int trace;
extern int DISASM;
extern ushort PANEL_PROCESSOR;

extern char debugname[];
extern char debugtype[];
extern FILE *debugfile;
extern int debug;

#define RUNNING_DIR     "/Users/heiko/src/nd100em/nd100em/tmp"

extern _NDRAM_		VolatileMemory;
extern _RUNMODE_	CurrentCPURunMode;
extern _CPUTYPE_	CurrentCPUType;

extern struct CpuRegs *gReg;
extern union NewPT *gPT;
extern struct MemTraceList *gMemTrace;
extern struct IdentChain *gIdentChain;

extern unsigned long instr_counter;

extern pthread_mutex_t mutrun;
extern pthread_cond_t condrun;
extern pthread_mutex_t mutmopc;
extern pthread_cond_t condmopc;

extern struct ThreadChain *gThreadChain;

extern int emulatemon;

extern int CONFIG_OK;	/* This should be set to 1 when config file has been loaded OK */
typedef enum {BP, BPUN, FLOPPY} _BOOT_TYPE_;
extern _BOOT_TYPE_	BootType; /* Variable holding the way we should boot up the emulator */
extern ushort	STARTADDR;
/* should we try and disassemble as we run? */
extern int DISASM;
/* Should we detatch and become a daemon or not? */
extern int DAEMON;
/* is console on a socket, or just the local one? */
extern int CONSOLE_IS_SOCKET;

extern struct config_t *pCFG;

extern char *FDD_IMAGE_NAME;
extern bool FDD_IMAGE_RO;


/* semaphore to release signal thread when terminating */
extern nd_sem_t sem_sigthr;
extern nd_sem_t sem_rtc_tick;
extern nd_sem_t sem_mopc;
extern nd_sem_t sem_run;

extern struct termios nd_savetty;

extern void rtc_20(void);
extern void cpu_thread(void);
extern void mopc_thread(void);
extern void panel_thread(void);
extern void console_socket_thread(void);
extern void console_stdio_thread(void);
extern void floppy_thread(void);
extern void floppy_init(void);
extern void hdd_thread(void);
extern void MemoryWrite(ushort value, ushort addr, bool UseAPT, unsigned char byte_select);
extern ushort MemoryRead(ushort addr, bool UseAPT);

extern void Setup_IO_Handlers (void);
extern void nd_setbit(ushort regnum, ushort stsbit, char val);
extern void setbit_STS_MSB(ushort stsbit, char val);
extern int sectorread (int diskNumber, char cyl, char side, char sector, unsigned short *addr);
extern int sectorwrite (int diskNumber, char cyl, char side, char sector, unsigned short *addr);
extern void disasm_addword(ushort addr, ushort myword);
extern void panel_processor_thread(void);


int octalstr_to_integer(char *str);
int mysleep(int sec, int usec);
int bpun_load(void);
int bp_load(void);
int debug_open(void);
void unsetcbreak (void);
void setcbreak (void);
struct ThreadChain *AddThreadChain(void);
void RemThreadChain(struct ThreadChain * elem);
int nd100emconf(void);
void nd_shutdown(int signum);
void setsignals(void);
void daemonize(void);
pthread_t add_thread(void *funcpointer, bool is_jointype);
void start_threads(void);
void stop_threads(void);
void setup_cpu(void);
void program_load(void);
extern void cpu_savestate(void);
extern void cpu_loadstate(void);
