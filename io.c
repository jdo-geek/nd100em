/*
 * nd100em - ND100 Virtual Machine
 *
 * Copyright (c) 2008 Roger Abrahamsson
 * Copyright (c) 2008 Zdravko Dimitrov
 * Copyright (c) 2008 Goran Axelsson
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "nd100.h"
#include "nd100lib.h"
#include "io.h"
#include "cpu.h"

extern void trace_open(void);

/* console synchronization*/
nd_sem_t sem_cons;

/* io synchronization*/
nd_sem_t sem_io;

/* floppy synchronization*/
nd_sem_t sem_floppy;

/* hdd synchronization*/
nd_sem_t sem_hdd;

/* panel processor synchronization*/
nd_sem_t sem_pap;

struct display_panel *gPAP;

char *script_console = "";
static int diskNumber = 0;

/*
 * IOX and IOXT operation, dummy for now.
 * The Norm is: even address is read, odd address is write
 * source: ND-100 Input/Output Manual
 */
void io_op (ushort ioadd) {
        ioarr[ioadd](ioadd);	/* call using a function pointer from the array
				this way we are as flexible as possible as we
				implement io calls. */
}

static
void hdd_dev_clear(struct hdd_data *dev) {
    memset(dev,0,sizeof(struct hdd_data));
    dev->on_cylinder = false;
    dev->finished = true;
}

#define HDD_IRQ_ID      101
#define FLOPPY_ID       201
#define CONSOLE_IN_ID   301
#define CONSOLE_OUT_ID  401

static
void genIRQ(ushort level, ushort ident, ushort chainId)
{
    while ((nd_sem_wait(&sem_int)) == -1 && errno == EINTR) /* wait for interrupt lock to be free */
        continue;       /* Restart if interrupted by handler */
    gPID |= (1 << level) ;
    AddIdentChain(level,ident,chainId); /* Add interrupt to ident chain, lvl13, ident code 1, and identify us */
    if (nd_sem_post(&sem_int) == -1) { /* release interrupt lock */
        if (debug) fprintf(debugfile,"ERROR!!! sem_post failure DOMCL\n");
        CurrentCPURunMode = SHUTDOWN;
    }
    if (debug) fprintf(debugfile,"gen IRQ %d 0%o 0%o\n",level,ident,chainId);
    interrupt(level,0);
}

/*
 * Access to unpopulated IO area
 */
void Default_IO(ushort ioadd) {
    if (1) {
        if (ioadd & 1) {
            // Write...
        } else {
            // Read...
            gA = 0;
        }
    } else {
        mysleep(0,10); /* Sleep 10us for IOX timeout time simulation */
        if(gReg->reg_IIE & 0x80) {
            if (trace & 0x01) fprintf(tracefile,
                                      "#o (i,d) #v# (\"%d\",\"No IO device, IOX error interrupt after 10 us.\");\n",
                                      (int)instr_counter);
            interrupt(14,1<<7); /* IOX error lvl14 */
        }
    }
}

/*
 * Read and write from/to floppy
 */
void Floppy_IO(ushort ioadd) {
	int s;
	int a = ioadd & 0x07;
	ushort tmp;
	struct floppy_data *dev = iodata[ioadd];
	switch(a) {
	case 0: /* IOX RDAD - Read data buffer */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		if (dev->bufptr >= 0 && dev->bufptr < FDD_BUFSIZE) {
			gA = dev->buff[dev->bufptr];
            if (debug) fprintf(debugfile, "Floppy: read buffer %d -> 0%03o 0x%02X\n",dev->bufptr,gA,gA);
			dev->bufptr++;
			if(dev->bufptr >= FDD_BUFSIZE)
				dev->bufptr = 0;
		}
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 1: /* IOX WDAT - Write data buffer */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		if (dev->bufptr>=0 && dev->bufptr<FDD_BUFSIZE) {
			dev->buff[dev->bufptr] = gA;
            if (debug) fprintf(debugfile, "Floppy: write buffer %d -> 0%03o 0x%02X\n",dev->bufptr,gA,gA);
			dev->bufptr++;
			if(dev->bufptr >= FDD_BUFSIZE)
				dev->bufptr = 0;
		}

		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 2: /* IOX RSR1 - Read status register No. 1 */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		gA = 0; /* Put A in consistent state   bit 0: (1) not used */
		gA |= (dev->irq_en) ? (1<<1) : 0;	/* bit 1: (2) IRQ enabled */
        if (dev->busy_counter > 0) {
            dev->busy_counter--;
            gA |= (1<<2) ;        /* bit 2: (4) Device is busy (bit 2) */
        } else {
            gA |= (dev->busy) ? (1<<2) : 0;		/* bit 2: (4) Device is busy (bit 2) */
            gA |= (!dev->busy) ? (1<<3) : 0;    /* bit 3: (8) Device is ready for transfer */
            gA |= (dev->sense) ? (1<<4) : 0;	/* Bit 4: (16) from RSR2, interrupt set, check STS reg 2 */
            /* Bit 5: Deleted record */
            gA |= (dev->rwComplete) ? (1<<6) : 0;    /* bit 6: (64) R/W complete */
            gA |= (dev->seekComplete) ? (1<<7) : 0;    /*  Bit 7: (128) Seek complete */
        }
        if (debug) fprintf(debugfile,"Floppy: IOX %o RSR1 - A=0x%04x\n",ioadd,gA);
/* Bit 8: Timeout */
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 3: /* IOX WCWD - Write control word */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */
            ushort tempA = gA;
		if ((tempA >> 1) & 0x01) {
			dev->irq_en = 1;
		}
        if ((tempA >> 2) & 0x01) {  /* Bit 2 -> Autoload mode */
            /* Seek to track 0, read sector 1 (first) into buffer */
            dev->track = 48;
            dev->sector = 1;
            dev->bufptr_read = 0;
        }
		if ((tempA >> 3) & 0x01) {  /* Bit 3 (8) -> Test mode */
			dev->test_mode = 1;
		}
		if ((tempA >> 4) & 0x01) {		/* (16) Device clear and deselect */
			// dev->selected_drive = -1;
            // dev->track = 0;          /* Do not reset the track. */
			dev->bufptr = 0;
            dev->sector = 1;
		}
		if ((tempA >> 5) & 0x01) {		/* (32) Clear Interface buffer address */
            dev->bufptr_read = 0;
			dev->bufptr = 0;
		}
		if (tempA & 0xff00) {
			dev->busy = 1;
            dev->busy_counter = dev->irq_en ? 0 : 10;
            dev->rwComplete = 0;
            dev->seekComplete = 0;
			dev->command = ((tempA & 0xff00 ) >>8);
			/* TRIGGER FLOPPY THREAD */
			if (nd_sem_post(&sem_floppy) == -1) { /* release floppy lock */
				if (debug) fprintf(debugfile,"ERROR!!! sem_floppy failure Floppy_IO\n");
				CurrentCPURunMode = SHUTDOWN;
			}

		}

		if (debug) fprintf(debugfile,"Floppy: IOX %o WCWD - A=0x%04x\n",ioadd,tempA);

		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 4: /* IOX RSR2 - Read status register No. 2 */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		gA = 0; /* Put A in consistent state */
		gA |= (dev->drive_not_rdy) ? (1<<8) : 0;	/* drive not ready (bit 8)*/
		gA |= (dev->write_protect) ? (1<<9) : 0;	/* set if trying to write to write protected diskette (file) */
		gA |= (dev->missing) ? (1<<11) : 0;		/* sector missing / no am */

		if (debug) fprintf(debugfile,"Floppy: IOX %o RSR2 - A=0x%04x\n",ioadd,gA);

		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 5: /* IOX WDAD - Write Drive Address/ Write Difference */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		if (gA & 0x1) { /* Write drive address */
			if (debug) fprintf(debugfile,"Floppy: IOX 1565 - Write Drive Address... 0x%04x\n",gA);
			tmp = (gA >> 8) & 0x07;
            if (tmp <4) {
                if (debug) fprintf(debugfile,"Floppy: IOX 1565 - Select drive 0x%04x\n",tmp);
                dev->selected_drive = tmp;
            }
            if ((gA >> 11) & 0x01) {
                if (debug) fprintf(debugfile,"Floppy: IOX 1565 - Select no drive\n");
                dev->selected_drive = -1;
            }
			tmp = (gA >> 14) & 0x03;
			switch (tmp) {
			case 0:
				if(dev->selected_drive != -1)
					dev->unit[dev->selected_drive]->drive_format = 0;
				break;
			case 1:
				if(dev->selected_drive != -1)
					dev->unit[dev->selected_drive]->drive_format = 0;
				break;
			case 2:
				if(dev->selected_drive != -1)
					dev->unit[dev->selected_drive]->drive_format = 1;
				break;
			case 3:
				if(dev->selected_drive != -1)
					dev->unit[dev->selected_drive]->drive_format = 2;
				break;
			}
		} else { /* Write difference */
			tmp= (gA >> 8) & 0x7f;
			if(dev->selected_drive != -1) {
				dev->unit[dev->selected_drive]->diff_track = tmp;
				dev->unit[dev->selected_drive]->dir_track = (gA >> 15 ) & 0x01;
                if (debug) fprintf(debugfile,"Floppy: IOX 1565 - Write Difference...%d %d \n",
                                   dev->unit[dev->selected_drive]->diff_track,
                                   dev->unit[dev->selected_drive]->dir_track);
            } else {
                if (debug) fprintf(debugfile,"Floppy: IOX 1565 - Write Difference... NO DRIVE\n");
            }
		}

		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 6: /* Read Test */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

        if (debug) fprintf(debugfile,"Floppy: Read Test\n");
		if (dev->bufptr>=0 && dev->bufptr<FDD_BUFSIZE) {
			if (dev->bufptr_msb) {
				dev->buff[dev->bufptr] = (dev->buff[dev->bufptr] & 0x00ff) | ((dev->test_byte << 8) & 0xff00);
				dev->bufptr_msb = 0;
				dev->bufptr++;
			} else {
				dev->buff[dev->bufptr] = (dev->buff[dev->bufptr] & 0xff00) | ((dev->test_byte) & 0x00ff);
				dev->bufptr_msb = 1;
			}
			if(dev->bufptr >= FDD_BUFSIZE)
				dev->bufptr = 0;
		}

		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	case 7: /*IOX WSCT - Write Sector/ Write Test Byte */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		if (dev->test_mode) {
            if (debug) fprintf(debugfile, "Floppy: set sector (test): %d\n",dev->sector);
			dev->test_byte = (gA >>8) & 0xff;
		} else {
			dev->sector = (gA >> 8) & 0x7f;
			dev->sector_autoinc = (gA>>15) &0x01;
            if (debug) fprintf(debugfile, "Floppy: set sector: %d\n",dev->sector);
			/*TODO:: check ranges on sector based on floppy type selected */
		}

		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		break;
	}
}

