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
 * $Id: ss_exegraph.c,v 1.2 2006/06/24 08:55:05 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdlib.h>
#include "../tcfg.h"
#include "../loops.h"
#include "../bpred.h"
#include "../cache.h"
#include "../pipeline.h"
#include "../exegraph.h"
#include "machine.h"
#include "ss_machine.h"



extern int		    *num_mp_insts;
extern int		    pipe_stages;
extern enum ss_pfu_class    fu2pfu[]; 


extern tcfg_node_t	**tcfg;
extern mas_inst_t	**eg_insts;
extern egraph_node_t	**egraph;
extern int		eg_len, plog_len, elog_len, body_len;
extern egraph_edge_t	*egraph_edges;
extern int		num_eg_edges;
extern range16_t	*coexist;
extern eg_chain_t	*eg_chain;
extern int		bpred_type;
extern loop_t		*body_loop;

extern cache_t		cache;


// simplescalar simulator options
extern int		LSQ_size;
extern int		RUU_size;
extern int		fetch_width;
extern int		ruu_decode_width;
extern int		ruu_issue_width;
extern int		ruu_commit_width;
extern int		ruu_inorder_issue;
extern int		ruu_ifq_size;
extern int		ruu_branch_penalty;

extern int		enable_icache;


char			**data_dep;
char			*eg_insts_type;
short int		*eg_mem_insts, num_mem_insts;


static
range16_t fu_lat[] = {
    {1, 1},		// FUClass_NA = 0,	
    {1,	1},		// IntALU,		
    {1, 4},		// IntMULT,		
    {1, 4},		// IntDIV,		
    {1, 2},		// FloatADD,		
    {1, 2},		// FloatCMP,		
    {1, 2},		// FloatCVT,		
    {1, 12},		// FloatMULT,		
    {1, 12},		// FloatDIV,		
    {1, 12},		// FloatSQRT,		
    {1, 1},		// RdPort, FIXME: this is an approximate to the internal
			// instr of a lw instr
    {1, 1} 		// WrPort,
};


void
dump_data_dep();



static void
ss_alloc_mem()
{
    eg_insts_type = (char *) calloc(MAX_EG_LEN, sizeof(char));
    eg_mem_insts = (short int*) calloc(MAX_EG_LEN, sizeof(short int));
}



static void
alloc_dep_mem()
{
    int		i;

    data_dep = (char **) calloc(eg_len, sizeof(char *));
    for (i = 1; i < eg_len; i++)
	data_dep[i] = (char *) calloc(i - coexist[i].lo + 1, sizeof(char));
}



static void
dealloc_dep_mem()
{
    int		i;

    for (i = 1; i < eg_len; i++)
	free(data_dep[i]);
    free(data_dep);
}



static void
scan_pred_normal(int curr)
{
    int	    inst;

    for (inst = curr-1; inst >= 0; inst--) {
	if (eg_insts[inst]->bp_flag == BP_CPRED) {
	    eg_chain[curr].pred = inst;
	    eg_chain[inst].succ = curr;
	    if (curr - inst > 1)
		eg_chain[curr-1].succ = -1;
	    return;
	}
    }
    // not found, so no predecessor
    eg_chain[curr].pred = -1;
    eg_chain[curr-1].succ = -1;
}



static void
scan_pred_mpred(int curr)
{
    eg_chain[curr].pred = curr - 1;
    if (eg_insts[curr-1]->bp_flag != BP_CPRED)
	eg_chain[curr-1].succ = curr;
}



static void
scan_pred()
{
    int	    inst;

    eg_chain[0].pred = -1;
    for (inst = 1; inst < eg_len; inst++) {
	if (eg_insts[inst]->bp_flag == BP_CPRED)
	    scan_pred_normal(inst);
	else
	    scan_pred_mpred(inst);
    }
    eg_chain[eg_len-1].succ = -1;
}



