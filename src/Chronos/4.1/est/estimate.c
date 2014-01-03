/*****************************************************************************
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
 * $Id: estimate.c,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ****************************************************************************/

#include "common.h"
#include "bpred.h"
#include "pipeline.h"
#include "exegraph.h"
#include "ss/machine.h"
#include "ss/ss_machine.h"

#define	LATE	    0
#define	EARLY	    1
#define STEP_SEP    0
#define STEP_EST    1
#define MAX_ITERS   16
#define SHADED	    1


extern int		pipe_stages;
extern egraph_node_t	**egraph;
extern int		eg_len, plog_len, elog_len, body_len;
extern range16_t	*coexist;
extern int		*eg_inst_succ;
extern eg_chain_t	*eg_chain;
extern mas_inst_t	**eg_insts;
extern int		ruu_issue_width;
extern int		ruu_commit_width;
extern int		pipe_iwin_size;
extern int		bpred_type;	// from exegraph.c

// either find_separation (STEP_SEP) or make_estimation (STEP_EST)
int		step = STEP_SEP;
int		changed;
// last_p: last prolog instr; first_b: first body instr; ...
// last_np: last normal prolog instr
int		last_p, last_np, first_b, last_b, first_e;


void
dump_egraph_times(int start, int end);

void
check_fetch_times();



// can only init latest times for prologues
static void
init_shaded_pnode(egraph_edge_t *e)
{
    int		    hi, str, fin;
    egraph_node_t   *src, *dst;

    src = e->src; dst = e->dst;
    str = dst->rdy.hi - e->lat.lo;

    if ((src->flag != SHADED) || (src->str.hi > str)) {
	src->rdy.hi = src->str.hi = str;
	if (src->lat.lo == e->lat.lo)
	    src->fin.hi = dst->rdy.hi;
	else if (src->flag != SHADED)
	    src->fin.hi = str + src->lat.hi;
	else
	    src->fin.hi = min(src->fin.hi, str + src->lat.hi);
	if (src->flag != SHADED) {
	    src->flag = SHADED;
	    src->rdy.lo = src->str.lo = src->fin.lo = -INFTY;
	}
	for (e = src->in; e != NULL; e = e->next_in) {
	    if (e->normal == EG_NORM_EDGE)
		init_shaded_pnode(e);
	}
    }
}



static void
reset_nodes_flags(int first, int last)
{
    int	    inst, stage, event;

    for (inst = first; inst <= last; inst++) {
	for (stage = 0; stage < pipe_stages; stage++)
	    egraph[inst][stage].flag = 0;
    }
}



// init intervals of egraph nodes (prolog/body/epilog) by assuming 0 at the
// first body node: t(first_b, IF, ready) = 0
static void
init_sep()
{
    int		    inst, stage;
    egraph_node_t   *vb, *v;
    egraph_edge_t   *e;

    // assume t(first_b, IF, ready) = 0
    vb = &egraph[first_b][0];
    vb->rdy.lo = vb->rdy.hi = 0;
    vb->str.lo = vb->fin.lo = 0;
    vb->str.hi = vb->fin.hi = INFTY;
    for (e = vb->in; e != NULL; e = e->next_in)
	init_shaded_pnode(e);

    for (inst = 0; inst < eg_len; inst++) {
	for (stage = 0; stage < pipe_stages; stage++) {
	    v = &egraph[inst][stage];
	    if ((v->flag == SHADED) || (v == vb))
		continue;
	    if (v->inst >= first_b) {
		v->rdy.lo = v->str.lo = 0;
		v->fin.lo = v->lat.lo;
	    } else {
		v->rdy.lo = v->str.lo = v->fin.lo = -INFTY;
	    }
	    v->rdy.hi = v->str.hi = v->fin.hi = INFTY;
	}
    }
}



static int
lcontd_delay(egraph_node_t *v)
{
    int		    new_hi, tmp;
    egraph_node_t   *u; 
    egraph_edge_t   *e;

    if (v->l_contd == NULL)
	return v->rdy.hi;

    if (v->inst >= first_e) {
	if (v->lat.hi > 1)
	    return v->rdy.hi + v->lat.hi - 1;
    }
	
    new_hi = v->rdy.hi;
    for (e = v->l_contd; e != NULL; e = e->next_out) {
	if (e->contd_type != BI_DELAY)
	    continue;
	u = e->dst;
	tmp = min(u->fin.hi, v->rdy.hi + u->lat.hi - 1);
	new_hi = max(new_hi, tmp);
    }
    return new_hi;
}