/*
 * TODO:: Not having figured out exactly what these are supposed to do.
 * MIGHT be described in Sintran III Reference Manual...
 * So just add functionality here for now to get further in testing programs
 * by guessing what they want :)
 */
void Parity_Mem_IO(ushort ioadd) {
	switch(ioadd) {
	case 04: /* Read */
		break;
	case 05: /* Write */
		break;
	case 06: /* Read */
		gA = 0x0008; /* test program seems to look for this */
		break;
	case 07: /* Write */
		break;
	}
}

/*
 * Read and write from/to hard disk
 */
void HDD_IO(ushort ioadd) {
    int s;
    int a = ioadd & 0x07;
    struct hdd_data *dev = iodata[ioadd];
    while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
        continue; /* Restart if interrupted by handler */
    switch(a) {
    case 0: /* Read Mem Addr / Word count */
            if (dev->cwr & 0x8000) {
                if (!dev->second_word) {
                    gA = dev->word_count & 0xffff;
                } else {
                    gA = (dev->word_count >> 16) & 0xff;
                }
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read Word Count %d A=0x%04x\n",ioadd,dev->word_count,gA);
            } else {
                if (!dev->second_word) {
                    gA = dev->mem_addr & 0xffff;
                } else {
                    gA = (dev->mem_addr >> 16) & 0xff;
                }
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read Mem Addr 0x%08x A=0x%04x FF=%d\n",ioadd,dev->mem_addr,gA,dev->second_word);
            }
            dev->second_word = !dev->second_word;
            break;
    case 1: /* Load Mem Addr / Word count */
            if (dev->cwr & 0x8000) {
                dev->word_count = ((dev->word_count << 16) & 0x00ff0000) | gA;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Load Word Count 0x%08x 0x%04x\n",ioadd,dev->word_count,gA);
            } else {
                dev->mem_addr = ((dev->mem_addr << 16) & 0x00ff0000) | gA;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Load Mem Addr 0x%08x 0x%04x\n",ioadd,dev->mem_addr,gA);
            }
            dev->second_word = !dev->second_word;
            break;
    case 2: /* Read Seek Condition / Read ECC Count */
            if (dev->cwr & 0x8000) {
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read ECC Count\n",ioadd);
            } else {
                ushort acc = 0;
                // Bits 0..3 Seek complete: toggles every revolution
                acc |= 0x0;
                // 4 ..7 not used
                // 8..9 Unit select
                acc |= (dev->unit_select & 0x03) << 8;
                // 11 Seek error
                // 12 always 1
                acc |= (1<<12);
                // 13 ECC correctable
                // 14 ECC parity error
                // 15 Error in Adress Field
                gA = acc;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read Seek Cond. A=0x%04x\n",ioadd,acc);
            }
            break;
    case 3: /* Load Block Address I / Load Block Address II */
            if (dev->cwr & 0x8000) {
                dev->track = gA;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Load Block Addr II track=%d\n",ioadd,dev->track);
            } else {
                // new:
                dev->sector = gA & 0xff;
                dev->surface = ((gA >> 8) & 0xff);
                // old 10MB:
                //dev->sector = gA & 0x1F;
                //dev->surface = (((gA >> 14) | (gA >> 5)) & 0x03) ^ 2;
                //dev->track = (gA >> 6) & 0x1FF;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Load Block Addr I A=0x%04x sect=%d surf=%d cyl=%d\n",ioadd,gA,dev->sector,dev->surface, dev->track);
            }
            break;
    case 4: /* Read Status / Read ECC Pattern */
            if (dev->cwr & 0x8000) {
                ushort acc = 0xF800;
                gA = acc;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read ECC Pattern 0x%04x\n",ioadd,acc);
            } else {
                dev->second_word = false;
                ushort acc = 0;
                acc |= dev->irq_en ? 1 : 0;       // Bit 0 IRQ Enabled
                acc |= dev->err_irq_en ? (1<<1) : 0;
                acc |= dev->active ? (1<<2) : 0;
                acc |= dev->finished ? (1<<3) : 0;
                bool error = dev->err_hw || dev->err_data || dev->err_compare || dev->err_timeout 
                    || dev->err_abnormal || dev->err_ill_load || dev->err_disk_unit || dev->err_dma_channel || dev->err_addr_mismatch;
                acc |= error ? (1<<4) : 0;
                acc |= dev->err_ill_load ? (1<<5) : 0;
                acc |= dev->err_timeout ? (1<<6) : 0;
                acc |= dev->err_hw ? (1<<7) : 0;
                acc |= dev->err_addr_mismatch ? (1<<8) : 0;
                acc |= dev->err_data ? (1<<9) : 0;
                acc |= dev->err_compare ? (1<<10) : 0;
                acc |= dev->err_dma_channel ? (1<<11) : 0;
                acc |= dev->err_abnormal ? (1<<12) : 0;
                acc |= dev->err_disk_unit ? (1<<13) : 0;
                acc |= dev->on_cylinder ? (1<<14) : 0;
                gA = acc;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read Status 0x%04x\n",ioadd,acc);
            }
            break;
        case 5: /* Write Control Word WCWD */ {
            char *opname = "??";
            ushort acc = gA;
            if (acc & (1<<4)) { /* Device clear */
                hdd_dev_clear(dev);
                dev->on_cylinder = true;
                dev->finished = true;
                dev->active = false;
            }
            dev->cwr = acc;
            // Old HAWK drive: Bit 5 => Bit 16 memory address Bit 6 => Bit 17 memory address
            dev->unit_select = (acc & 0x180) >> 7;
            dev->opcode = (acc & 0x7800) >> 11;
            dev->active = (acc & (1<<2)) != 0;
            dev->irq_en = (acc & 1) != 0;
            // bit 3: test mode
            // bit 4: clear
            switch (dev->opcode) {
                case 0: opname = "ReadTransfer"; break;
                case 1: opname = "WriteTransfer"; break;
                case 2: opname = "ReadParity"; break;
                case 3: opname = "Compare"; break;
                case 4: opname = "InitiateSeek"; break;
                case 5: opname = "WriteFormat"; break;
                case 6: opname = "SeekComplete"; break;
                case 7: opname = "ReturnToZeroSeek"; break;
                case 8: opname = "RunECC"; break;
                case 9: opname = "SelectRelease"; break;
            }
            if (dev->active) {
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Write control word 0x%04x, unit=%d, opcode=%d, %s, active=%d\n",ioadd,acc,dev->unit_select, dev->opcode, opname, dev->active);
            } else {
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Write control word 0x%04x, unit=%d\n",ioadd,acc,dev->unit_select);
            }
            if (dev->active && dev->finished) {
                dev->finished = false;
                /* TRIGGER HDD THREAD */
                if (nd_sem_post(&sem_hdd) == -1) { /* release hdd lock */
                    if (debug) fprintf(debugfile,"ERROR!!! sem_hdd failure HDD_IO\n");
                    CurrentCPURunMode = SHUTDOWN;
                }
            }
            break; }
    case 6: /* Read Block Address I / Read Block Address II */
            if (dev->cwr & 0x8000) {
                gA = dev->track;
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read Block Addr II track=%d A=0x%04x\n",ioadd,dev->track,gA);
            } else {
                gA = (dev->sector & 0xff) | ((dev->surface << 8) & 0xff00);
                if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Read Block Addr I sect=%d surf=%d A=0x%04x\n",ioadd,dev->sector,dev->surface,gA);
            }
            break;
        case 7: /* Load Word Count / Load ECC Control */
                if (dev->cwr & 0x8000) {
                    if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Load ECC control A=0x%04x\n",ioadd,gA);
                } else {
                    if (dev->second_word) {
                        dev->word_count = (dev->word_count & 0xffff) | ((gA << 16) & 0xff0000);
                    } else {
                        dev->word_count = (dev->word_count & 0xff0000) | gA;
                    }
                    // Not on 10MB disk: dev->second_word = !dev->second_word;
                    dev->second_word = !dev->second_word;
                    if (debug) fprintf(debugfile,"HDD_IO: IOX 0%o Load Word Count %d A=0x%04x\n",ioadd,dev->word_count,gA);
                }
                break;

    }
    if (debug) fflush(debugfile);
    if (nd_sem_post(&sem_io) == -1) { /* release io lock */
        if (debug) fprintf(debugfile,"ERROR!!! sem_post failure HDD_IO\n");
        CurrentCPURunMode = SHUTDOWN;
    }
} /* HDD IO */

