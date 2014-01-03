
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
 * $Id: readfile.h,v 1.1.1.1 2005/08/26 02:45:21 lixianfe Exp $
 *
 ******************************************************************************/


#ifndef READ_FILE_H
#define READ_FILE_H


#include "machine.h"


/* program text (code) segment base */
extern md_addr_t ld_text_base;

/* program text (code) size in bytes */
extern unsigned int ld_text_size;

md_inst_t * readcode(char *fname);
void print_code(md_inst_t *code, int size, unsigned entry);

void read_symbol_table(char * fname, md_inst_t *code);
int  decode_function(FILE * fdis, md_inst_t inst,md_addr_t pc, char * procname);
int cmp_adr(const void *m1, const void *m2);
#endif