static void
inst_coexists(int curr)
{
    int	    inst, rob_num = 1, lsq_num = 0;

    if ((eg_insts_type[curr] == INST_LOAD) || (eg_insts_type[curr] == INST_STORE))
	lsq_num = 1;

    for (inst = eg_chain[curr].succ; inst >= 0; inst = eg_chain[inst].succ) {
	// first check load store queue
	if ((eg_insts_type[curr] == INST_LOAD) || (eg_insts_type[curr] == INST_STORE)) {
	    // load/store queue full, inst and inst0 not coexistable
	    if (++lsq_num > LSQ_size)
		break;
	}
	// then check RUU buffer, exit if it's full
	if (++rob_num >= RUU_size)
	    break;
    }
    if (inst < 0) {
	coexist[curr].hi = eg_len - 1;
	if (curr == 0)
	    coexist[eg_len-1].lo = 0;
    } else {
	coexist[curr].hi = inst;
	coexist[inst].lo = curr;
    }
}



// only search trailing mispred instr
static void
mpinst_coexists(int curr, int br_inst)
{
    int	    i, inst, br_succ;

    br_succ = eg_chain[br_inst].succ; 
    if (br_succ - curr >= RUU_size)
	coexist[curr].hi = curr + RUU_size - 1;
    else
	coexist[curr].hi = br_succ - 1;

    if (curr - br_inst >= RUU_size)
	coexist[curr].lo = curr - RUU_size + 1;
    else if (curr - br_inst == RUU_size - 1)
	coexist[curr].lo = br_inst;
    else {
	coexist[curr].lo = max(0, eg_chain[coexist[curr+1].lo].pred);
    }
}



static void
scan_coexists()
{
    int	    inst, br_inst;

    for (inst = 0; inst >= 0; inst = eg_chain[inst].succ) {
	if (eg_insts[inst]->bp_flag == BP_CPRED) {
	    coexist[inst].lo = 0;
	}
    }

    for (inst = 0; inst >= 0; inst = eg_chain[inst].succ) {
	if (eg_insts[inst]->bp_flag == BP_CPRED) {
	    inst_coexists(inst);
	}
    }
    for (inst = eg_len - 1; inst > 0; inst--) {
	if (eg_insts[inst]->bp_flag != BP_CPRED) {
	    if (eg_insts[inst+1]->bp_flag == BP_CPRED)
		br_inst = eg_chain[inst + 1].pred;
	    mpinst_coexists(inst, br_inst);
	}
    }
}



static void
add_inst(int inst)
{
    int		    stage, fu, lo ,hi, ic_flag, bbi_id, mblk_id;
    egraph_node_t   *node;
    loop_t	    *lp;

    ic_flag = eg_insts[inst]->ic_flag;
    if ((ic_flag != IC_HIT) && (inst < plog_len || inst >= (plog_len + body_len) )) {
	bbi_id = eg_insts[inst]->bbi_id;
	mblk_id = eg_insts[inst]->mblk_id;
	if (enable_icache == 0 || get_mblk_hitmiss(tcfg[bbi_id], mblk_id, body_loop) == IC_HIT)
	    ic_flag = IC_HIT;
    }

    eg_insts_type[inst] = inst_type(eg_insts[inst]->inst);
    if ((eg_insts_type[inst] == INST_LOAD) || (eg_insts_type[inst] == INST_STORE))
	eg_mem_insts[num_mem_insts++] = inst;
    for (stage = 0; stage < pipe_stages; stage++) {
	node = &egraph[inst][stage];
	node->inst = inst;
	node->stage = stage;
	if ((stage == STAGE_EX)) {
	    fu = ss_inst_fu(eg_insts[inst]->inst);
	    node->fu = fu;
	    // FIXME: this is hardcoded for the RTSJ submission, should be
	    // generalized in the future
	    if ((fu == IntALU) || (fu == RdPort) || (fu == WrPort)) {
		node->num_fu = 2;
	    } else
		node->num_fu = 1;
	    node->lat.hi = fu_lat[fu].hi; node->lat.lo = fu_lat[fu].lo;
	} else if (stage == STAGE_IF) {
	    node->fu = node->num_fu = 0;
	    if (ic_flag == IC_HIT) {
		node->lat.lo = node->lat.hi = 1;
	    } else if (ic_flag == IC_MISS) {
		node->lat.lo = node->lat.hi = cache.cmp + 1;
	    } else {
		node->lat.lo = 1; node->lat.hi = cache.cmp + 1;
	    }
	} else {
	    node->fu = node->num_fu = 0;
	    node->lat.hi = node->lat.lo = 1;
	}
	node->bp_type = eg_insts[inst]->bp_flag;
	node->flag = 0;
	node->in = node->out = NULL;
	node->e_contd = node->l_contd = NULL;
    }
}