/*
 * mopc function to scan for an available char
 * returns nonzero if char was available and the char
 * in the address pointed to by chptr.
 */
int mopc_in(char *chptr) {
	int s;
	unsigned char cp,pp,ptr; /* ringbuffer pointers */
	ushort status;

	if (debug) fprintf(debugfile,"(##) mopc_in...\n");
	if (debug) fflush(debugfile);

	if(!(tty_arr[0]))	/* array dont exists, so no chars available */
		return(0);

	cp = tty_arr[0]->rcv_cp;
	pp = tty_arr[0]->rcv_fp;
	status = tty_arr[0]->in_status;
	if (nd_sem_post(&sem_io) == -1) { /* release io lock */
		if (debug) fprintf(debugfile,"ERROR!!! sem_post failure mopc_in\n");
		CurrentCPURunMode = SHUTDOWN;
	}

	if (!(status & 0x0008))	/* Bit 3=0 device not ready for transfer */
		return(0);

	if (debug) fprintf(debugfile,"(##) mopc_in looking for char...\n");
	if (debug) fflush(debugfile);

	if (pp != cp) {	/* ok we have some data here */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */
		ptr = tty_arr[0]->rcv_cp;
		*chptr =  (tty_arr[0]->rcv_arr[ptr] & 0x7f);
		tty_arr[0]->rcv_cp +=1;
		if (tty_arr[0]->rcv_fp == tty_arr[0]->rcv_cp) {
			tty_arr[0]->in_status &= ~0x0008;	/* Bit 3=0 device not ready for transfer */
		}
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure mopc_in\n");
			CurrentCPURunMode = SHUTDOWN;
		}

		if (debug) fprintf(debugfile,"(##) mopc_in data found...\n");
		if (debug) fflush(debugfile);

		return(1);
	} else {
		if (debug) fprintf(debugfile,"(##) mopc_in data not found...\n");
		if (debug) fflush(debugfile);
		return(0);
	}
}

/*
 * mopc function to output a char ch.
 */
