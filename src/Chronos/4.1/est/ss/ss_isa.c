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
 * $Id: ss_isa.c,v 1.2 2006/06/24 08:55:05 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common.h"
#include "../isa.h"
#include "ss_isa.h"
#include "machine.h"



extern isa_t	*isa;
extern int	num_isa;



// BEGIN: from machine.h in SimpleScalar 3.0
// -----------------------------------------------------------------------------



#if 0

char *md_op2name[OP_MAX] = {
  NULL, /* NA */
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3) NAME,
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT) NAME,
#define CONNECT(OP)
#include "machine.def"
};



/* enum md_opcode -> opcode flags, used by simulators */
unsigned int md_op2flags[OP_MAX] = {
  NA, /* NA */
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3) FLAGS,
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT) NA,
#define CONNECT(OP)
#include "machine.def"
};

#endif

extern char *md_op2name[];
extern unsigned int md_op2flags[];



// -----------------------------------------------------------------------------
// END: from machine.h in SimpleScalar 3.0


// initiate SimpleScalar ISA info by reading its machine.def file
void
init_isa_ss()
{
    int	    i, len;

    num_isa = OP_MAX - 1;

    isa = (isa_t *) calloc(OP_MAX - 1, sizeof(isa_t));
    CHECK_MEM(isa);
    for (i = 1; i < OP_MAX - 1; i++) {
	isa[i].opcode = i;
	if (md_op2name[i] != NULL) {
	    len = strlen(md_op2name[i]) + 1;
	    isa[i].name = (char *) malloc(len);
	    CHECK_MEM(isa[i].name);
	    strcpy(isa[i].name, md_op2name[i]);
	}
	if (md_op2flags[i] & F_ICOMP)
	    isa[i].type = INST_ICOMP;
	else if (md_op2flags[i] & F_FCOMP)
	    isa[i].type = INST_FCOMP;
	else if (md_op2flags[i] & F_LOAD)
	    isa[i].type = INST_LOAD;
	else if (md_op2flags[i] & F_STORE)
	    isa[i].type = INST_STORE;
	else if (md_op2flags[i] & F_COND)
	    isa[i].type = INST_COND;
	else if (md_op2flags[i] & F_CALL)
	    isa[i].type = INST_CALL;
	else if ((md_op2flags[i] & (F_CTRL|F_UNCOND|F_DIRJMP))
		== (F_CTRL|F_UNCOND|F_DIRJMP))
	    isa[i].type = INST_UNCOND;
	else if ((md_op2flags[i] & (F_CTRL|F_UNCOND|F_INDIRJMP))
		== (F_CTRL|F_UNCOND|F_INDIRJMP))
	    isa[i].type = INST_RET;
	else if (md_op2flags[i] & F_TRAP)
	    isa[i].type = INST_TRAP;
	else {
	    fprintf(stderr, "%s: unidentified instruction type!\n", isa[i].name);
	    exit(1);
	}
    }
}