static int
econtd_delay(egraph_node_t *v, int new_hi)
{
    egraph_node_t   *u; 
    egraph_edge_t   *e;
    int		    max_u = new_hi, sum_d = 0;

    if (v->e_contd == NULL)
	return new_hi;

    for (e = v->e_contd; e != NULL; e = e->next_in) {
	if (e->contd_type == NO_DELAY)
	    continue;
	u = e->src;
	if (u->fin.hi <= new_hi)
	    continue;
	sum_d += min(u->fin.hi - new_hi, u->lat.hi);
	max_u = max(max_u, u->fin.hi);
    }
    new_hi = min(max_u, new_hi + sum_d / v->num_fu);
    return new_hi;
}



static int
scalar_delay(egraph_node_t *v, int new_hi)
{
// different policies for considering peers whose latest start times are within
// or beyond PEERS_WIN-1 cycles from new_hi
#define	PEERS_WIN   6
    int		    inst, i, d, rest_peers, mrest, tmp;
    // mpeers are ld/st which consume issue width twice: issue of address
    // calculation followed by issue of actual ld/st
    // FIXME: in current processor model, the second issue takes place *exactly*
    // one cycle after the first issue since addr calc takes one cycle and all
    // dependencies have been resolved by the first issue, thus the second issue
    // will not be delayed further. If this assumption does not hold, then this
    // implementation do not apply.
    int		    peers[PEERS_WIN], mpeers[PEERS_WIN];
    egraph_node_t   *u, *um;

    for (i = 0; i < PEERS_WIN; i++)
	peers[i] = mpeers[i] = 0;
    
    // collect number of peers in each cycle
    inst = v->inst;
    for (i = 1; i < pipe_iwin_size - 1; i++) {
	inst = eg_chain[inst].pred;
	if (inst < 0)
	    break;
	u = &egraph[inst][v->stage];
	d = u->str.hi - new_hi;
	if (d < 0) {
	    if ((u->fu == RdPort) || (u->fu == WrPort)) {
		um = &egraph[inst][u->stage+1];
		d = um->str.hi - new_hi;
		if (d >= 0) {
		    d = min(d, PEERS_WIN-1);
		    peers[d]++;
		}
	    }
	} else {
	    d = min(d, PEERS_WIN-1);
	    if ((u->fu == RdPort) || (u->fu == WrPort))
		mpeers[d]++;
	    else
		peers[d]++;
	}
    }
    for (d = PEERS_WIN-1; d > 0; d--) {
	peers[d-1] += peers[d];
	mpeers[d-1] += mpeers[d];
    }
    rest_peers = peers[0] + mpeers[0];
    mrest = 0;
    for (d = 0; d < PEERS_WIN-1; d++) {
	if ((rest_peers + mrest) < ruu_issue_width)
	    return new_hi + d;
	tmp = rest_peers;
	rest_peers = rest_peers - max(ruu_issue_width - mrest, 0);
	rest_peers = min(rest_peers, peers[d+1] + mpeers[d+1]);
	mrest = min(mpeers[d], tmp) - min(mpeers[d+1], rest_peers);
    }
    new_hi += d + (rest_peers + min(mpeers[d], rest_peers) + mrest) / ruu_issue_width;
    return new_hi;
}



static int
fetch_dep(egraph_edge_t *e)
{
    return min(e->src->str.hi + e->lat.hi, e->src->fin.hi);
}



