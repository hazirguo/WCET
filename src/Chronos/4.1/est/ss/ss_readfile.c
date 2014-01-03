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
 * $Id: ss_readfile.c,v 1.2 2006/06/24 08:55:06 lixianfe Exp $
 *
 ******************************************************************************/

#include <stdio.h>
#include "ecoff.h"
#include "symbol.h"
#include "../cfg.h"
#include "../common.h"


// variables from readfile.c
extern prog_t	    prog;
addr_t		*procs_addr; 
int		num_procs;



/* amount of tail padding added to all loaded text segments */
#define TEXT_TAIL_PADDING 128

/* program text (code) segment base */
md_addr_t	ld_text_base = 0;
/* program text (code) size in bytes */
unsigned int	ld_text_size = 0;
unsigned	text_entry;
unsigned	text_offset;
extern struct	sym_sym_t **sym_textsyms;
extern int	sym_ntextsyms;



// tell from the name of the function whether it is a library function, if so, it
// means no subsequent user functions
static int
is_lib_func(char *func_name)
{
    if ((strcmp(func_name, "__do_global_dtors") == 0)
	|| (strcmp(func_name, "__libc_init") == 0))
	return 1;
    else
	return 0;
}



// locate the start & end addr of the user code by excluding library functions,
// in addition, locate the main() entrance

/* HBK: modify to read more information of procs & vars */
static void
lookup_addr(char *fname)
{
    int     dbg = 0;
    int		i, size = 0; 
    int     max_num_vars = 16,max_num_procs = 16;
    struct  sym_sym_t	*sym;

    sym_loadsyms(fname, 1);
    //sym_dumptextsyms(stdout, 1);
    prog.start_addr = sym_textsyms[0]->addr;
    procs_addr = (md_addr_t *) calloc(max_num_procs, sizeof(md_addr_t));
    prog.p_info = (symbol_i *) calloc(max_num_procs,sizeof(symbol_i));
    for (i=0; i < sym_ntextsyms; i++) {
        sym = sym_textsyms[i];
        if (is_lib_func(sym->name))
            break;
        if (strcmp(sym->name, "main") == 0)
            prog.main_addr = sym->addr;
        if (num_procs >= max_num_procs) {
            max_num_procs *= 2;
            procs_addr = (md_addr_t *) realloc(procs_addr,
                    max_num_procs * sizeof(md_addr_t));
        }
        prog.p_info[num_procs].name = sym->name;
        prog.p_info[num_procs].addr = sym->addr;
        prog.p_info[num_procs].size = sym->size;
        if (dbg) printf("%s %08x %d\n",sym->name,sym->addr,sym->size);

        procs_addr[num_procs++] = sym->addr;
        prog.code_size += sym->size;
    }
    prog.end_addr = prog.start_addr + prog.code_size;

    prog.num_vars=0;
    prog.v_info = (symbol_i *) calloc(max_num_vars, sizeof(symbol_i));
    for (i=0;i<sym_ndatasyms;i++) {
        sym = sym_datasyms[i];
        if (prog.num_vars >= max_num_vars) {
            max_num_vars *= 2;
            prog.v_info = (symbol_i*) realloc(prog.v_info, max_num_vars*sizeof(symbol_i));
        }
        prog.v_info[prog.num_vars].name = sym->name;
        prog.v_info[prog.num_vars].addr = sym->addr;
        if (i<sym_ndatasyms-1) {
            prog.v_info[prog.num_vars].size = sym_datasyms[i+1]->addr-sym->addr;
        }
        else {//dunno
            prog.v_info[prog.num_vars].size = 0;
        }
        if (dbg) printf("%s %08x %d\n",prog.v_info[prog.num_vars].name,prog.v_info[prog.num_vars].addr,prog.v_info[prog.num_vars].size);
        prog.num_vars++;
    }
}



// locate & read the text header of the program
static void
read_text_head(FILE *fp)
{
    long		    pos;
    struct ecoff_filehdr    fhdr;
    struct ecoff_aouthdr    ahdr;
    struct ecoff_scnhdr	    shdr;
    unsigned		    text_size;
    int			    i;
    enum md_opcode	    op;

    md_init_decoder();

    // locate the text section
    fseek(fp, 0, SEEK_SET);
    fread(&fhdr, sizeof(fhdr), 1, fp);
    fread(&ahdr, sizeof(ahdr), 1, fp);
    for (i=0; i<fhdr.f_nscns; i++) {
	fread(&shdr, sizeof(shdr), 1, fp);
	if (shdr.s_flags != ECOFF_STYP_TEXT)
	    continue;
	text_size = shdr.s_size;
	text_offset = shdr.s_scnptr;
	text_entry = shdr.s_vaddr;
	ld_text_size = ((shdr.s_vaddr + shdr.s_size) - MD_TEXT_BASE) 
	    + TEXT_TAIL_PADDING;
    }
    text_offset += prog.start_addr - text_entry;
    ld_text_base = MD_TEXT_BASE;
}



// read an instruction from the object file, decode it and store the decoded instr in
// the ith entry of the decode instruction sequence
static void
read_inst(FILE *fp, addr_t addr, int i)
{
    md_inst_t	    inst;
    int		    op;

    fread(&inst, sizeof(inst), 1, fp);
    prog.code[i].addr = addr;
    prog.code[i].size = sizeof(md_inst_t);
    MD_SET_OPCODE(op, inst);
    prog.code[i].op_enum = MD_OP_ENUM(op);
    decode_inst(&prog.code[i], inst);
}



// read the obj code, decode the inst and store them in prog
void
read_code_ss(char *fname)
{
    FILE	    *fp;
    int		    i;
    addr_t	    addr;

    lookup_addr(fname);

    fp = fopen(fname, "r");
    read_text_head(fp);
    prog.num_inst = prog.code_size / sizeof(md_inst_t);
    prog.code = (de_inst_t *) calloc(prog.num_inst, sizeof(de_inst_t));
    CHECK_MEM(prog.code);
    addr = prog.start_addr;
    // read the text
    fseek(fp, text_offset, SEEK_SET);
    for (i = 0; i < prog.num_inst; i++) {
	read_inst(fp, addr, i);
	addr += sizeof(md_inst_t);
    }
    // the linker may leave an nop instr at the end of the user object text for
    // aligning purpose, e.g, if the text ends at 0x400308, then an nop is inserted
    // at 0x400308, and the library code starts at 0x400310; if this happens, the
    // CFG will be awkard, so we leave it out and adjust the end_addr, prog size etc.
    if (prog.code[prog.num_inst-1].op_enum == NOP) {
	prog.num_inst--;
	prog.code_size -= sizeof(md_inst_t);
	prog.end_addr -= sizeof(md_inst_t);
    }
    
    fclose(fp);
}