static void
new_depend(egraph_node_t *src, egraph_node_t *dst, int low, int high, int normal)
{
    egraph_edge_t   *edge;

    edge = &egraph_edges[num_eg_edges];
    num_eg_edges++;

    edge->src = src; edge->dst = dst;
    edge->lat.lo = low; edge->lat.hi = high;
    edge->next_out = src->out; src->out = edge;
    edge->next_in = dst->in; dst->in = edge;
    edge->normal = normal;
    edge->contd_type = 0;
}



static void
stage_order_depends()
{
    int		    inst, stage;
    egraph_node_t   *src, *dst;

    for (inst = 0; inst < eg_len; inst++) {
	for (stage = 0; stage < pipe_stages-1; stage++) {
	    src = &egraph[inst][stage];
	    dst = &egraph[inst][stage+1];
	    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
	}
    }
}



static void
succ_fetch_depends(int inst)
{
    int		    succ, edge_type, lo, hi;
    egraph_node_t   *src, *dst;

    succ = eg_chain[inst].succ;
    if (succ < 0)
	return;

    src = &egraph[inst][STAGE_IF];

    if ((fetch_width == 1) || (eg_insts[inst]->ic_flag == IC_MISS)) {
	lo = src->lat.lo; hi = src->lat.hi;
    } else if (eg_insts[inst]->ic_flag == IC_UNCLEAR) {
	lo = 0; hi = src->lat.hi;
    } else {
	lo = hi = 0;
    }

    // first consider normal successor
    dst = &egraph[succ][STAGE_IF];
    new_depend(src, dst, lo, hi, EG_NORM_EDGE);
    if (succ == (inst+1))
	return;
    // then consider mpred successor
    dst = &egraph[inst+1][STAGE_IF];
    new_depend(src, dst, lo, hi, EG_NORM_EDGE);

}



static void
succ_decode_depends(int inst)
{
    int		    succ, edge_type, lo, hi;
    egraph_node_t   *src, *dst;

    succ = eg_chain[inst].succ;
    if (succ < 0)
	return;

    src = &egraph[inst][STAGE_ID];
    // first consider normal successor
    dst = &egraph[succ][STAGE_ID];
    if (ruu_decode_width == 1)
	lo = hi = 1;
    else
	lo = hi = 0;
    new_depend(src, dst, lo, hi, EG_NORM_EDGE);
    if (succ == (inst+1))
	return;
    // then consider mpred successor
    dst = &egraph[inst+1][STAGE_ID];
    new_depend(src, dst, lo, hi, EG_NORM_EDGE);
}



// for inorder execution
static void
succ_issue_depends(int inst)
{
    int		    succ, edge_type, fu;
    egraph_node_t   *src, *dst;

    succ = eg_chain[inst].succ;
    if (succ < 0)
	return;

    fu = ss_inst_fu(eg_insts[inst]->inst);
    if ((fu == RdPort) || (fu == WrPort))
	src = &egraph[inst][STAGE_WB];
    else
	src = &egraph[inst][STAGE_EX];
    // first consider normal successor
    dst = &egraph[succ][STAGE_ID];
    new_depend(src, dst, 0, 0, EG_NORM_EDGE);
    if (succ == (inst+1))
	return;
    // then consider mpred successor
    dst = &egraph[inst+1][STAGE_ID];
    new_depend(src, dst, 0, 0, EG_NORM_EDGE);
}