static int
latest_time(egraph_node_t *v)
{
    int		    new_hi;
    egraph_node_t   *u;
    egraph_edge_t   *e;

    if (v->in == NULL) {
	if ((v->l_contd == NULL) && v->str.hi != v->rdy.hi) {
	    v->str.hi = v->rdy.hi;
	    v->fin.hi = v->str.hi + v->lat.hi;
	    return 1;
	} else
	    return 0;
    }
    new_hi = -INFTY;
    for (e = v->in; e != NULL; e = e->next_in) {
	// FIXME: applicable only if stage 0 is instr fetch
	if (v->stage == 0 && e->src->stage == 0)
	    new_hi = max(new_hi, fetch_dep(e));
	else
	    new_hi = max(new_hi, e->src->str.hi + e->lat.hi);
    }
    u = &egraph[0][pipe_stages-1];
    if ((plog_len > 0) && (v->inst > 0) && coexist[v->inst].lo == 0)
	v->rdy.hi = max(v->rdy.hi, u->rdy.hi - 1);

    v->rdy.hi = new_hi;
    if (v->fu != 0) {
	if (v->lat.hi > 1) {
	    if (v->inst >= first_e)
		new_hi += v->lat.hi - 1;
	    else
		new_hi = lcontd_delay(v);
	}
	new_hi = econtd_delay(v, new_hi);
    }
    // FIXME: should be generalized to stages constrained by issue width, etc.
    if (v->stage == STAGE_EX)
	new_hi = scalar_delay(v, new_hi);
    if (new_hi < v->str.hi) {
	v->str.hi = new_hi;
	v->fin.hi = new_hi + v->lat.hi;
	changed = 1;
	if ((v->e_contd != NULL) || (v->l_contd != NULL))
	    return 1;
    }
    return 0;
}



static int
earliest_time(egraph_node_t *v)
{
    int		    new_lo;
    egraph_edge_t   *e;

    if (v->in == NULL)
	return 0;
    // event time inited from dependences
    new_lo = -INFTY;
    for (e = v->in; e != NULL; e = e->next_in) {
	if (e->normal != EG_NORM_EDGE)
	    continue;
	new_lo = max(new_lo, e->src->str.lo + e->lat.lo);
    }
    if (new_lo > v->rdy.lo) {
	v->rdy.lo = v->str.lo = new_lo;
	v->fin.lo = new_lo + v->lat.lo;
	changed = 1;
	return 1;
    }
    return 0;
}



// two nodes cannot contend if
// earliest(node1, ready) >= latest(node2, finish), or
// earliest(node2, ready) >= latest(node1, finish)
// in addition, a late node2 cannot delay an early node1 if
// earliest(node2, start) >= latest(node1, ready)
static void
update_contd(egraph_node_t *v)
{
    egraph_node_t   *u;
    egraph_edge_t   *e;

    for (e = v->e_contd; e != NULL; e = e->next_in) {
	if (e->contd_type == NO_DELAY)
	    continue;
	u = e->src;
	if ((v->rdy.lo >= u->fin.hi) || (u->rdy.lo >= v->fin.hi))
	    e->contd_type = NO_DELAY;
	else if (v->str.lo >= u->rdy.hi)
	    e->contd_type = UNI_DELAY;
    }
    for (e = v->l_contd; e != NULL; e = e->next_out) {
	if (e->contd_type == NO_DELAY)
	    continue;
	u = e->dst;
	if ((v->rdy.lo >= u->fin.hi) || (u->rdy.lo >= v->fin.hi))
	    e->contd_type = NO_DELAY;
	else if (u->str.lo >= v->rdy.hi)
	    e->contd_type = UNI_DELAY;
    }
}



static void
find_sep()
{
    int		    inst, stage, echanged, lchanged;
    egraph_node_t   *v;

    init_sep();
    do {
	changed = 0;
	for (inst = 0; inst < eg_len; inst++) {
	    for (stage = 0; stage < pipe_stages; stage++) {
		v = &egraph[inst][stage];
		if (inst >= first_b)
		    echanged = earliest_time(v);
		lchanged = latest_time(v);
		if (echanged || lchanged)
		    update_contd(v);
	    }
	}
    } while (changed);
}



