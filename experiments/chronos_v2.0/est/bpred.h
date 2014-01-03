
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
 * $Id: bpred.h,v 1.1.1.1 2006/06/17 00:58:23 lixianfe Exp $
 *
 * This file containts data structures and interfaces for branch prediction
 * analysis.
 *
 ******************************************************************************/

#ifndef BPRED_H
#define BPRED_H

#include "common.h"
#include "tcfg.h"

#define BP_NONE		0
#define BP_CPRED	1
#define BP_MPRED	2
#define BP_UNCLEAR	3

#define ROOT_BBB_ID	77777
#define END_BBB_ID	99999

// branch prediction schemes
enum bpred_scheme_t	{ NO_BPRED, GAG, GSHARE, LOCAL };


typedef struct bfg_edge_t    bfg_edge_t;

// bbb - basic block with branch history
typedef struct {
    tcfg_node_t	*bbi;
    int		id;
    short	bhr;	// BHR: branch history register
    short	pi;	// branch context (bhr manipulated with branch address)
    bfg_edge_t	*out;	// out edges
    bfg_edge_t	*in;	// in edges

    int		flags;
} bfg_node_t;



// an edge connecting two bbbs
struct bfg_edge_t {
    int		branch;
    bfg_node_t	*src, *dst;	// block s -> t
    bfg_edge_t  *prev_out, *next_out, *prev_in, *next_in;
};



typedef struct btg_edge_t   btg_edge_t;

struct btg_edge_t {
    bfg_node_t	*src, *dst;
    int		branch;
    btg_edge_t	*next_in, *next_out;
};



int
bbb_inst_num(bfg_node_t *bbb);

void
bpred_analys();

void
dump_bfg();

#endif
