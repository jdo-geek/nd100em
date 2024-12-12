/*
 * nd100em - ND100 Virtual Machine
 *
 * Copyright (c) 2008-2016 Roger Abrahamsson
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "floppy.h"
extern int debug;
extern FILE* debugfile;

/*
 * int sectorread (cyl, side, sector, *addr)
 * cyl can be 0-76, side 0-1, sector 1-8...
 * Reads an ND100 format floppy. 8 sectors per side, 2 sides per track, 77 tracks
 * each sector has an 8 byte sectorinfo in the format of:
 * +------+------+------+------+------+------+----------+
 * | ACYL | ASID | LCYL | LSID | LSEC | LLEN |  COUNT   |
 * +------+------+------+------+------+------+----------+
 *  ACYL      Actual cylinder, 1 byte
 *  ASID      Actual side, 1 byte
 *  LCYL      Logical cylinder; cylinder as read, 1 byte
 *  LSID      Logical side; or side as read, 1 byte
 *  LSEC      Sector number as read, 1 byte
 *  LLEN      Length code as read, 1 byte
 *  COUNT     Byte count of data to follow,  2 bytes.   If zero, no data is contained in this sector.
 *
 */
int oldsectorread (char cyl, char side, char sector, unsigned short *addr) {
	FILE *floppy_file;
	char floppyimage[]="floppy.nd100.img";
	char loadtype[]="r+";
	int offset, flat_sector;

	flat_sector = (((int)cyl*2)+((int)side))*8+((int)sector-1);
	offset=flat_sector*1032;
	offset+=8;

	floppy_file=fopen(floppyimage,loadtype);
	fseek(floppy_file,offset,SEEK_SET);
	fread(addr,2,512,floppy_file);
	fclose(floppy_file);
	return 0;
}

/*
 * imd_check
 * check that a file starts with the three magic chars 'IMD'
 * returns: -1 - open failed
 *           0 - not an IMD file
 *           1 - IMD magic marker found
 */
int imd_check(char *imgname) {
	FILE *fp;
	char buf[16];
	int res;

	fp = fopen(imgname,"r");
	if (fp == NULL)
		return -1;

	if (fread(buf, 1, 3, fp) == 3){
		res = ((buf[0]=='I') && (buf[1]=='M') && (buf[2]=='D')) ? 1 : 0;
	} else
		res = -1;

	fclose(fp);
	return res;
}

/*
 * imd_sectorread
 * Reads a sector from an Imagedisk IMD floppy image.
 * returns -1 if failed, or not an IMD image
 */
int imd_sectorread (char cyl, char side, char sector, unsigned short *addr, char *imgname) {
	FILE *fp;
	char buf[256];
	int i,j;
    int imod = 0;
    int icyl = 0;
    int ihead = 0;
    int isecs = 0;
    int isecsize = 0;
    int secsize = 0;
    int result = 0;
    
	if (!(imd_check(imgname)>0))
		return -1;

	fp = fopen(imgname,"r");
	if (fp == NULL)
		return -1;

	/* Attempt to read until end of header comment block*/
	/* no error handling yet really */
	while (fread(buf, 1, 1, fp) == 1) {
		if (buf[0] == 0x1a)
			break;

	}

    int cylinder = 0;
    int found = 0;
    while (feof(fp) == 0) {
        if (fread(buf, 1, 5, fp) == 5){
            imod = buf[0];
            icyl = buf[1];
            ihead = buf[2];
            isecs = buf[3];
            isecsize = buf[4];

            secsize= (0x80 << isecsize);
        } else {
            break;
        }

        if (ihead & 0x80){ /* Has a sector cylinder map */
        }
        if (ihead & 0x80){ /* Has a sector head map */
        }
        /* read sector numbering map */
        for(i=0; i < isecs; i++){
            if (fread(buf, 1, 1, fp) == 1) {
            }
        }
        
        /* read sector data records */
        for(i=0; i < isecs; i++){
            found = (cyl == cylinder && side == (ihead & 1) && sector == i);
            if (fread(buf, 1, 1, fp) == 1) {
                switch(buf[0]){
                    case 0x00: /* Sector data unavailable - could not be read: 0 bytes follow*/
                        result = -1;
                        break;
                    case 0x01: /* Normal data: sector size bytes follow */
                        for(j=0; j < secsize/2; j++){
                            if (fread(buf, 2, 1, fp) == 1) {
                                if (found) {
                                    unsigned short b2 = buf[0] & 0xff;
                                    unsigned short b1 = buf[1] & 0xff;
                                    *addr= b1 | (b2 << 8);
                                    addr++;
                                }
                            }
                        }
                        break;
                    case 0x02: /* Compressed - all bytes in sector have same value: 1 bytes follow */
                        if (fread(buf, 1, 1, fp) == 1) {
                            if (found) {
                                for (int j = 0; j < 256; j++) {
                                    *addr = (buf[0] | buf[0] << 8);
                                    addr++;
                                }
                            }
                        }
                        break;
                    case 0x03: /* Data with deleted-data address mark: sector size bytes follow */
                        for(j=0; j < secsize/2; j++){
                            if (fread(buf, 2, 1, fp) == 1) {
                                unsigned short b1 = buf[0] & 0xff;
                                unsigned short b2 = buf[1] & 0xff;
                                *addr= b1 | (b2 << 8);
                                addr++;
                            }
                        }
                        break;
                    case 0x04: /* Compressed data with deleted-data address mark: 1 byte follow */
                        if (fread(buf, 1, 1, fp) == 1) {
                            if (found) {
                                for (int j = 0; j < 256; j++) {
                                    *addr = (buf[0] | buf[0] << 8);
                                    addr++;
                                }
                            }
                        }
                        break;
                    case 0x05: /* Data with read error on original: sector size bytes follow */
                        for(j=0; j < secsize; j++){
                            if (fread(buf, 1, 1, fp) == 1)
                                printf("%02x ",((int)buf[0] & 0x00ff));
                        }
                        printf("\n");
                        result = -1;
                        break;
                    case 0x06: /* Compressed data with read error on original : 1 byte follow */
                        if (fread(buf, 1, 1, fp) == 1)
                            printf("%02x ",((int)buf[0] & 0x00ff));
                        result = -1;
                        break;
                    case 0x07: /* Deleted data with read error on original: sector size bytes follow */
                        printf("%02x: \n",buf[0]);
                        for(j=0; j < secsize; j++){
                            if (fread(buf, 1, 1, fp) == 1)
                                printf("%02x ",((int)buf[0] & 0x00ff));
                        }
                        printf("\n");
                        result = -1;
                        break;
                    case 0x08: /* Deleted data with read error on original, compressed: 1 byte follow */
                        if (fread(buf, 1, 1, fp) == 1)
                            printf("%02x ",((int)buf[0] & 0x00ff));
                        result = -1;
                        break;
                    default:
                        printf("***ERROR***\n");
                        break;
                }
            }
            if (found) break;
        }
        cylinder++;
        if (found) break;
    }

	fclose(fp);
    return result;
}

