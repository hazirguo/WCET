/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Copyright (C) 2005 Xianfeng Li
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * $Id: isa.c,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdio.h>
#include "isa.h"



isa_t	*isa;		// info of the instruction types of the ISA
int	num_isa;	// number of instruction types of the ISA



// initiate ISA info
void
init_isa()
{
    // if SimpleScalar is used, call this to init SimpleScalar ISA info
    init_isa_ss();
    //dump_isa();
}



// return (decoded) instruction type
inline int
inst_type(de_inst_t *inst)
{
    return isa[inst->op_enum].type;
}



int
max_inst_lat(de_inst_t *inst)
{
    int	    fu;

    return ss_max_inst_lat(inst);
}









// dump functions for debug usage
//==============================================================================

void
dump_isa()
{
    int	    i;

    for (i = 0; i < num_isa; i++)
	printf("%3d: %-10s type %x\n", i, isa[i].name, isa[i].type);
}