static void
plog_backtrack(egraph_edge_t *e)
{
    int		    str, fin, inst, stage;
    egraph_node_t   *src, *dst, *v;
    egraph_edge_t   *e1;

    if (e->normal == EG_COND_EDGE)
	return;

    src = e->src; dst = e->dst;
    src->rdy.lo = src->str.lo = src->fin.lo = -INFTY;
    // first check & update e->src's latest time with e->dst's latest time, if
    // updated, propagate this change by recursive backtracking
    str = dst->rdy.hi - e->lat.lo;

    if (src->str.hi > str) {
	src->rdy.hi = src->str.hi = str;
	if (src->lat.lo == e->lat.lo)
	    src->fin.hi = dst->rdy.hi;
	else
	    src->fin.hi = min(src->fin.hi, str + src->lat.hi);
	// FIXME: execution times of mpred nodes are bounded by branch
	// write-back, this is only applicable to SimpleScalar
	if ((eg_chain[src->inst].succ > src->inst + 1) && src->stage == 3) {
	    for (inst = src->inst+1; inst < eg_chain[src->inst].succ; inst++) {
		for (stage = 0; stage < pipe_stages; stage++) {
		    v = &egraph[inst][stage];
		    v->rdy.hi = v->str.hi = v->fin.hi = src->fin.hi;
		}
	    }
	}
	for (e = src->in; e != NULL; e = e->next_in)
	    plog_backtrack(e);
    }
}



// backtrack to all prolog nodes and get their latest times (cannot know their
// earliest times via backtracking)
static void
init_est_plog()
{
    int		    inst, stage, i;
    egraph_node_t   *v;
    egraph_edge_t   *e;

    // initialize conservative intervals
    for (inst = 0; inst <= last_p; inst++) {
	for (stage = 0; stage < pipe_stages; stage++) {
	    v = &egraph[inst][stage];
	    v->rdy.lo = v->str.lo = -INFTY;
	    v->rdy.hi = v->str.hi = 0;
	}
    }
    v = &egraph[last_np][pipe_stages-1];
    v->fin.hi = v->fin.lo = 0;
    v->rdy.lo = -INFTY;
    v->str.lo = 0 - v->lat.hi;
    v->rdy.hi = v->str.hi = 0 - v->lat.lo;
    for (e = v->in; e != NULL; e = e->next_in)
	plog_backtrack(e);

    if (bpred_type == BP_CPRED)
	return;

    for (inst = last_np + 1; inst <= last_p; inst++) {
	for (stage = 0; stage < pipe_stages; stage++) {
	    v = &egraph[inst][stage];
	    v->rdy.hi = v->str.hi = v->fin.hi = egraph[last_np][3].fin.hi;
	}
    }
}



static void
init_est_body()
{
    int		    inst, stage;
    egraph_node_t   *v;

    // last normal prologue node (there could be mispred instr)
    for (inst = first_b; inst < eg_len; inst++) {
	for (stage = 0; stage < pipe_stages; stage++) {
	    v = &egraph[inst][stage];
	    v->rdy.lo = v->str.lo = 0; 
	    v->rdy.hi = v->str.hi = INFTY;
	}
    }
}



// for each paths x->y, calculate its length by summing up minimum delay of each node
// along the path; the distance is the length of the longest path
static int
distance(egraph_node_t *v, egraph_node_t *u)
{
    int		    len = 0, tmp;
    egraph_edge_t   *e;

    if (v == u)
	return 0;

    for (e = v->out; e != NULL; e = e->next_out) {
	if (e->dst->inst > u->inst)
	    continue;
	tmp = e->lat.lo + distance(e->dst, u);
	if (tmp > len)
	    len = tmp;
    }
    return len;
}



// minimal overlap between x and y, this is for overlap between prolog and body
// e.g., where x is (first_body, IF, ready); y is (last_prolog, CM, finish)
static int
min_overlap(egraph_node_t *v, egraph_node_t *u)
{
    int		    d, mo = INFTY;
    egraph_edge_t   *e;

    for (e = v->in; e != NULL; e = e->next_in) {
	d = distance(e->src, u) - e->lat.hi;
	if (d < mo)
	    mo = d;
    }
    return mo;
}



static void
init_est()
{
    init_est_plog();
    init_est_body();
}



// make estimation based on separation info obtained from find_sep()
static void
make_est()
{
    int		    inst, stage;
    egraph_node_t   *v;

    init_est();
    do {
	changed = 0;
	for (inst = first_b; inst < eg_len; inst++) {
	    for (stage = 0; stage < pipe_stages; stage++) {
		v = &egraph[inst][stage];
		earliest_time(v);
		latest_time(v);
	    }
	}
    } while (changed);
}