static void
succ_commit_depends(int inst)
{
    int		    succ, lo, hi;
    egraph_node_t   *src, *dst;

    succ = eg_chain[inst].succ;
    if (succ < 0)
	return;

    if (ruu_commit_width == 1)
	lo = hi = 1;
    else
	lo = hi = 0;
    src = &egraph[inst][STAGE_CM];
    dst = &egraph[succ][STAGE_CM];
    new_depend(src, dst, lo, hi, EG_NORM_EDGE);
}



static void
fetch_width_depends_n(int inst, int succ)
{
    int		    i, succ1, tagset0, tagset;
    egraph_node_t   *src, *dst;

    // first consider normal successor
    src = &egraph[inst][STAGE_IF];
    tagset0 = TAGSET(eg_insts[inst]->inst->addr);
    // if inst and succ are not in a memory block, an edge should be created
    // from inst to the first instr next to inst's memory block instead of succ
    succ1 = inst;
    for (i = 0; i < fetch_width; i++) {
	succ1 = eg_chain[succ1].succ;
	tagset = TAGSET(eg_insts[succ1]->inst->addr);
	if (tagset0 != tagset)
	    break;
    }
    dst = &egraph[succ1][STAGE_IF];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



static void
fetch_width_depends_m(int inst)
{
    int		succ = inst + fetch_width, tagset0, tagset;
    egraph_node_t   *src, *dst;

    if (eg_insts[succ]->bp_flag == BP_CPRED)
	return;

    src = &egraph[inst][STAGE_IF];
    tagset0 = TAGSET(eg_insts[inst]->inst->addr);
    for (succ = inst + 1; succ < inst + fetch_width; succ++) {
	tagset = TAGSET(eg_insts[succ]->inst->addr);
	if (tagset0 != tagset)
	    break;
    }
    dst = &egraph[succ][STAGE_IF];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



static void
fetch_width_depends(int inst, int succ)
{
    fetch_width_depends_n(inst, succ);
    if ((eg_insts[inst]->bp_flag != BP_CPRED) || (succ == (inst + fetch_width)))
	return;

    // then consider mpred successor
    fetch_width_depends_m(inst);
}



static void
decode_width_depends_n(int inst, int succ)
{
    egraph_node_t   *src, *dst;

    src = &egraph[inst][STAGE_ID];
    dst = &egraph[succ][STAGE_ID];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



static void
decode_width_depends_m(int inst)
{
    int		    succ;
    egraph_node_t   *src, *dst;

    succ = inst + ruu_decode_width;
    if (eg_insts[succ]->bp_flag == BP_CPRED)
	return;
    src = &egraph[inst][STAGE_ID];
    dst = &egraph[succ][STAGE_ID];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



static void
decode_width_depends(int inst, int succ)
{
    // first consider normal successor
    decode_width_depends_n(inst, succ);
    if ((eg_insts[inst]->bp_flag != BP_CPRED) || (succ == (inst + ruu_decode_width)))
	return;

    // then consider mpred successor
    decode_width_depends_m(inst);
}



static void
issue_width_depends_n(int inst, int succ)
{
    egraph_node_t   *src, *dst;

    src = &egraph[inst][STAGE_EX];
    dst = &egraph[succ][STAGE_EX];
    new_depend(src, dst, 1, 1, EG_NORM_EDGE);
}



static void
issue_width_depends_m(int inst)
{
    int		    succ;
    egraph_node_t   *src, *dst;

    succ = inst + ruu_decode_width;
    if (eg_insts[succ]->bp_flag == BP_CPRED)
	return;
    src = &egraph[inst][STAGE_EX];
    dst = &egraph[succ][STAGE_EX];
    new_depend(src, dst, 1, 1, EG_NORM_EDGE);
}



static void
issue_width_depends(int inst, int succ)
{
    // first consider normal successor
    issue_width_depends_n(inst, succ);
    if ((eg_insts[inst]->bp_flag != BP_CPRED) || (succ == (inst + ruu_issue_width)))
	return;

    // then consider mpred successor
    issue_width_depends_m(inst);
}



static void
commit_width_depends(int inst, int succ)
{
    egraph_node_t   *src, *dst;

    src = &egraph[inst][STAGE_CM];
    dst = &egraph[succ][STAGE_CM];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



static void
inorder_depends()
{
    int		    i, inst, succ, msucc;

    for (inst = 0; inst < eg_len - 1; inst++) {
	// 1) in-order constraints
	succ_fetch_depends(inst);
	succ_decode_depends(inst);
	succ_commit_depends(inst);
	if (ruu_inorder_issue == TRUE)
	    succ_issue_depends(inst);
    }
    if (fetch_width == 1)
	return;

    succ = 0;
    for (i = 0; i < fetch_width; i++)
	succ = eg_chain[succ].succ;

    for (inst = 0; inst < eg_len - fetch_width; inst++) {
	if (succ < 0)
	    break;
	// 2) in-order + superscalarity limit constraints
	if (eg_insts[inst]->bp_flag == BP_CPRED) {
	    fetch_width_depends(inst, succ);
	    decode_width_depends(inst, succ);
	    commit_width_depends(inst, succ);
	    succ = eg_chain[succ].succ;
	} else {
	    msucc = eg_chain[inst-1].succ - fetch_width;
	    while (inst < msucc) {
		fetch_width_depends_m(inst);
		decode_width_depends_m(inst);
		inst++;
	    }
	    inst = msucc + fetch_width - 1;
	}
    }
    if (inst < eg_len - fetch_width) {
	for (; inst < eg_len - fetch_width; inst++) {
	    fetch_width_depends_m(inst);
	    decode_width_depends_m(inst);
	}
    }
}



//  inst is a normal instr, build edges from inst's ID to another inst's IF
//  which gets the free i-buf entry from inst
static void
ibuf_normal_depends(int inst, int succ)
{
    egraph_node_t   *src, *dst;

    // first consider normal successor
    src = &egraph[inst][STAGE_ID];
    dst = &egraph[succ][STAGE_IF];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
    if ((src->bp_type != BP_CPRED) || (succ == (inst+ruu_ifq_size)))
	return;

    // then consider mispred instr
    succ = inst + ruu_ifq_size;
    if (succ >= eg_chain[inst].succ)
	return;
    dst = &egraph[succ][STAGE_IF];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



static void
ibuf_mpred_depends(int inst, int succ)
{
    egraph_node_t   *src, *dst;

    src = &egraph[inst][STAGE_ID];
    dst = &egraph[succ][STAGE_IF];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



// decode of inst frees an i-buffer entry, thus make a room for a subsequent
// instr to be fetched, the distance of the subsequent instr is determined by
// i-buffer size
static void
fifo_ibuf_depends()
{
    int		    i, inst, succ, msucc;

    succ = 0;
    for (i = 0; i < ruu_ifq_size; i++)
	succ = eg_chain[succ].succ;

    for (inst = 0; inst < eg_len - ruu_ifq_size; inst++) {
	if (succ < 0)
	    break;
	if (eg_insts[inst]->bp_flag == BP_CPRED) {
	    ibuf_normal_depends(inst, succ);
	    succ = eg_chain[succ].succ;
	} else {
	    msucc = eg_chain[inst-1].succ - ruu_ifq_size;
	    while (inst < msucc) {
		ibuf_mpred_depends(inst, inst + ruu_ifq_size);
		inst++;
	    }
	    inst = msucc + ruu_ifq_size - 1;
	}
    }
    if (inst < eg_len - ruu_ifq_size) {
	for (; inst < eg_len - ruu_ifq_size; inst++) {
	    succ = inst + ruu_ifq_size;
	    if (eg_insts[succ]->bp_flag == BP_CPRED) 
		continue;
	    if (eg_insts[inst]->bp_flag == BP_CPRED)
		ibuf_normal_depends(inst, succ);
	    else
		ibuf_mpred_depends(inst, succ);
	}
    }
}



static void
rob_depends(int inst, int succ)
{
    egraph_node_t   *src, *dst;

    src = &egraph[inst][STAGE_CM];
    dst = &egraph[succ][STAGE_ID];
    new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
}



// commit of inst frees a ROB entry, thus make a room for a subsequent instr to
// be decoded and dispatched into ROB, the distance of the subsequent instr is
// determined by i-buffer size
static void
fifo_rob_depends()
{
    int		    i, inst, succ;

    if (eg_len <= RUU_size)
	return;
    succ = 0;
    for (i = 0; i < RUU_size; i++) {
	succ = eg_chain[succ].succ;
	if (succ < 0)
	    break;
    }
    for (inst = 0; (inst >= 0) && (succ >= 0); inst = eg_chain[inst].succ) {
	rob_depends(inst, succ);
	succ = eg_chain[succ].succ;
    }
}



// mark dst's dependence on src, and propogate this dependence back along src
static void
mark_data_dep(int src_id, int dst_id)
{
    int		inst, lbound;

    if (src_id < coexist[dst_id].lo)
	return;
    // mark dst's dependence on src
    data_dep[dst_id][dst_id - src_id] = 1;
    // propagate depedence onto src's dependences
    lbound = max(0, coexist[dst_id].lo);
    for (inst = eg_chain[src_id].pred; inst >= coexist[dst_id].lo; inst = eg_chain[inst].pred) {
	if (data_dep[src_id][src_id - inst])
	    data_dep[dst_id][dst_id - inst] = 1;
    }
}



static void
create_bpred_depend(int src_id, int dst_id, int normal)
{
    int		    lo, hi;
    egraph_node_t   *src, *dst;
    
    src = &egraph[src_id][STAGE_WB];
    dst = &egraph[dst_id][STAGE_IF];
    lo = ruu_branch_penalty + src->lat.lo;
    hi = ruu_branch_penalty + src->lat.hi;
    new_depend(src, dst, lo, hi, normal);
}



static void
create_data_depend(int src_id, int dst_id, int normal)
{
    egraph_node_t   *src, *dst;

    if ((eg_insts_type[src_id] == INST_ICOMP) || (eg_insts_type[src_id] == INST_FCOMP))
	src = &egraph[src_id][STAGE_EX];
    else if (eg_insts_type[src_id] == INST_LOAD)
	src = &egraph[src_id][STAGE_WB];
    else
	return;
    dst = &egraph[dst_id][STAGE_EX];
    new_depend(src, dst, src->lat.lo, src->lat.hi, normal);
    mark_data_dep(src_id, dst_id);
}



static void
data_depends()
{
    int		i, j, r, br_inst, bp_flag, src_id;
    int		regs[MD_TOTAL_REGS], mp_regs[MD_TOTAL_REGS];
    de_inst_t	*inst;

    for (i = 0; i < MD_TOTAL_REGS; i++) {
	regs[i] = mp_regs[i] = -1;
    }

    bp_flag = eg_insts[0]->bp_flag;
    inst = eg_insts[0]->inst;
    // update register producer
    for (j = 0; j < inst->num_out; j++) {
	r = inst->out[j];
	regs[r] = 0;
    }
    if (eg_insts_type[0] == INST_COND)
	create_bpred_depend(0, eg_chain[0].succ, EG_COND_EDGE);

    for (i = 1; i < eg_len; i++) {
	inst = eg_insts[i]->inst;
	bp_flag = eg_insts[i]->bp_flag;
	// get source producers and create data dependencies
	for (j = 0; j < inst->num_in; j++) {
	    r = inst->in[j];
	    if ((bp_flag != BP_CPRED) && (mp_regs[r] > br_inst))
		src_id = mp_regs[r];
	    else
		src_id = regs[r];

	    if (src_id >= coexist[i].lo)
		create_data_depend(src_id, i, EG_NORM_EDGE);
	}
	// update register producer
	for (j = 0; j < inst->num_out; j++) {
	    r = inst->out[j];
	    if (bp_flag == BP_CPRED)
		regs[r] = i;
	    else
		mp_regs[r] = i;
	}
	if (eg_insts_type[i] != INST_COND)
	    continue;
	if (eg_chain[i].succ < 0)
	    continue;

	if (eg_chain[i].succ == plog_len) {
	    if (bpred_type == BP_MPRED) {
		create_bpred_depend(i, eg_chain[i].succ, EG_NORM_EDGE);
		mark_data_dep(i, eg_chain[i].succ);
	    }
	} else {
	    create_bpred_depend(i, eg_chain[i].succ, EG_COND_EDGE);
	}
    }
}



static void
load_wait_store()
{
    int		    i, j, type;
    de_inst_t	    *inst;
    egraph_node_t   *src, *dst;

    for (i = 1; i < num_mem_insts; i++) {
	if (eg_insts_type[eg_mem_insts[i]] == INST_STORE)
	    continue;
	dst = &egraph[eg_mem_insts[i]][STAGE_WB];
	for (j = i - 1; j >= 0; j--) {
	    if (eg_mem_insts[j] < coexist[i].lo)
		break;
	    if (eg_insts_type[eg_mem_insts[j]] == INST_STORE) {
		src = &egraph[eg_mem_insts[j]][STAGE_EX];
		new_depend(src, dst, src->lat.lo, src->lat.hi, EG_NORM_EDGE);
	    }
	}
    }
}



static void
build_depends()
{
    stage_order_depends();
    // in-order dependencies in some stages
    inorder_depends();
    // FIFO producer/consumer dependencies: FIFOs include fetch buffer and ROB
    fifo_ibuf_depends();
    fifo_rob_depends();
    // data dependences
    data_depends();
    // no support for load bypass store, so load always wait for completion of
    // address computation of early stores
    load_wait_store();
}



static int
ready_earlier(int inst1, int inst2)
{
    egraph_node_t   *v1, *v2;
    egraph_edge_t   *e1, *e2;
    int		    earlier = 1, num_deps = 0;

    if (inst1 > inst2)
	return 0;
    else if (inst1 == inst2)
	return 1;
    else if (coexist[inst2].lo >= inst1)
	return 1;
    else if (data_dep[inst2][inst2 - inst1])
	return 1;

    v1 = &egraph[inst1][STAGE_EX];
    v2 = &egraph[inst2][STAGE_EX];
    if (fu2pfu[v1->fu] != fu2pfu[v2->fu]) {
	for (e2 = v2->in; e2 != NULL; e2 = e2->next_in) {
	    if ((e2->src->stage != STAGE_EX) && (e2->src->stage != STAGE_WB))
		continue;
	    earlier = ready_earlier(inst1, e2->src->inst);
	    if (earlier)
		break;
	}
	return earlier;
    }

    for (e1 = v1->in; e1 != NULL; e1 = e1->next_in) {
	if ((e1->src->stage != STAGE_EX) && (e1->src->stage != STAGE_WB))
	    continue;
	num_deps++;
	for (e2 = v2->in; e2 != NULL; e2 = e2->next_in) {
	    if ((e2->src->stage != STAGE_EX) && (e2->src->stage != STAGE_WB))
		continue;
	    earlier = ready_earlier(e1->src->inst, e2->src->inst);
	    if (earlier)
		break;
	}
	if (!earlier)
	    break;
    }
    if ((coexist[inst2].lo == 0) && (num_deps < 2))
	return 0;
    else
	return earlier;
}



static int
delay_type(egraph_edge_t *ce)
{
    int		    earlier;

    earlier = ready_earlier(ce->src->inst, ce->dst->inst);
    if (earlier)
	return UNI_DELAY;
    else
	return BI_DELAY;
}



static void
new_contend(egraph_node_t *src, egraph_node_t *dst, int normal)
{
    egraph_edge_t   *edge;

    edge = &egraph_edges[num_eg_edges];
    num_eg_edges++;

    edge->src = src;
    edge->dst = dst;
    edge->next_out = src->l_contd;
    src->l_contd = edge;
    edge->next_in = dst->e_contd;
    dst->e_contd = edge;

    // override edge_type parameter to conditional contention if dst is in
    // unclear section while src is a normal instr since the contention may not
    // arise if dst does not exist (e.g., the prediction is correct)
    if ((eg_insts[dst->inst]->bp_flag == BP_UNCLEAR) && (eg_insts[src->inst]->bp_flag == BP_CPRED))
	normal = EG_COND_EDGE;
    edge->normal = normal;

    // a late node with exe latency <= 1 cannot delay an earlier node
    if (edge->dst->lat.hi <= 1)
	edge->contd_type = UNI_DELAY;
    else
	edge->contd_type = delay_type(edge);
}



static void
inst_contends(int curr)
{
    int		    inst, normal;
    egraph_node_t   *src, *dst;

    dst = &egraph[curr][STAGE_EX];
    for (inst = eg_chain[curr].pred; inst >= 0; inst = eg_chain[inst].pred) {
	if (coexist[curr].lo > inst)
	    break;
	if (data_dep[curr][curr-inst] == EG_DEP_NORM)
	    continue;
	else if (data_dep[curr][curr-inst] == EG_DEP_COND)
	    normal = EG_COND_EDGE;
	else
	    normal = EG_NORM_EDGE;
	src = &egraph[inst][STAGE_EX];
	if (fu2pfu[src->fu] == fu2pfu[dst->fu])
	    new_contend(src, dst, normal);
	else if (((src->fu == IntALU) || (src->fu == RdPort) || (src->fu == WrPort))
		&& (dst->fu == IntALU || (dst->fu == RdPort) || (dst->fu == WrPort))) {
	    // because load/store use IntALU for address calculation
	    new_contend(src, dst, normal);
	}
    }
}



static void
build_contends()
{
    int		inst;

    for (inst = 1; inst < eg_len; inst++)
	inst_contends(inst);
}



void
create_egraph_ss()
{
    int		i;
    static int	first = 1;

    if (first) {
	ss_alloc_mem();
	first = 0;
    }
    num_eg_edges = 0;
    num_mem_insts = 0;
    scan_pred();
    scan_coexists();
    for (i = 0; i < eg_len; i++)
	add_inst(i);
    alloc_dep_mem();
    build_depends();
    build_contends();
    dealloc_dep_mem();
}



void
dump_coexist(int first, int last)
{
    int		i;

    for (i = first; i < last; i++)
	printf("coexist[%d]/%d: [%d, %d]\n", i, eg_insts[i]->bp_flag, coexist[i].lo, coexist[i].hi);
}



void
dump_data_dep()
{
    int		i, j;

    printf("data dependences\n");
    for (i = 1; i < eg_len; i++) {
	printf("%d(%x): ", i, eg_insts[i]->inst->addr & 0xffff);
	int	    j;
	for (j = coexist[i].lo; j < i; j++)
	    if (data_dep[i][i-j] != 0)
		printf("%d(%x) ", j, eg_insts[j]->inst->addr & 0xffff);
	printf("\n");
    }
}