int imd_sectordump (char cyl, char side, char sector, unsigned short *addr, char *imgname) {
    FILE *fp;
    char buf[256];
    int i,j;
    int imod = 0;
    int icyl = 0;
    int ihead = 0;
    int isecs = 0;
    int isecsize = 0;
    int secsize = 0;

    if (!(imd_check(imgname)>0))
        return -1;

    fp = fopen(imgname,"r");
    if (fp == NULL)
        return -1;

    /* Attempt to read until end of header comment block*/
    /* no error handling yet really */
    while (fread(buf, 1, 1, fp) == 1) {
        if (buf[0] == 0x1a)
            break;

    }

    int cylinder = 0;
    while (feof(fp) == 0) {
        if (fread(buf, 1, 5, fp) == 5){
            imod = buf[0];
            icyl = buf[1];
            ihead = buf[2];
            isecs = buf[3];
            isecsize = buf[4];

            secsize= (0x80 << isecsize);
        } else {
            printf("End Of File\n");
            break;
        }

        if (ihead & 0x80){ /* Has a sector cylinder map */
        }
        if (ihead & 0x80){ /* Has a sector head map */
        }
            printf("MODE:        %02x \n",imod);
            printf("CYLINDER:    %02x \n",icyl);
            printf("HEAD:        %02x \n",ihead);
            printf("SECTORS:     %02x \n",isecs);
            printf("SECTOR SIZE: %02x \n",isecsize);
            
            printf("SECTOR Bytes: %d\n",secsize);
            printf("SECTOR NUMBERING MAP:\n");
        /* read sector numbering map */
        for(i=0; i < isecs; i++){
            if (fread(buf, 1, 1, fp) == 1) {
                printf("%02x ",buf[0]);
            }
        }
        printf("\n");
        printf("SECTOR DATA RECORDS CYLINDER %d:\n",cylinder);
        /* read sector data records */
        for(i=0; i < isecs; i++){
            if (fread(buf, 1, 1, fp) == 1) {
                switch(buf[0]){
                    case 0x00: /* Sector data unavailable - could not be read: 0 bytes follow*/
                        printf("%02x: \n",buf[0]);
                        break;
                    case 0x01: /* Normal data: sector size bytes follow */
                        printf("%02x: \n",buf[0]);
                        for(j=0; j < secsize; j++){
                            if (fread(buf, 1, 1, fp) == 1)
                                printf("%02x ",((int)buf[0] & 0x00ff));
                        }
                        break;
                    case 0x02: /* Compressed - all bytes in sector have same value: 1 bytes follow */
                        if (fread(buf, 1, 1, fp) == 1)
                            printf("%02x ",((int)buf[0] & 0x00ff));
                        break;
                    case 0x03: /* Data with deleted-data address mark: sector size bytes follow */
                        printf("%02x: \n",buf[0]);
                        for(j=0; j < secsize; j++){
                            if (fread(buf, 1, 1, fp) == 1)
                                printf("%02x ",((int)buf[0] & 0x00ff));
                        }
                        printf("\n");
                        break;
                    case 0x04: /* Compressed data with deleted-data address mark: 1 byte follow */
                        if (fread(buf, 1, 1, fp) == 1)
                            printf("%02x ",((int)buf[0] & 0x00ff));
                        break;
                    case 0x05: /* Data with read error on original: sector size bytes follow */
                        printf("%02x: \n",buf[0]);
                        for(j=0; j < secsize; j++){
                            if (fread(buf, 1, 1, fp) == 1)
                                printf("%02x ",((int)buf[0] & 0x00ff));
                        }
                        printf("\n");
                        break;
                    case 0x06: /* Compressed data with read error on original : 1 byte follow */
                        if (fread(buf, 1, 1, fp) == 1)
                            printf("%02x ",((int)buf[0] & 0x00ff));
                        break;
                    case 0x07: /* Deleted data with read error on original: sector size bytes follow */
                        printf("%02x: \n",buf[0]);
                        for(j=0; j < secsize; j++){
                            if (fread(buf, 1, 1, fp) == 1)
                                printf("%02x ",((int)buf[0] & 0x00ff));
                        }
                        printf("\n");
                        break;
                    case 0x08: /* Deleted data with read error on original, compressed: 1 byte follow */
                        if (fread(buf, 1, 1, fp) == 1)
                            printf("%02x ",((int)buf[0] & 0x00ff));
                        break;
                    default:
                        printf("***ERROR***\n");
                        break;
                }
            }
        }
        printf("\n");
        cylinder++;
    }

    fclose(fp);
    return 0;
}