void mopc_out(char ch) {
	int s;
	unsigned char ptr; /* ringbuffer pointer */

	if (debug) fprintf(debugfile,"(##) mopc_out...\n");
	if (debug) fflush(debugfile);

	if(tty_arr[0]){ /* array exists so we can work with this now */
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */
		ptr=tty_arr[0]->snd_fp;
		tty_arr[0]->snd_arr[ptr]= ch;
		tty_arr[0]->snd_fp++;
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure mopc_out\n");
			CurrentCPURunMode = SHUTDOWN;
		}

		if (nd_sem_post(&sem_cons) == -1) { /* release console lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure mopc_out\n");
			CurrentCPURunMode = SHUTDOWN;
		}
	}
}

static void slowDown(void)
{
    static double last = 0;
    if (instr_counter > last + 20) {
        last = instr_counter;
        usleep(1000);
    }
}

/*
 * Read and write to system console
 */
void Console_IO(ushort ioadd) {
	int s;
	unsigned char ptr; /* ringbuffer pointer */
    while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
        continue; /* Restart if interrupted by handler */
	switch(ioadd) {
	case 0300: /* Read input data */
		if (!tty_arr[0]){ /* tty structure not created yet... return zero(safety function) */
			gA=0;
			break;
		}
		if (MODE_OPCOM) { /* mopc has authority */
			break;
		}
		if(tty_arr[0]) { /* array exists so we can work with this now */
			if (tty_arr[0]->rcv_fp != tty_arr[0]->rcv_cp) { /* ok we have some data here */
				ptr = tty_arr[0]->rcv_cp;
				gA =  (tty_arr[0]->rcv_arr[ptr] & 0x00ff);
				tty_arr[0]->rcv_cp++;
				if (tty_arr[0]->rcv_fp == tty_arr[0]->rcv_cp) {
					tty_arr[0]->in_status &= ~0x0008; /* Bit 3=0 device not ready for transfer */
				}
			} else {
				gA=0;
			}
		}
		break;
	case 0301: /* NOOP*/
		break;
	case 0302: /* Read input status */
		if (!tty_arr[0]){ /* tty structure not created yet... return zero (safety function) */
			gA=0;
			break;
		}
		if (MODE_OPCOM) { /* mopc has authority */
			gA =tty_arr[0]->in_status & ~0x00008;	/* Bit 3=0 device not ready for transfer */
			break;
		}
        gA =tty_arr[0]->in_status;
        slowDown();
		break;
	case 0303: /* Set input control */
		if (!tty_arr[0]) return; /* tty structure not created yet... return zero (safety function) */
		tty_arr[0]->in_control = gA; /* sets control reg all flags */
		if (gA & 0x0004) {	/* activate device */
			tty_arr[0]->in_status |= 0x0004;
		} else {		/* deactivate device */
			tty_arr[0]->in_status &= ~0x0004;
		}
		break;
	case 0304: /* Returns 0 in A */
		gA = 0;
		break;
	case 0305: /* Write data */
		if(tty_arr[0]){ /* array exists so we can work with this now */
            ushort ch = gA & 0x007F;
			ptr=tty_arr[0]->snd_fp;
			tty_arr[0]->snd_arr[ptr] = ch;
			tty_arr[0]->snd_fp++;
            if (debug) fprintf(debugfile,"Console: 0x%02X (%c)\n",ch,ch);
			if (nd_sem_post(&sem_cons) == -1) { /* release console lock */
				if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Console_IO\n");
				CurrentCPURunMode = SHUTDOWN;
			}
		}
		break;
	case 0306: /* Read output status */
		if (!tty_arr[0]){ /* tty structure not created yet... return zero (safety function) */
			gA=0;
			break;
		}
		gA = tty_arr[0]->out_status;
        tty_arr[0]->out_status &= 0xFFFE; // Clear IRQ bit
		break;
	case 0307: /* Set output control */
		if (!tty_arr[0]) break; /* tty structure not created yet... return zero (safety function) */
        tty_arr[0]->out_control = gA;
		break;
	}
    if (nd_sem_post(&sem_io) == -1) { /* release io lock */
            if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Console_IO\n");
            CurrentCPURunMode = SHUTDOWN;
    }
}

void IO_Handler_Add(int startdev, int stopdev, void *funcpointer, void *datapointer) {
	int i;
	for(i=startdev;i<=stopdev;i++)
		ioarr[i] = funcpointer;
	return;
}

void IO_Data_Add(int startdev, int stopdev,  void * datastructptr) {
	int i;
	for(i=startdev;i<=stopdev;i++)
		iodata[i] = datastructptr;
	return;
}

static
void hdd_init(void)
{
    struct hdd_data * dev;
    
    dev =calloc(1,sizeof(struct hdd_data));
    IO_Data_Add(01540,01547,dev);
    
    dev =calloc(1,sizeof(struct hdd_data));
    IO_Data_Add(0500,0507,dev);
}

/* Add IO handler addresses in this function */
void Setup_IO_Handlers (void) {
	int i;

	/* to be removed, and changed to new structure */
	for(i=0; i<=255;i++){
		tty_arr[i] = 0;
	}

	IO_Handler_Add(0,65535,&Default_IO,NULL);		/* Default setup to dummy routine */
	IO_Data_Add(0,65535,NULL);				/* Make sure all data pointers are NULL */
	IO_Handler_Add(4,7,&Parity_Mem_IO,NULL);		/* Parity Memory something, 4-7 octal */
	IO_Handler_Add(8,11,&RTC_IO,NULL);			/* CPU RTC 10-13 octal */
	IO_Handler_Add(192,199,&Console_IO,NULL);		/* Console terminal 300-307 octal */
	IO_Handler_Add(880,887,&Floppy_IO,NULL);		/* Floppy Disk 1 at 1560-1567 octal */
    floppy_init();
    IO_Handler_Add(01540,01547,&HDD_IO,NULL);        /* Hard Disk 1 at 1540-1547 octal */
    // 10MB IO_Handler_Add(0500,0507,&HDD_IO,NULL);        /* Hard Disk 1 at 1540-1547 octal */
    hdd_init();
}

/*
 * floppy_init
 * NOTE: Not sure if this is the best way, but needed to start somewhere. Might be a totally different solution in the end.
 *
 */
void floppy_init(void) {
	struct floppy_data * ptr;
	struct fdd_unit * ptr2;

	ptr =calloc(1,sizeof(struct floppy_data));
	if (ptr) {
		ptr2 = calloc(1,sizeof(struct fdd_unit));
		if (ptr2) {
			ptr->unit[0] = ptr2;
			if(FDD_IMAGE_NAME) {
				ptr->unit[0]->filename = strdup(FDD_IMAGE_NAME);
				ptr->unit[0]->readonly = FDD_IMAGE_RO;
				if (FDD_IMAGE_RO && ptr->unit[0]->filename) {
					ptr->unit[0]->fp = fopen(FDD_IMAGE_NAME, "r");
				} else if(ptr->unit[0]->filename){
					ptr->unit[0]->fp = fopen(FDD_IMAGE_NAME, "r+");
				}
			}
		}
		ptr2 = calloc(1,sizeof(struct fdd_unit));
		if (ptr2) {
			ptr->unit[1] = ptr2;
		}
		ptr2 = calloc(1,sizeof(struct fdd_unit));
		if (ptr2) {
			ptr->unit[2] = ptr2;
		}
	}
	ptr->selected_drive = 0;	/* no drive selected at start */
    ptr->sector = 1;
	IO_Data_Add(880,887,ptr);
}

void
select_floppy(int which) {
    diskNumber = which;
}

/*
 * Here we do the stuff to setup a socket and listen to it.
 * The rest is supposed to be done in the function calling it,
 * like starting up threads for accepting connections etc.
 */
void do_listen(int port, int numconn, int * sock) {

	int istrue = 1;
	struct sockaddr_in server_addr;
	char errbuff[256];

	if ((*sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		if( strerror_r( errno, errbuff, 256 ) == 0 ) {
			if (debug) fprintf(debugfile,"(#)SOCKET error -- %s\n",errbuff);
		}
		CurrentCPURunMode = SHUTDOWN;
		return;
	}
	if (setsockopt(*sock,SOL_SOCKET,SO_REUSEADDR,&istrue,sizeof(int)) == -1) {
		if( strerror_r( errno, errbuff, 256 ) == 0 ) {
			if (debug) fprintf(debugfile,"(#)SOCKET error -- %s\n",errbuff);
		}
		CurrentCPURunMode = SHUTDOWN;
		return;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server_addr.sin_zero),8);
	if (bind(*sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
		if( strerror_r( errno, errbuff, 256 ) == 0 ) {
			if (debug) fprintf(debugfile,"(#)SOCKET bind error -- %s\n",errbuff);
		}
		CurrentCPURunMode = SHUTDOWN;
		return;
	}
	if (listen(*sock, numconn) == -1) {
		if( strerror_r( errno, errbuff, 256 ) == 0 ) {
			if (debug) fprintf(debugfile,"(#)SOCKET listen error -- %s\n",errbuff);
		}
		CurrentCPURunMode = SHUTDOWN;
		return;
	}
}

void
dump_mem(char *filename) {
    FILE *memfile = fopen(filename,"w+");
    if (memfile != NULL) {
        fwrite(&VolatileMemory,1,sizeof(VolatileMemory),memfile);
        fclose(memfile);
    }
}

void console_stdio_in(void) {
	int s;
	char ch,parity;
	char recv_data[1024];
	long int numbytes;
	unsigned char pp,cp,tmp,cnt,cnt2;
	long int numread = 0;
	ushort status,control;
	fd_set rfds;
	int retval;

	parity=0;

	if (debug) fprintf(debugfile,"(#)console_stdio_in running...\n");
	if (debug) fflush(debugfile);

	/* Watch stdin (fd 0) to see when it has input. */
	FD_ZERO(&rfds);
	FD_SET(0, &rfds);

	while(CurrentCPURunMode != SHUTDOWN) {

		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */
		status = tty_arr[0]->in_status;
		control = tty_arr[0]->in_control;
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure console_stdio_in\n");
			CurrentCPURunMode = SHUTDOWN;
		}
        if (status & 0x0008) {
            usleep(100000);
            if (control & 1) {
                tty_arr[0]->in_status |= 1;
                genIRQ(12,1,CONSOLE_IN_ID);
            }
        }
		if ((status & 0x0004) && !(status & 0x0008)) { /* Bit 2=1 device is active */
			while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
				continue; /* Restart if interrupted by handler */
			pp=tty_arr[0]->rcv_fp;
			cp=tty_arr[0]->rcv_cp;
			if (nd_sem_post(&sem_io) == -1) { /* release io lock */
				if (debug) fprintf(debugfile,"ERROR!!! sem_post failure console_stdio_in\n");
				CurrentCPURunMode = SHUTDOWN;
			}

			/* this gets us max buffer size we can use */
			numbytes = (cp < pp) ? 256-pp+cp-2 : (pp < cp) ? cp-pp-2 : 254;
			numbytes = ( numbytes > 0) ? numbytes : 0;
            if ((*script_console)) {
                while ((*script_console)) {
                    char ch = *script_console++;
                    if (ch == '\t') {
                        char ch2 = *script_console++;
                        if (ch2 >= '0' && ch2 <= '9') {
                            select_floppy(ch2 - '0');
                        }
                    } else {
                        *recv_data = ch;
                        numread = 1;
                        retval = 1;
                        break;
                    }
                }
            } else {
                do {
                    retval = select(1, &rfds, NULL, NULL, NULL);
                    
                    /* ok lets send what we got... */
                    numread = read (0, recv_data, numbytes);
                    if (numbytes == 1 && recv_data[0] == 25) { // CTRL-Y saves CPU State
                        cpu_savestate();
                    } else {
                        break;
                    }
                } while (true);
            }
			tmp = numread;
			if (numread > 0) {
				tmp = numread;
				for (cnt=0;tmp>0; tmp--){
					ch=recv_data[cnt];

					if (debug) fprintf (debugfile,"(##) ch=%i (%c)\n",ch,ch);
					if (debug) fflush(debugfile);

					ch = (ch == 10) ? 13 : ch; /* change lf to cr */
					switch((control & 0x1800)>>11){
					case 0:/* 8 bits */
						break;
					case 1:/* 7 bits */
						ch &= 0x7f;
						break;
					case 2:/* 6 bits */
						ch &= 0x3f;
						break;
					case 3:/* 5 bits */
						ch &= 0x1f;
						break;
					}
                    if(1) { /*control & 0x4000) {	/* Bit 14=1 even parity is used */
						/* set parity to 0 for even parity or 1 for odd parity  */
						parity=0;
						for (cnt2 = 0; cnt2 < 8; cnt2++)
							parity ^= ((ch >> cnt2) & 1);
						ch = (parity) ? ch | 0x80 : ch;
                        if (debug) fprintf (debugfile,"(##) ch=%i (%c) parity=%i statusreg(bits)=%d\n",ch,ch,parity,(int)((control & 0x1800)>>11));
                    } else {
                        if (debug) fprintf (debugfile,"(##) ch=%i (%c) NO parity=%i statusreg(bits)=%d\n",ch,ch,parity,(int)((control & 0x1800)>>11));
                    }

					if (debug) fflush(debugfile);

//					if (ch == 10) { /* lf?? */
//						cnt++;
//					} else {
						while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
							continue; /* Restart if interrupted by handler */
						pp=tty_arr[0]->rcv_fp;
						tty_arr[0]->rcv_arr[pp]=ch;
						tty_arr[0]->rcv_fp++;
						if (nd_sem_post(&sem_io) == -1) { /* release io lock */
							if (debug) fprintf(debugfile,"ERROR!!! sem_post failure console_stdio_in\n");
							CurrentCPURunMode = SHUTDOWN;
						}

						cnt++;
						pp++;
//					}
				}
				while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
					continue; /* Restart if interrupted by handler */
				tty_arr[0]->in_status |= 0x0008;	/* Bit 3=1 device ready for transfer */
				if (nd_sem_post(&sem_io) == -1) { /* release io lock */
					if (debug) fprintf(debugfile,"ERROR!!! sem_post failure console_stdio_in\n");
					CurrentCPURunMode = SHUTDOWN;
				}
			}
		}
	}
}

void console_stdio_thread(void) {
	int s;
//	char ch;
	unsigned char pp,cp,tmp;

	struct ThreadChain *tc_elem;

	if (debug) fprintf(debugfile,"(#)console_stdio_thread running...\n");
	if (debug) fflush(debugfile);
	tty_arr[0] = calloc(1,sizeof(struct tty_io_data));

	tc_elem=AddThreadChain();
	pthread_attr_init(&tc_elem->tattr);
	pthread_attr_setdetachstate(&tc_elem->tattr,PTHREAD_CREATE_DETACHED);
	tc_elem->tk = CANCEL;
	pthread_create(&tc_elem->thread, &tc_elem->tattr, (void *)&console_stdio_in, NULL);

	while(CurrentCPURunMode != SHUTDOWN) {
		tty_arr[0]->out_status |= 0x0008; /* Bit 3=1 ready for transfer */
		while(CurrentCPURunMode != SHUTDOWN) {
			while ((s = nd_sem_wait(&sem_cons)) == -1 && errno == EINTR) /* wait for console lock to be free */
				continue; /* Restart if interrupted by handler */

			/* ok lets send what we got... */
			pp=tty_arr[0]->snd_fp;
			cp=tty_arr[0]->snd_cp;
            if (cp != pp) {
                while (cp != pp) {
                    printf("%c",tty_arr[0]->snd_arr[cp]);
                    cp++;
                }
                tty_arr[0]->snd_cp=cp;
            }
            if (cp == pp && (tty_arr[0]->out_control & 1)) {
                usleep(10000);
                if ((tty_arr[0]->out_control & 1) && !(tty_arr[0]->out_status & 1)) {
                    tty_arr[0]->out_status |= 1;
                    genIRQ(10,1,CONSOLE_OUT_ID);
                }
            }
		}
	}
	return;
}

void console_socket_in(int *connected) {
	int s;
	char ch,parity;
	char recv_data[1024];
	int numbytes;
	ushort status,control;
	unsigned char pp,cp,tmp,cnt,cnt2;
	int numread;
	fd_set rfds;
	int fdmax;
	int retval;
	int throw =0;

	parity = 0;
	fdmax = *connected;

	if (debug) fprintf(debugfile,"(#)console_socket_in running...\n");
	if (debug) fflush(debugfile);

	/* Watch socket to see when it has input. */
	FD_ZERO(&rfds);
	FD_SET(*connected, &rfds);

	while(CurrentCPURunMode != SHUTDOWN) {
		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */
		status = tty_arr[0]->in_status;
		control = tty_arr[0]->in_control;
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure console_stdio_in\n");
			CurrentCPURunMode = SHUTDOWN;
		}
		if (status & 0x0004) { /* Bit 2=1 device is active */
			while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
				continue; /* Restart if interrupted by handler */
			pp=tty_arr[0]->rcv_fp;
			cp=tty_arr[0]->rcv_cp;
			if (nd_sem_post(&sem_io) == -1) { /* release io lock */
				if (debug) fprintf(debugfile,"ERROR!!! sem_post failure console_stdio_in\n");
				CurrentCPURunMode = SHUTDOWN;
			}

			/* this gets us max buffer size we can use */
			numbytes = (cp < pp) ? 256-pp+cp-2 : (pp < cp) ? cp-pp-2 : 254;
			numbytes = ( numbytes > 0) ? numbytes : 0;

			retval = select(fdmax+1, &rfds, NULL, NULL, NULL);

			/* ok lets send what we got... */

			numread = recv(*connected,recv_data,numbytes,0);
			if (numread > 0) {
				tmp = numread;
				for (cnt=0;tmp>0; tmp--){
					ch=recv_data[cnt];
					if((unsigned int)ch == 255) { /* Telnet IAC command */
						throw=2;
						cnt++;
					} else if (throw) { /* previous IAC command, throw out 2 chars after */
						throw--;
						cnt++;
					} else {
						if (debug) fprintf (debugfile,"(##) ch=%i (%c)\n",ch,ch);
						if (debug) fflush(debugfile);
//						ch = (ch == 10) ? 13 : ch; /* change lf to cr */
						ch &= 0x7f;	/* trim to 7 bit ASCII  FIXME:: This should depend on bits 11-12 in control reg*/
						if(tty_arr[0]->in_control & 0x4000){	/* Bit 14=1 even parity is used */
							/* set parity to 0 for even parity or 1 for odd parity  */
							parity=0;
							for (cnt2 = 0; cnt2 < 8; cnt2++)
								parity ^= ((ch >> cnt2) & 1);
							ch = (parity) ? ch | 0x80 : ch;
						}
						if (debug) fprintf (debugfile,"(##) ch=%i (%c) parity=%i\n",ch,ch,parity);
						if (debug) fflush(debugfile);
//						if (ch == 10) { /* lf?? */
//							cnt++;
//						} else {
							tty_arr[0]->rcv_arr[pp]=ch;
							cnt++;
							pp++;
//						}
					}
				}
				tty_arr[0]->rcv_fp=pp;
				tty_arr[0]->in_status |= 0x0008;	/* Bit 3=1 device ready for transfer */
			}
		}
	}
}

void console_socket_thread(void) {
	int s;
	int sock, connected;
	char send_data [1024];
	struct sockaddr_in client_addr;
	socklen_t sin_size;
	int numbytes;
	unsigned char pp,cp,tmp;

	/* IAC WILL ECHO IAC WILL SUPPRESS-GO-AHEAD IAC DO SUPPRESS-GO-AHEAD */
	char telnet_setup[9] = {0xff,0xfb,0x01,0xff,0xfb,0x03,0xff,0xfd,0x0f3};

	struct ThreadChain *tc_elem;

	if (debug) fprintf(debugfile,"(#)console_socket_thread running...\n");
	if (debug) fflush(debugfile);
	tty_arr[0] = calloc(1,sizeof(struct tty_io_data));

	do_listen(5101, 1, &sock);
	if (debug) fprintf(debugfile,"\n(#)TCPServer Waiting for client on port 5101\n");
	if (debug) fflush(debugfile);

	while(CurrentCPURunMode != SHUTDOWN) {
		sin_size = (socklen_t) sizeof(struct sockaddr_in);
		connected = accept(sock, (struct sockaddr *)&client_addr,&sin_size);
		if (debug) fprintf(debugfile,"(#)I got a console connection from (%s , %d)\n",
			inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));
		if (debug) fflush(debugfile);

		/* setup the other side to "uncooked" data */
		send(connected,telnet_setup,9,0);

		tc_elem=AddThreadChain();
		pthread_attr_init(&tc_elem->tattr);
		pthread_attr_setdetachstate(&tc_elem->tattr,PTHREAD_CREATE_DETACHED);
		tc_elem->tk = CANCEL;
		pthread_create(&tc_elem->thread, &tc_elem->tattr, (void *)&console_socket_in, &connected);

		tty_arr[0]->out_status |= 0x0008; /* Bit 3=1 ready for transfer */
		while(CurrentCPURunMode != SHUTDOWN) {
			while ((s = nd_sem_wait(&sem_cons)) == -1 && errno == EINTR) /* wait for console lock to be free */
				continue; /* Restart if interrupted by handler */

			/* ok lets send what we got... */
			pp=tty_arr[0]->snd_fp;
			cp=tty_arr[0]->snd_cp;
			numbytes=0;
			if (cp != pp) {

				numbytes= (cp < pp) ? pp-cp : 256-cp+pp;
				for(tmp=0;tmp<numbytes;tmp++) {
					send_data[tmp]=tty_arr[0]->snd_arr[cp];
					cp++;
				}
				tty_arr[0]->snd_cp=cp;
			}
			if (numbytes) send(connected,send_data,numbytes,0);
		}
	}
	close(sock);
	return;
}

