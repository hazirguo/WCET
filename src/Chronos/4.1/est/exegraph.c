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
 * $Id: exegraph.c,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdlib.h>
#include "common.h"
#include "loops.h"
#include "bpred.h"
#include "cache.h"
#include "pipeline.h"
#include "exegraph.h"


extern int	*num_mp_insts;
extern int	pipe_stages;

mas_inst_t	**eg_insts;
egraph_node_t	**egraph;
int		eg_len = 0, plog_len = 0, elog_len = 0, body_len = 0;
// edge store
egraph_edge_t	*egraph_edges = NULL;
int		num_eg_edges = 0;

// [coexist[i].lo, coexist[i].hi]: earliest and latest instr that can
// coexist with i (i.e., can appear in the instruction window)
range16_t	*coexist;

// for a normal instr, its successor is the next normal instr or -1 if not
// exist; its predecessor is the previous normal instr or -1 if not exist
// for a mispred instr, its successor is the next mispred instr or -1 if not
// exist; its predecessor is the previous mispred instr or -1 if not exist
eg_chain_t	*eg_chain;

int		bpred_type;
loop_t		*body_loop;



void
dump_egraph();



static void
alloc_mem()
{
    int		    i;

    egraph_edges = (egraph_edge_t *) calloc(MAX_EG_EDGES, sizeof(egraph_edge_t));
    eg_insts = (mas_inst_t **) calloc(MAX_EG_LEN, sizeof(mas_inst_t *));
    egraph = (egraph_node_t **) calloc(MAX_EG_LEN, sizeof(egraph_node_t *));
    for (i = 0; i < MAX_EG_LEN; i++)
	egraph[i] = (egraph_node_t *) calloc(pipe_stages, sizeof(egraph_node_t));
    coexist = (range16_t *) calloc(MAX_EG_LEN, sizeof(range16_t));
    eg_chain = (eg_chain_t *) calloc(MAX_EG_LEN, sizeof(eg_chain_t));
}



// dependence edge types:
// 1) (inst, stage1, finish) -> (inst, stage2, ready)
//    inst proceed through the pipeline in-order
// 2) (inst1, stage, finish) -> (inst2, stage, ready)
//    inst and inst2 proceed through a unpipelined stage in-order
// 3) (inst1, stage, start) -> (inst2, stage, ready)
//    inst1 and inst2 proceed through a pipelined stage in-order
// 4) (inst, stage, ready) -> (inst, stage, start)
//    latency of this edge is always zero, the factors that postpone start are
//    contentions from other instructions
// 5) (inst, stage, start) -> (inst, stage, finish)
//    latency of this edge is simply the function unit's execution latency
// 6) (inst1, stage1, finish) -> (inst2, stage2, ready)
//    stage2 of inst2 is data dependent on stage1 of inst1

// contention edge:
// (inst1, stage1, start) and (inst2, stage2, start) contend with each other if
// 1) they use the same functional unit;
// 2) no path from one to the other; and
// 3) their distance makes it impossible for them to coexist in the pipeline
// the contention edge should be annotated with the contended resource;



void
create_egraph(mas_inst_t *plog, int np, mas_inst_t *elog, int ne,
	      mas_inst_t *body, int nb, int bp, loop_t *lp)
{
    int		i, n;
    static	first = 1;

    plog_len = np;
    elog_len = ne;
    body_len = nb;
    eg_len = np + ne + nb;
    bpred_type = bp;
    body_loop = lp;

    if (first) {
	alloc_mem();
	first = 0;
    }

    // collect pointers to each mas_inst
    for (i = 0; i < plog_len; i++)
	eg_insts[i] = &plog[i];
    n = plog_len;
    for (i = 0; i < body_len; i++)
	eg_insts[i+n] = &body[i];
    n += body_len;
    for (i = 0; i < elog_len; i++)
	eg_insts[i+n] = &elog[i];

    create_egraph_ss();

    //dump_egraph();
}



static void
dump_egraph_depends(int inst)
{
    egraph_node_t   *n1, *n2;
    egraph_edge_t   *e;
    int		    stage;

    for (stage = 0; stage < pipe_stages; stage++) {
	printf("  stage[%d]: in:", stage);
	n1 = &egraph[inst][stage];
	for (e = n1->in; e != NULL; e = e->next_in) {
	    if (e->normal == EG_COND_EDGE)
		printf("*");
	    n2 = e->src;
	    printf(" %d.%d(%d)", n2->inst, n2->stage, e->lat.hi);
	}
	printf(";  out:");
	for (e = n1->out; e != NULL; e = e->next_out) {
	    if (e->normal == EG_COND_EDGE)
		printf("*");
	    n2 = e->dst;
	    printf(" %d.%d", n2->inst, n2->stage, e->lat.hi);
	}
	printf("\n");
    }
}



static void
dump_egraph_contends(int inst)
{
    egraph_node_t   *n1, *n2;
    egraph_edge_t   *e;
    int		    stage;

    printf("  E_CONTEND:");
    for (stage = 0; stage < pipe_stages; stage++) {
	n1 = &egraph[inst][stage];
	for (e = n1->e_contd; e != NULL; e = e->next_in) {
	    n2 = e->src;
	    printf(" %d.%d", n2->inst, n2->stage);
	    if (e->normal == EG_COND_EDGE)
		printf("*");
	}
    }
    printf("\n  L_CONTEND:");
    for (stage = 0; stage < pipe_stages; stage++) {
	n1 = &egraph[inst][stage];
	for (e = n1->l_contd; e != NULL; e = e->next_out) {
	    n2 = e->dst;
	    printf(" %d.%d", n2->inst, n2->stage);
	    if (e->normal == EG_COND_EDGE)
		printf("*");
	}
    }
    printf("\n");
}



void
dump_egraph()
{
    int		    inst;

    printf("\nexec graph: [P=%d; B=%d; E=%d]\n", plog_len, body_len, elog_len);
    printf("#edges: %d; avg: %d\n", num_eg_edges, num_eg_edges/eg_len);
    for (inst = 0; inst < eg_len; inst++) {
	if (eg_insts[inst]->bp_flag == BP_MPRED)
	    printf("inst[%d]:%x(m)\n", inst, eg_insts[inst]->inst->addr);
	else if (eg_insts[inst]->bp_flag == BP_UNCLEAR)
	    printf("inst[%d]:%x(u)\n", inst, eg_insts[inst]->inst->addr);
	else
	    printf("inst[%d]:%x   \n", inst, eg_insts[inst]->inst->addr);
	
	dump_egraph_depends(inst);
	dump_egraph_contends(inst);
    }
}
