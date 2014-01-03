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
 * $Id: exegraph.h,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ******************************************************************************/

#ifndef EXE_GRAPH_H
#define EXE_GRAPH_H


#include "common.h"
#include "bpred.h"
#include "cache.h"
#include "pipeline.h"

#define MAX_EG_LEN	1000
#define MAX_EG_EDGES	20000


enum { EG_NORM_EDGE, EG_COND_EDGE };
// types of delays of (x, y): 
// BI_DELAY: statically , x and y can delay one another
// UNI_DELAY: statically, only x can delay y
// NO_DELAY: statically, x and y can be determined not contendable
enum { BI_DELAY, UNI_DELAY, NO_DELAY };
// types of data dependence among instrs (x, y): 
// EG_DEP_NONE: y has no data dependence on x;
// EG_DEP_NORM: y has dependence x in any case; and
// EG_DEP_COND: y has dependence on x conditioinally, e.g. if some statically
// unclear predictions are actually mispredictions
enum { EG_DEP_NONE, EG_DEP_NORM, EG_DEP_COND };



typedef struct egraph_edge_t egraph_edge_t;

typedef struct {
    short	    inst, stage;   
    char	    fu, num_fu;		// applicable to EX nodes
    range16_t	    lat;		// execution latency of EX nodes
    range_t	    rdy, str, fin;	// ready, start, finish
    char	    bp_type;		// BP_CPRED, BP_MPRED, BP_UNCLEAR
    char	    flag;
    egraph_edge_t   *in, *out;
    egraph_edge_t   *e_contd, *l_contd;
} egraph_node_t;


struct egraph_edge_t {
    egraph_node_t   *src, *dst;
    range16_t	    lat;	// max/min latencies from src to dst
    // type can be normal edge (EG_NORM_EDGE) or conditional edge (EG_COND_EDGE)
    char	    normal;
    // for a contention edge, flag denotes three contention possibilities:
    // 1) BI_DELAY: src and dst may delay each other
    // 2) UNI_DELAY: only src may delay dst
    // 3) NO_DELAY: src and dst cannot delay one another
    char	    contd_type;
    egraph_edge_t   *next_in, *next_out;
};


// predecessor and successor instrs of current instr
typedef struct {
    short   pred;
    short   succ;
} eg_chain_t;



#endif