/*
Floppy_IO: IOX 880 - A=0
Floppy_IO: IOX 880 - A=0
Floppy_IO: IOX 882 - A=880
Floppy_IO: IOX 883 - A=2
Floppy_IO: IOX 882 - A=10831
Floppy_IO: IOX 882 - A=2
*/
void floppy_thread(void){
	int s;
	struct floppy_data *dev;
    struct fdd_unit *unit = NULL;
    
	while(CurrentCPURunMode != SHUTDOWN) {
		while ((s = nd_sem_wait(&sem_floppy)) == -1 && errno == EINTR) /* wait for floppu lock to be free and take it */
			continue; /* Restart if interrupted by handler */
        usleep(100);
		/* Do our stuff now and then return and wait for next freeing of lock. */
		dev = iodata[880];	/*TODO:: This is just a temporary solution!!! */
        if(dev->selected_drive != -1) {
            unit = dev->unit[dev->selected_drive];
        }

		while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
			continue; /* Restart if interrupted by handler */

		if (dev->busy) { /* command is upper 8 bit of written IOX call */
			if(dev->command & (1<<7)) { 		    /* 128: CONTROL RESET */
                dev->busy = false;
			} else if (dev->command & (1<<6)) {		/* 64: RECALIBRATE */
                dev->track = 0;
                dev->seekComplete = true;
                dev->busy = false;
				/* track = 0, interrupt!, set status seek complete */
			} else if (dev->command & (1<<5)) {		/* 32: SEEK */
                int oldTrack = dev->track;
                if (unit != NULL) {
                    if (unit->dir_track == 1) {
                        dev->track += unit->diff_track;
                        if (dev->track > 77) dev->track = 77;
                    } else {
                        dev->track -= unit->diff_track;
                        if (dev->track < 0) dev->track = 0;
                    }
                    if(debug) fprintf(debugfile, "Floppy: seek from %d to %d\n",oldTrack,dev->track);
                } else {
                    if(debug) fprintf(debugfile, "Floppy: seek unit==NULL\n");
                }
                dev->seekComplete = true;
                dev->busy = false;
				/* track = nn, interrupt!, set status seek complete */
			} else if (dev->command & (1<<3)) {		/* 8: READ ID: Track 0 Sector 2 */
                if (debug) fprintf(debugfile, "Floppy: read ID Track=%d, Sector=%d\n",dev->track,dev->sector);
                dev->buff[0] = dev->track << 8 ;
                dev->buff[1] = (dev->sector << 8) | 0x02;       // Low byte = 0x2
                dev->rwComplete = true;
                dev->busy = false;
			} else if (dev->command & (1<<4)) {		/* 16: READ DATA */
                if (debug) fprintf(debugfile,"Floppy: read DATA Track=%d, Sector=%d Buf=%d\n",dev->track,dev->sector,dev->bufptr_read);
                int xsector = dev->sector -1;
                if (xsector < 0) xsector = 0;
                sectorread (diskNumber, dev->track, 0, xsector, &dev->buff[dev->bufptr_read]);
                dev->bufptr_read = ( dev->bufptr_read + 256 ) % FDD_BUFSIZE;
                dev->rwComplete = true;
                dev->busy = false;
			} else if ((dev->command & (1<<2)) || (dev->command & (1<<1))) {		/* 4: WRITE DATA */
                if (debug) fprintf(debugfile,"Floppy: write DATA Track=%d, Sector=%d Buf=%d\n",dev->track,dev->sector,dev->bufptr_read);
                int xsector = dev->sector -1;
                if (xsector < 0) xsector = 0;
                sectorwrite (diskNumber, dev->track, 0, xsector, &dev->buff[dev->bufptr_read]);
                dev->bufptr_read = ( dev->bufptr_read + 256 ) % FDD_BUFSIZE;
                dev->rwComplete = true;
                dev->busy = false;
			} else if (dev->command & (1<<0)) {		/* 1: FORMAT TRACK */
                dev->busy = false;
			}
            if (!dev->busy) {
                if (dev->irq_en) {
                    genIRQ(11,021,FLOPPY_ID);
                }
                if (nd_sem_post(&sem_floppy) == -1) { /* release io lock */
                    if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
                    CurrentCPURunMode = SHUTDOWN;
                }
            }
		}
		if (nd_sem_post(&sem_io) == -1) { /* release io lock */
			if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
			CurrentCPURunMode = SHUTDOWN;
		}
	}
}
extern _NDRAM_        VolatileMemory;

