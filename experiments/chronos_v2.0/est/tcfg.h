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
 * $Id: tcfg.h,v 1.2 2006/06/24 08:54:57 lixianfe Exp $
 *
 * Construct a flow graph transformed from the original CFG due to function
 * inlining, loop unrolling ...
 *
 ******************************************************************************/

#ifndef TCFG_H
#define TCFG_H

#include "cfg.h"

// in many cases, two virtual nodes are assumed to exist: one is a "virtual
// start" node preceding the start node of the tcfg; the other is the "virtual
// end" node following end nodes of the tcfg
#define V_START_ID  -1
#define V_END_ID    -2


typedef struct tcfg_edge_t tcfg_edge_t;

// transformed CFG node type (basic block instance)
typedef struct {
    cfg_node_t	*bb;	// pointer to the physical basic block
    int		id;	// global id in tcfg (has nothing to do with its bb id)
    tcfg_edge_t	*in, *out;  // incoming and outgoing edges
    unsigned	flags;
} tcfg_node_t;



// since the numbers of outgoing/incoming edges of each tcfg node vary widely, a
// double link list is used, e.g., given any tcfg edge connecting src->dst, all
// outgoing edges of src can be traversed by going along two directions: going along
// prev_out will traverse all earlier out edges of src, and going along next_out will
// traverse all later out edges of src; similary for the incoming edges of dst
struct tcfg_edge_t {
    int		id;
    int		branch;		// TAKEN or NOT_TAKEN
    tcfg_node_t	*src, *dst;
    tcfg_edge_t *next_out;	// next outgoing edge of src
    tcfg_edge_t *next_in;	// next incoming edge of dst
    int		flags;
};



typedef struct tcfg_nlink_t tcfg_nlink_t;
struct tcfg_nlink_t {
    tcfg_node_t	    *bbi;
    tcfg_nlink_t    *next;
};


typedef struct tcfg_elink_t tcfg_elink_t;

struct tcfg_elink_t {
    tcfg_edge_t	    *edge;
    tcfg_elink_t    *next;
};



void
prog_tran();



void
clear_bbi_flags();



int
bbi_type();



void
dump_tcfg(FILE *fp);



#endif