// decode the raw instr into decoded instr
void
decode_inst(de_inst_t *de_inst, md_inst_t inst)
{
    int	    op, type, offset, i, incr;
    int	    in[3], out[2];
    int	    num_in = 0, num_out = 0;
	 char* inst_format;
	 enum md_opcode mop;

    // get inst opcode and from the opcode get its type info
    MD_SET_OPCODE(mop, inst);
    op = MD_OP_ENUM(mop);
    de_inst->op_enum = op;
    de_inst->size = sizeof(md_inst_t);
    type = isa[op].type;
	 
	 /* Get the instruction format */
	 inst_format = MD_OP_FORMAT(op);
	 
    /* sudiptac ::: Set the immediate field. Needed for 
	  * address analysis */
	 while(*inst_format) {
		  switch(*inst_format)
		  {
			  case 'o':
			  case 'i':
#ifdef _DEBUG
				printf("Immediate value = %d\n", IMM);
#endif
				de_inst->imm = IMM;
				break;
			  case 'H':
#ifdef _DEBUG
				printf("Immediate value = %d\n", SHAMT);
#endif
				de_inst->imm = SHAMT;
				break;
			  case 'u':
#ifdef _DEBUG
				printf("Immediate value = %u\n", UIMM);
#endif
				de_inst->imm = UIMM;
			   break;
			  case 'U':
#ifdef _DEBUG
				printf("Immediate value = %d\n", UIMM);
#endif
				de_inst->imm = UIMM;
				break;
			  default:
				/* Do nothing */	 
			   ;
			   /* sudiptac: Default code may need to be 
				 * modified */
		  }
		  inst_format++;
	  }	  
    
	 // if inst is a ctr transfer, compute the target addr
    if (type == INST_COND) {
		  offset = ((int)((short)(inst.b & 0xffff))) << 2;
		  de_inst->target = de_inst->addr + sizeof(md_inst_t) + offset;
    } else if ((type == INST_UNCOND) || (type == INST_CALL)) {
		  offset = (inst.b & 0x3ffffff) << 2;
		  de_inst->target = (de_inst->addr & 0xf0000000) | offset;
    }

    // decode the input/output info
    switch(op) {
#define DEFINST(OP, MSK, NAME, FMT, FU, CLASS, O1,O2, IN1, IN2, IN3) \
    case OP: \
	     in[0] = IN1; in[1] = IN2; in[2] = IN3; \
		 out[0] = O1; out[1] = O2; \
		 break;
#include "machine.def"
#undef DEFINST
    default:
	     in[0] = in[1] = in[2] = NA;
	     out[0] = out[1] = NA;
    }
    incr = 0;
    for (i = 0; i <= 2; i++) {
		 if (in[i] != NA) {
		   num_in++;
			//incr = 1;
		}	
    }
    incr = 0;
    for (i = 0; i <= 1; i++) {
		 if (out[i] != NA) { 
		   num_out++;
			//incr = 1;
		}	
    }
	 if(!strcmp(isa[op].name, "lb") || !strcmp(isa[op].name, "lh") ||
		  !strcmp(isa[op].name, "lw") || !strcmp(isa[op].name, "lhu") ||
		  !strcmp(isa[op].name, "lwl") || !strcmp(isa[op].name, "lwr") ||
		  !strcmp(isa[op].name, "l.d") || !strcmp(isa[op].name, "l.s"))
	 {
		  if(in[2] != NA)
			 num_in = 2;
		  else	 
			 num_in = 1;
	 }		 
	 if(!strcmp(isa[op].name, "sb") || !strcmp(isa[op].name, "sh") ||
		  !strcmp(isa[op].name, "sw") || !strcmp(isa[op].name, "swl") ||
		  !strcmp(isa[op].name, "swr") || !strcmp(isa[op].name, "s.d") ||
		  !strcmp(isa[op].name, "s.s"))
	 {
		  if(in[2] != NA)
			 num_in = 3;
		  else	 
			 num_in = 2;
	 }		 
    
	 de_inst->in = (int *) calloc(num_in, sizeof(int));
    CHECK_MEM(de_inst->in);
    de_inst->num_in = 0;
	 if(!strcmp(isa[op].name, "lb") || !strcmp(isa[op].name, "lh") ||
		  !strcmp(isa[op].name, "lw") || !strcmp(isa[op].name, "lhu") ||
		  !strcmp(isa[op].name, "lwl") || !strcmp(isa[op].name, "lwr") ||
		  !strcmp(isa[op].name, "l.d") || !strcmp(isa[op].name, "l.s"))
	 {	  
		for (i = 0; i < num_in; i++) 
		  de_inst->in[de_inst->num_in++] = in[i+1];
	 }
	 else if(!strcmp(isa[op].name, "sb") || !strcmp(isa[op].name, "sh") ||
		  !strcmp(isa[op].name, "sw") || !strcmp(isa[op].name, "swl") ||
		  !strcmp(isa[op].name, "swr") || !strcmp(isa[op].name, "s.d") || 
		  !strcmp(isa[op].name, "s.s"))
	 { 	  
		for (i = 0; i < num_in; i++) 
		  de_inst->in[de_inst->num_in++] = in[i];
	 }	  
	 else
	 {
		for (i = 0; i <= 2; i++) {
		  //if (in[i] != NA)
			de_inst->in[de_inst->num_in++] = in[i];
		}
	 }
    de_inst->out = (int *) calloc(num_out, sizeof(int));
    CHECK_MEM(de_inst->out);
    de_inst->num_out = 0;
    for (i = 0; i <= 1; i++) {
		//if (out[i] != NA)
		  de_inst->out[de_inst->num_out++] = out[i];
    }
}

int
ss_inst_fu(de_inst_t *inst)
{
    return MD_OP_FUCLASS(inst->op_enum);
}



extern range_t fu_lat[];
int
ss_max_inst_lat(de_inst_t *inst)
{
    int	    fu;

    fu = ss_inst_fu(inst);
    return fu_lat[fu].hi;
}