/*
         - For each track on the disk:
            1 byte  Mode value                  (0-5)
            1 byte  Cylinder                    (0-n)
            1 byte  Head                        (0-1)   (see Note)
            1 byte  number of sectors in track  (1-n)
            1 byte  sector size                 (0-6)
            sector numbering map                * number of sectors
            sector cylinder map (optional)      * number of sectors
            sector head map     (optional)      * number of sectors
            sector data records                 * number of sectors

*/
/**
    Sector starts at 0 !
 */
int sectorread (int diskNumber, char cyl, char side, char sector, unsigned short *addr) {
    FILE *floppy_file;
    char floppyimage[256];
    snprintf(floppyimage, sizeof(floppyimage), "floppy.nd100.%02d.img", diskNumber);
    char loadtype[]="r+";
    int offset, flat_sector;
    /*
    return imd_sectorread(cyl, side, sector, addr, floppyimage);
     */
    int i=0;
    unsigned short tmp,tmp2;

    // flat_sector = (((int)cyl*2)+((int)side))*8+((int)sector);
    flat_sector = (((int)cyl)+((int)side))*8+((int)sector);
    offset=flat_sector*512;
    //offset+=8;

    floppy_file=fopen(floppyimage,loadtype);
    if (floppy_file == NULL) {
        perror(floppyimage);
        return 0;
    }
    int result = fseek(floppy_file,offset,SEEK_SET);
    if (debug) fprintf(debugfile, "Floppy: Read %s at 0x%x res=%d\n",floppyimage,offset,result);

    while (i < 256) {   /* Sector 512 bytes -> 256 words */
        fread(&tmp,2,1,floppy_file);
        tmp2=(tmp & 0xff00)>>8;
        tmp2= tmp2 | ((tmp & 0x00ff) << 8);
        *addr=tmp2;
        addr++;
        i++;
    }

    fclose(floppy_file);
    return 0; 
}

int sectorwrite (int diskNumber, char cyl, char side, char sector, unsigned short *addr) {
    FILE *floppy_file;
    char floppyimage[256];
    snprintf(floppyimage, sizeof(floppyimage), "floppy.nd100.%02d.img", diskNumber);
    char loadtype[]="r+";
    int offset, flat_sector;
    /*
    return imd_sectorread(cyl, side, sector, addr, floppyimage);
     */
    int i=0;
    unsigned short tmp,tmp2;

    // flat_sector = (((int)cyl*2)+((int)side))*8+((int)sector);
    flat_sector = (((int)cyl)+((int)side))*8+((int)sector);
    offset=flat_sector*512;
    //offset+=8;

    floppy_file=fopen(floppyimage,loadtype);
    if (floppy_file == NULL) {
        perror(floppyimage);
        return 0;
    }
    int result = fseek(floppy_file,offset,SEEK_SET);
    if (debug) fprintf(debugfile, "Floppy: Write %s at 0x%x res=%d\n",floppyimage,offset,result);

    while (i < 256) {   /* Sector 512 bytes -> 256 words */
        tmp = *addr;
        tmp2=(tmp & 0xff00)>>8;
        tmp2= tmp2 | ((tmp & 0x00ff) << 8);
        addr++;
        i++;
        fwrite(&tmp2,2,1,floppy_file);
    }

    fclose(floppy_file);
    return 0;
}