int
est_egraph()
{
    int		tm, min_ov;
    egraph_node_t   *v, *u, *w;

    first_b = plog_len;
    last_p = first_b - 1;
    last_np = eg_chain[first_b].pred;
    first_e = plog_len + body_len;
    last_b = first_e - 1;

    find_sep();
    // tm is the estimate from find_sep (where time of first body node was
    // assumed 0), usually it is more conservative than the estimate by the
    // following make_est procedure, thus tm is not the final estimate
    // in most cases
    v = &egraph[last_b][pipe_stages-1];
    tm = v->str.hi + v->lat.hi;

    if (plog_len == 0)
	return tm;

    u = &egraph[first_b][0];
    w = &egraph[last_np][pipe_stages-1];
    min_ov = min_overlap(w, u);
    tm -= min_ov;
    make_est();
    tm = min(tm, v->str.hi + v->lat.hi);
    return tm;
}



void
check_fetch_times()
{
    int	    inst, t1, t2;

    for (inst = 1; inst < eg_len; inst++) {
	t1= egraph[inst-1][0].str.hi + egraph[inst-1][0].lat.hi;
	t2= egraph[inst][0].str.hi + egraph[inst][0].lat.hi;
	if (t1 > t2)
	    printf("IF[%d]: %d %d\n", inst-1, t1, t2);
    }
}



void
dump_egraph_times(int start, int end)
{
    int		    inst, stage;
    egraph_node_t   *v;

    if ((start == 0) && (end == 0)) 
	end = plog_len + body_len + elog_len;
    printf("dump execution graph times [%d %d]: %d, %d, %d\n",
	    start, end, plog_len, body_len, elog_len);
    for (inst = start; inst < end; inst++) {
	if ((inst == first_b) || (inst == first_e))
	    printf("----------------------------------------------------------\n");
	printf("%3d[%x]", inst, eg_insts[inst]->inst->addr & 0xffff);
	if (eg_insts[inst]->bp_flag == BP_CPRED)
	    printf("c");
	else if (eg_insts[inst]->bp_flag == BP_MPRED)
	    printf("m");
	else
	    printf("u");
	if ((eg_insts[inst]->ic_flag == IC_HIT) || (egraph[inst][0].lat.hi == 1))
	    printf("/H");
	else if (eg_insts[inst]->ic_flag == IC_MISS)
	    printf("/M");
	else
	    printf("/U");
	for (stage = 0; stage < pipe_stages; stage++) {
	    v = &egraph[inst][stage];
	    printf(" * %3d %3d %3d", v->rdy.hi, v->str.hi, v->fin.hi);
	}
	printf("\n");
    }
    printf("\n");
}



void
dump_egraph_earliest(int start, int end)
{
    int		    inst, stage, event;
    egraph_node_t   *v;

    if ((start == 0) && (end == 0)) 
	end = plog_len + body_len + elog_len;
    printf("dump execution graph EARLIEST [%d %d]: %d, %d, %d\n", start, end,
	    plog_len, body_len, elog_len);
    for (inst = start; inst < end; inst++) {
	if ((inst == first_b) || (inst == first_e))
	    printf("------------------------------------------------------------------------\n");
	printf("%3d[%x]", inst, eg_insts[inst]->inst->addr & 0xffff);
	if (eg_insts[inst]->bp_flag == BP_CPRED)
	    printf("c");
	else if (eg_insts[inst]->bp_flag == BP_MPRED)
	    printf("m");
	else
	    printf("u");
	if ((eg_insts[inst]->ic_flag == IC_HIT) || (egraph[inst][0].lat.hi == 1))
	    printf("/H");
	else if (eg_insts[inst]->ic_flag == IC_MISS)
	    printf("/M");
	else
	    printf("/U");
	for (stage = 0; stage < pipe_stages; stage++) {
	    v = &egraph[inst][stage];
	    printf(" * %3d %3d %3d", v->rdy.lo, v->str.lo, v->str.lo + v->lat.lo);
	}
	printf("\n");
    }
    printf("\n");
}


static void
dump_econtd(egraph_node_t *v)
{
    egraph_edge_t   *e;

    printf("econtd[%d]\n", v->inst);
    for (e = v->e_contd; e != NULL; e = e->next_in) {
	printf("<-[%d]: %d\n", e->src->inst, e->contd_type);
    }
}
