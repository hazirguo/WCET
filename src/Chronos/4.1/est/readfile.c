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
 * $Id: readfile.c,v 1.2 2006/06/24 08:54:57 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdio.h>
#include "cfg.h"
#include "isa.h"


extern prog_t	prog;
extern isa_t	*isa;



// read & decode object code, then store them in prog
void 
read_code(char *fname)
{
    read_code_ss(fname);
    //dump_code();
}








void
dump_code()
{
    int		i, j;
    de_inst_t	*inst;

    for (i = 0 ; i < prog.num_inst; i++) {
	inst = &prog.code[i];
	printf("%8x %-10s", inst->addr, isa[inst->op_enum].name);
	for (j = 0; j < inst->num_out; j++)
	    printf(" O%d", inst->out[j]);
	for (j = 0; j < inst->num_in; j++)
	    printf(" I%d", inst->in[j]);
	if (inst->target != 0)
	    printf(" target: %x", inst->target);
	printf("\n");
    }
}