/*
Disk_IO: IOX 1540
*/
void hdd_thread(void){
    int s;
    struct hdd_data *dev;
    
    while(CurrentCPURunMode != SHUTDOWN) {
        while ((s = nd_sem_wait(&sem_hdd)) == -1 && errno == EINTR) /* wait for hdd lock to be free and take it */
            continue; /* Restart if interrupted by handler */
        usleep(5000);
        /* Do our stuff now and then return and wait for next freeing of lock. */
        dev = iodata[01540];    /*TODO:: This is just a temporary solution!!! */

        while ((s = nd_sem_wait(&sem_io)) == -1 && errno == EINTR) /* wait for io lock to be free and take it */
            continue; /* Restart if interrupted by handler */
        if (dev->active) {
            int hdd_file = open("hdd.img",O_RDWR|O_CREAT);
            // 10MB long logical = (dev->sector | (dev->track << 5) | (dev->surface << 14)) & 0xffff;
            long numSect = 18;
            long numSurf = 5;
            long numTrack = 823; // 75MB
            long logical = (dev->sector) + (dev->surface * numSect) + (dev->track * numSect * numSurf); // 75MB
            logical *= 1024;
            int bytesToRead = 2*dev->word_count;
            ushort *addr = &VolatileMemory.n_Array[dev->mem_addr];
            off_t seekFail = lseek(hdd_file, logical, SEEK_SET);
            if (seekFail >= 0) {
                size_t result = 0;
                switch (dev->opcode) {
                    case 0: // Read Transfer
                        result = read(hdd_file, addr, bytesToRead);
                        if (debug) {
                            fprintf(debugfile,"HDD_IO: read at 0x%lx -> 0%o %ld, errno=%d\n",logical,dev->mem_addr,result,errno);
                        }
                        dev->mem_addr += dev->word_count;
                        break;
                    case 1: // Write Transfer
                        result = write(hdd_file, addr, bytesToRead);
                        if (debug) {
                            fprintf(debugfile,"HDD_IO: write from 0%o -> 0x%lx %ld, errno=%d\n",dev->mem_addr,logical,result,errno);
                        }
                        dev->mem_addr += dev->word_count;
                        break;
                    case 2: // Read Parity
                        break;
                    case 3: // Compare Test
                        
                        break;
                    case 4: // Initiate Seek
                        
                        break;
                    case 5: // Write Format
                        
                        break;
                    case 6: // Seek complete search
                        
                        break;
                    case 7: // Seek to zero
                        
                        break;
                    default: if (debug) fprintf(debugfile,"HDD_IO: Invalid opcode: %d\n",dev->opcode);
                        break;
                }
            } else {
                if (debug) fprintf(debugfile,"HDD_IO: SEEK FAILED result %lld, errno=%d\n",seekFail,errno);
            }
            close(hdd_file);
            dev->active = false;
            dev->finished = true;
            if (!dev->active) {
                if (dev->irq_en) {
                    genIRQ(11,017,HDD_IRQ_ID);
                }
                if (nd_sem_post(&sem_hdd) == -1) { /* release io lock */
                    if (debug) fprintf(debugfile,"ERROR!!! sem_post failure HDD_IO\n");
                    CurrentCPURunMode = SHUTDOWN;
                }
            }
        }
        if (nd_sem_post(&sem_io) == -1) { /* release io lock */
            if (debug) fprintf(debugfile,"ERROR!!! sem_post failure Floppy_IO\n");
            CurrentCPURunMode = SHUTDOWN;
        }
    }
}

