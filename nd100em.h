/*
 * nd100em - ND100 Virtual Machine
 *
 * Copyright (c) 2006-2008 Roger Abrahamsson
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

extern int trace;
extern int debug;
extern int DAEMON;
extern int DISASM;
extern ushort PANEL_PROCESSOR;

extern long int instr_counter;
extern struct ThreadChain *gThreadChain;

extern nd_sem_t sem_int;
extern nd_sem_t sem_cons;
extern nd_sem_t sem_sigthr;
extern nd_sem_t sem_rtc_tick;
extern nd_sem_t sem_rtc;
extern nd_sem_t sem_io;
extern nd_sem_t sem_mopc;
extern nd_sem_t sem_run;
extern nd_sem_t sem_floppy;
extern nd_sem_t sem_hdd;
extern nd_sem_t sem_pap;

extern float usertime,systemtime,totaltime;
extern struct rusage *used;

extern int octalstr_to_integer(char *str);
extern int mysleep(int sec, int usec);
extern int bpun_load(void);
extern int bp_load(void);
extern int debug_open(void);
extern void unsetcbreak (void);
extern void setcbreak (void);
extern struct ThreadChain *AddThreadChain(void);
extern void RemThreadChain(struct ThreadChain * elem);
extern int nd100emconf(void);
extern void nd100_shutdown(void);
extern void setsignals(void);
extern void daemonize(void);
extern void start_threads(void);
extern void stop_threads(void);
extern void setup_cpu(void);
extern void program_load(void);
extern void blocksignals(void);
extern int trace_open(void);
extern void disasm_addword(ushort addr, ushort myword);
extern void disasm_init(void);
extern void disasm_dump(void);
extern void setup_pap(void);


int main(int argc, char *argv[]);
