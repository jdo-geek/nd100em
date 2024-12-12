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

#ifndef cpu_state_h
#define cpu_state_h
/**
    Loads or saves the current CPU state.
 */
void cpustate(bool load,
            ulong        *instr_counter,
            _NDRAM_      *VolatileMemory,
            struct CpuRegs *gReg,
            union NewPT *gPT,
            struct IdentChain **gIdentChain);

#endif /* cpu_state_h */