void panel_thread(void) {
	int s;
    int sock, connected;
    long int bytes_recieved;
	char recv_data[1024];
	struct sockaddr_in client_addr;
	socklen_t sin_size;

	if (debug) fprintf(debugfile,"(#)panel_thread running...\n");
	if (debug) fflush(debugfile);

	do_listen(5100, 1, &sock);
	if (debug) fprintf(debugfile,"\n(#)TCPServer Waiting for client on port 5100\n");
	if (debug) fflush(debugfile);

	while(CurrentCPURunMode != SHUTDOWN) {
		sin_size = (socklen_t) sizeof(struct sockaddr_in);
		connected = accept(sock, (struct sockaddr *)&client_addr,&sin_size);
		if (debug) fprintf(debugfile,"(#)I got a panel connection from (%s , %d)\n",
			inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));
		if (debug) fflush(debugfile);
		while(CurrentCPURunMode != SHUTDOWN) {
			bytes_recieved = recv(connected,recv_data,1024,0);
			recv_data[bytes_recieved] = '\0';
			if (debug) fprintf(debugfile,"(#)PANEL DATA received\n");
			if(strncmp("OPCOM_PRESSED\n",recv_data,strlen("OPCOM_PRESSED"))==0){
				MODE_OPCOM=1;
				if (debug) fprintf(debugfile,"(#)OPCOM_PRESSED\n");

			} else if(strncmp("MCL_PRESSED\n",recv_data,strlen("MCL_PRESSED"))==0){
				if (debug) fprintf(debugfile,"(#)MCL_PRESSED\n");
				/* TODO:: this should be in a separate routine DoMCL later */
				CurrentCPURunMode = STOP;
				/* NOTE:: buggy in that we cannot do STOP and MCL without a running cpu between.. FIXME */
				while ((s = nd_sem_wait(&sem_stop)) == -1 && errno == EINTR) /* wait for stop lock to be free and take it */
					continue; /* Restart if interrupted by handler */
				bzero(gReg,sizeof(struct CpuRegs));	/* clear cpu */
				nd_setbit(_STS,_O,1);
				setbit_STS_MSB(_N100,1);
				gCSR = 1<<2;    /* this bit sets the cache as not available */

			} else if(strncmp("LOAD_PRESSED\n",recv_data,strlen("LOAD_PRESSED"))==0){
				if (debug) fprintf(debugfile,"(#)LOAD_PRESSED\n");
				gPC=STARTADDR;
				CurrentCPURunMode = RUN;
				if (nd_sem_post(&sem_run) == -1) { /* release run lock */
					if (debug) fprintf(debugfile,"ERROR!!! sem_post failure panel_thread\n");
					CurrentCPURunMode = SHUTDOWN;
				}
			} else if(strncmp("STOP_PRESSED\n",recv_data,strlen("STOP_PRESSED"))==0){
				if (debug) fprintf(debugfile,"(#)STOP_PRESSED\n");
				CurrentCPURunMode = STOP;
				/* NOTE:: buggy in that we cannot do STOP and MCL without a running cpu between.. FIXME */
				while ((s = nd_sem_wait(&sem_stop)) == -1 && errno == EINTR) /* wait for stop lock to be free and take it */
					continue; /* Restart if interrupted by handler */
			} else {
				if (debug) fprintf(debugfile,"(#)Panel received:%s\n",recv_data);
			}
			if (debug) fflush(debugfile);
		}
	}
	close(sock);
	return;
}

