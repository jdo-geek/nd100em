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

nd_sem_t sem_rtc_tick;
nd_sem_t sem_rtc;

struct rtc_data {
        bool irq_en; /* enable irq when pulse occurs */
        bool rdy; /* ready for transfer */
	ushort cntr_20ms;	/* this should wrap at 50, so we count the num of 20ms interrupts to get second ticks */
};

struct rtc_data *sys_rtc = NULL;

extern nd_sem_t sem_int;
extern nd_sem_t sem_mopc;
extern nd_sem_t sem_pap;
extern struct display_panel *gPAP;

extern struct CpuRegs *gReg;
extern ushort MODE_OPCOM;
extern ushort PANEL_PROCESSOR;
extern _RUNMODE_      CurrentCPURunMode;
extern int debug;
extern FILE *debugfile;

void rtc_20(void);
void RTC_IO(ushort ioadd);

extern void AddIdentChain(char lvl, ushort identnum, int callerid);
extern void checkPK_inInt(char *msg);