void setup_pap(void){
	gPANS=0x8000;	/* Tell system we are here */
	gPANS=gPANS | 0x4000;	/* Set FULL which is active low, so not full */
	gPAP = calloc(1,sizeof(struct display_panel));
}

void panel_event(void){
	char tmpbyte;

	if (gPAP->trr_panc) {	/* TRR has been issued, process command */
		if (debug) fprintf(debugfile,"panel_event: TRR\n");
		if (debug) fflush(debugfile);
		gPAP->trr_panc = false;
		switch ((gPANC & 0x0700)>>8) {
		case 0:		/* Illegal */
			break;
		case 1:		/* Future extension */
			break;
		case 2:		/* Message Append */	// TODO: Not Implemented yet except basic return info
			gPANS = 0xd200;
			break;
		case 3:		/* Message Control */	// TODO: Not Implemented yet except basic return info
			gPANS = 0xd300;
			break;
		case 4:		/* Update Low Seconds */
			if (gPANC & 0x2000){	/* Read */
				tmpbyte = (gPAP->seconds) & 0x00ff;
				gPANS = 0xf400 | tmpbyte;
			} else {		/*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->seconds = (gPAP->seconds & 0xff00) | tmpbyte;
				gPANS = 0xd400;
			}
			break;
		case 5:		/* Update High Seconds */
			if (gPANC & 0x2000){	/* Read */
				tmpbyte = (gPAP->seconds >> 8);
				gPANS = 0xf500 | tmpbyte;
			} else {		/*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->seconds = (gPAP->seconds & 0x00ff) | ((ushort)tmpbyte)<<8;
				gPANS = 0xd500;
			}
			break;
		case 6:		/* Update Low Days */
			if (gPANC & 0x2000){	/* Read */
				tmpbyte = (gPAP->days) & 0x00ff;
				gPANS = 0xf600 | tmpbyte;
			} else {		/*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->days = (gPAP->days & 0xff00) | tmpbyte;
				gPANS = 0xd600;
			}
			break;
		case 7:		/* Update High Days */
			if (gPANC & 0x2000){	/* Read */
				tmpbyte = (gPAP->days >> 8);
				gPANS = 0xf700 | tmpbyte;
			} else {		/*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->days = (gPAP->days & 0x00ff) | ((ushort)tmpbyte)<<8;
				gPANS = 0xd700;
			}
			break;
		default :	/* This should never happen */
			break;
		}
		if (debug) fprintf(debugfile,"panel_event: TRR - result: gPANS = %0x04\n",gPANS);
		if (debug) fflush(debugfile);
	}
	if (gPAP->sec_tick) {	/* Seconds tick from rtc, update counters */
		if (debug) fprintf(debugfile,"panel_event: 1 second tick\n");
		if (debug) fflush(debugfile);
		gPAP->sec_tick = false;
		gPAP->seconds++;
		if (gPAP->seconds >= 43200){	/* 12h wraparound */
			gPAP->seconds = 0;
			gPAP->days++;
		}
	}

}

void panel_processor_thread(void) {
	int s;
	while(CurrentCPURunMode != SHUTDOWN) {
		while ((s = nd_sem_wait(&sem_pap)) == -1 && errno == EINTR) /* wait for pap 'kick' */
			continue; /* Restart if interrupted by handler */

		panel_event();
	}
	return;
}
