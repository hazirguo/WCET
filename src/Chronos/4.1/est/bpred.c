
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
 * $Id: bpred.c,v 1.1.1.1 2006/06/17 00:58:23 lixianfe Exp $
 *
 * This file contains functions performing branch prediction analysis.
 *
 ******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "bpred.h"



extern int	    num_tcfg_nodes;
extern int	    num_tcfg_edges;
extern tcfg_node_t  **tcfg;
extern tcfg_edge_t  **tcfg_edges;
extern int	    bpred_scheme;
extern int	    pipe_ibuf_size, pipe_iwin_size;

int		    *pi_table;
bfg_node_t	    root_bbb, end_bbb;
int		    root_bbb_id, end_bbb_id;
bfg_node_t	    ***bfg;
int		    num_bfg_nodes;
int		    BHR, BHR_PWR, BHR_MSK;
int		    BHT_SIZE, BHT, BHT_MSK;
bfg_node_t	    **vbbb;


// TASK1: collecting mispred instructions for each branch
// =============================================================================

// mp_insts[i]: mispred instructions on the alternative path of edge[i];
// NULL for non-conditional edges
de_inst_t	***mp_insts;
// number of mispred instructions in mp_insts[i]
int		*num_mp_insts;


// collect mispred instructions along the alternative path of each conditional
// edge, this process terminates under either of the following conditions:
// - the number of collected instructions >= instruciton window
// - hit another branch or program end
// ---------------------------------------------------------------------------

void
collect_mp_insts()
{
    int		num = 0, i = 0, j, limit;
    de_inst_t	**tmp_insts;
    tcfg_node_t	*bbi;
    tcfg_edge_t	*e;

    limit = pipe_ibuf_size + pipe_iwin_size - 1;
    mp_insts = (de_inst_t ***) calloc(num_tcfg_edges, sizeof(de_inst_t**));
    CHECK_MEM(mp_insts);
    num_mp_insts = (int *) calloc(num_tcfg_edges, sizeof(int));
    CHECK_MEM(num_mp_insts);

    tmp_insts = (de_inst_t **) calloc(limit, sizeof(de_inst_t*));
    CHECK_MEM(tmp_insts);
    for (i = 0; i < num_tcfg_edges; i++) {
	e = tcfg_edges[i];
	if (!cond_bbi(e->src))
	    // non-conditional blocks has no mispred execution
	    continue;
	if (e == e->src->out)
	    bbi = e->next_out->dst;
	else
	    bbi = e->src->out->dst;
	num = 0;
	while (num < limit) {
	    for (j = 0; (j < bbi->bb->num_inst) && (num < limit); j++)
		tmp_insts[num++] = &bbi->bb->code[j];
	    if ((bbi->out == NULL) || (cond_bbi(bbi)))
		break;
	    bbi = bbi->out->dst;
	}
	mp_insts[i] = (de_inst_t **) calloc(num, sizeof(de_inst_t*));
	CHECK_MEM(mp_insts[i]);
	memmove(mp_insts[i], tmp_insts, num * sizeof(de_inst_t*));
	num_mp_insts[i] = num;
    }

    free(tmp_insts);
}









// TASK2: building bfg
// =============================================================================

static void
init_root_bbb()
{
    root_bbb.bbi = NULL;
    root_bbb.bhr = 0;
    root_bbb.pi = 0;
    root_bbb.out = NULL;    // out edges
    root_bbb.in = NULL;	    // in edges
    root_bbb.flags = 0;
}



static void
init_end_bbb()
{
    end_bbb.bbi = NULL;
    end_bbb.pi = BHT_SIZE + 1;	// very important
    end_bbb.out = NULL;		// out edges
    end_bbb.in = NULL;		// in edges
    end_bbb.flags = 0;
}



// update branch history register (BHR) with a branch outcome
static unsigned
bhr_update(unsigned bhr, int branch)
{
    if (branch == TAKEN)
	return ((BHR_MSK & ((bhr) << 1)) | 1);
    else if (branch == NOT_TAKEN)
	return ((BHR_MSK & ((bhr) << 1)) | 0);
    else
	return bhr;
}



// for each tcfg node, init its instances under context of branch history
// there are at most total_bbi * (2^BHR) instances for all tcfg nodes
static void
init_bfg()
{
    int	    i;

    init_root_bbb();
    init_end_bbb();

    bfg = (bfg_node_t ***) malloc(num_tcfg_nodes * sizeof(bfg_node_t **));
    CHECK_MEM(bfg);
    for (i = 0; i < num_tcfg_nodes; i++) {
	bfg[i] = (bfg_node_t **) calloc(BHT_SIZE, sizeof(bfg_node_t *));
	CHECK_MEM(bfg[i]);
    }

    pi_table = (int *) calloc(BHT_SIZE, sizeof(int));
}



static unsigned
bhr_to_pi(tcfg_node_t *bbi, unsigned bhr)
{
    cfg_node_t	*bb = bbi->bb;
    addr_t	addr;

    if (bpred_scheme == GSHARE) {	// gshare
	addr = bb->code[bb->num_inst-1].addr;
	return (((bhr << (BHT - BHR)) ^ (addr >> 3)) & BHT_MSK);
    } else if (bpred_scheme == GAG) {	// gag
	return bhr;
    } else {	// local
	addr = bb->code[bb->num_inst-1].addr;
	return ((addr >> 3) & BHT_MSK);
    }
}



// create a node instance with a branch history (BHR)
static bfg_node_t *
new_bbb(int bbi_id, int bhr)
{
    bfg_node_t	*x;

    x = (bfg_node_t *) calloc(1, sizeof(bfg_node_t));
    CHECK_MEM(x);
    x->bbi = tcfg[bbi_id];
    x->bhr = bhr;
    x->pi = bhr_to_pi(x->bbi, bhr);

    pi_table[x->pi] = 1;

    return x;
}



// find the first conditional branch starting from the target of e, return NULL if
// not found
static tcfg_node_t *
next_cond_bbi(tcfg_edge_t *e)
{
    while (!cond_bbi(e->dst)) {
	e = e->dst->out;
	if (e == NULL)
	    return NULL;
    }
    return e->dst;
}



static bfg_edge_t *
new_bfg_edge(bfg_node_t *x, bfg_node_t *y, int branch)
{
    bfg_edge_t	*p;

    p = (bfg_edge_t *) calloc(1, sizeof(bfg_edge_t));
    CHECK_MEM(p);
    p->src = x; p->dst = y;
    p->branch = branch;

    p->prev_out = NULL; p->next_out = x->out;
    if (x->out != NULL)
	x->out->prev_out = p;
    x->out = p;

    p->prev_in = NULL; p->next_in = y->in;
    if (y->in != NULL)
	y->in->prev_in = p;
    y->in = p;
}



// traverse the tcfg to build (bbi, BHR) nodes
static void
trav_tcfg()
{
    int		bhr, pi;
    tcfg_node_t	*bbi;
    Queue	worklist;
    bfg_node_t	*x;
    tcfg_edge_t	*e;
    int		branch;

    bbi = tcfg[0];

    num_bfg_nodes = 0;
    init_queue(&worklist, sizeof(bfg_node_t *));
    bbi = tcfg[0];
    // find the first conditional branch
    while (!cond_bbi(bbi) && (bbi->out != NULL))
	    bbi = bbi->out->dst;
    if(bbi->out == NULL)
        return;
    
    bfg[bbi->id][0] = new_bbb(bbi->id, 0);
    bfg[bbi->id][0]->id = num_bfg_nodes++;
    new_bfg_edge(&root_bbb, bfg[bbi->id][0], NOT_TAKEN);
    enqueue(&worklist, &bfg[bbi->id][0]);

    while (!queue_empty(&worklist)) {
	x = *((bfg_node_t **) dequeue(&worklist));
	for (e = x->bbi->out; e != NULL; e = e->next_out) {
	    branch = e->branch;
	    bbi = next_cond_bbi(e);
	    if (bbi == NULL) {
		new_bfg_edge(x, &end_bbb, branch);
		continue;
	    }

	    if (branch == TAKEN)
		bhr = bhr_update(x->bhr, 1);
	    else if (branch == NOT_TAKEN)
		bhr = bhr_update(x->bhr, 0);
	    else
		bhr = x->bhr;
	    pi = bhr_to_pi(bbi, bhr);
	    if (bfg[bbi->id][pi] == NULL) {
		bfg[bbi->id][pi] = new_bbb(bbi->id, bhr);
		bfg[bbi->id][pi]->id = num_bfg_nodes++;
		enqueue(&worklist, &bfg[bbi->id][pi]);
	    }
	    new_bfg_edge(x, bfg[bbi->id][pi], branch);
	}
    }

    if (num_bfg_nodes < 77777) {
	root_bbb_id = 77777;
	end_bbb_id = 88888;
    } else {
	fprintf(stderr, "Program complexity is over analysis limit!\n");
	exit(1);
    }
    root_bbb.id = root_bbb_id;
    end_bbb.id = end_bbb_id;
}



// collect pointers of bfg nodes
static void
collect_bfg_nodes()
{
    int		i, j, id;
    tcfg_node_t *p;

    vbbb = (bfg_node_t **) malloc(num_bfg_nodes * sizeof(bfg_node_t *));
    CHECK_MEM(vbbb);
    for (i = 0; i < num_tcfg_nodes; i++) {
	for (j = 0; j < BHT_SIZE; j++) {
	    if (bfg[i][j] != NULL) {
		vbbb[bfg[i][j]->id] = bfg[i][j];
	    }
	}
    }
}



void
dump_bfg()
{
    int		i, j;
    bfg_edge_t	*e;
    int		bnum = 0;

    printf("\ndump bfg\n");
    for (i = 0; i < num_tcfg_nodes; i++) {
	for (j = 0; j < BHT_SIZE; j++) {
	    if (bfg[i][j] == NULL)
		continue;
	    printf("%d(%d_%x) [ ", bfg[i][j]->id, bfg[i][j]->bbi->id, bfg[i][j]->pi);
	    for (e = bfg[i][j]->out; e != NULL; e = e->next_out) {
		if (e->dst->id >= num_bfg_nodes)
		    printf("%d(%d_%x) ", e->dst->id, e->dst->id, e->dst->pi);
		else
		    printf("%d(%d_%x) ", e->dst->id, e->dst->bbi->id, e->dst->pi);
	    }
	    printf(" ]\n");
	    if (bbi_type(bfg[i][j]->bbi) == CTRL_COND)
		bnum++;
	}
    }
}



static void
build_bfg()
{
    init_bfg();
    trav_tcfg();
    collect_bfg_nodes();
    //dump_bfg();
}






// TASK3: building btg
// =============================================================================

btg_edge_t	**btg_in, **btg_out;
btg_edge_t	**end_bbb_in, **root_bbb_out;


// p(x -> y)
static void
reach(bfg_node_t *x, bfg_node_t *y, int branch)
{
    btg_edge_t	*p;

    p = (btg_edge_t *) malloc(sizeof(btg_edge_t));
    CHECK_MEM(p);

    p->src = x; p->dst = y;
    p->branch = branch;
    p->next_out = btg_out[x->id];
    btg_out[x->id] = p;
    p->next_in = btg_in[y->id];
    btg_in[y->id] = p;
}



// P(x, end_bbb)
static void
reach_end(bfg_node_t *x, int branch)
{
    btg_edge_t	*p;

    p = (btg_edge_t *) malloc(sizeof(btg_edge_t));
    CHECK_MEM(p);

    p->src = x; p->dst = &end_bbb;
    p->branch = branch;
    p->next_out = btg_out[x->id];
    btg_out[x->id] = p;
    p->next_in = end_bbb_in[x->pi];
    end_bbb_in[x->pi] = p;
}



static void
collect_reachables(bfg_node_t *bbb, int branch)
{
    int		reached, end_reached;
    Queue	worklist;
    bfg_node_t	*x, *y;
    bfg_edge_t	*e;

    if (bbb->out->branch == branch) {
	x = bbb->out->dst;
	reached = (int) bbb->out;
    } else {
	x = bbb->out->next_out->dst;
	reached = (int) bbb->out->next_out;
    }

    init_queue(&worklist, sizeof(bfg_node_t *));
    enqueue(&worklist, &x);
    x->flags = reached;
    end_reached = 0;

    while (!queue_empty(&worklist)) {
	x = *((bfg_node_t **) dequeue(&worklist));
	if (x->pi == bbb->pi) {
	    reach(bbb, x, branch);
	} else if (x->out == NULL) {	// bbb => end
	    if (!end_reached) {
		reach_end(bbb, branch);
		end_reached = 1;
	    }
	} else {
	    y = x->out->dst;
	    if (y->flags != reached) {
		enqueue(&worklist, &y);
		y->flags = reached;
	    }
	    y = x->out->next_out->dst;
	    if (y->flags != reached) {
		enqueue(&worklist, &y);
		y->flags = reached;
	    }
	}
    }
    free_queue(&worklist);
}



// build an edge indicating reachablility from root to target without passing other
// node with the same branch context
static void
root_reach(bfg_node_t *target)
{
    btg_edge_t	*p;

    p = (btg_edge_t *) malloc(sizeof(btg_edge_t));
    CHECK_MEM(p);

    p->src = &root_bbb; p->dst = target;
    p->branch = NOT_TAKEN;
    p->next_out = root_bbb_out[target->pi];
    root_bbb_out[target->pi] = p;
    p->next_in = btg_in[target->id];
    btg_in[target->id] = p;
}



// P(root_bbb, end_bbb, pi)
static void
root_reach_end(int pi)
{
    btg_edge_t	*p;

    p = (btg_edge_t *) malloc(sizeof(btg_edge_t));
    CHECK_MEM(p);

    p->src = &root_bbb; p->dst = &end_bbb;
    p->branch = NOT_TAKEN;
    p->next_out = root_bbb_out[pi];
    root_bbb_out[pi] = p;
    p->next_in = end_bbb_in[pi];
    end_bbb_in[pi] = p;
}



// reachablility root->target without passing any other node with the same branch
// context as target
static void
collect_root_reachable(int pi)
{
    int		end_reached;
    Queue	worklist;
    bfg_node_t	*x, *y;
    bfg_edge_t	*e;

    if (vbbb[0]->pi == pi) {
	root_reach(vbbb[0]);
	return;
    }

    init_queue(&worklist, sizeof(bfg_node_t *));
    enqueue(&worklist, &vbbb[0]);
    vbbb[0]->flags == pi;	// if a node's flags == pi, it has been visted before
    end_reached = 0;

    while (!queue_empty(&worklist)) {
	x = *((bfg_node_t **) dequeue(&worklist));
	if (x->pi == pi) {
	    root_reach(x);
	} else if (x->out == NULL) {	// root => end
	    if (!end_reached) {
		root_reach_end(pi);
		end_reached = 1;
	    }
	} else {
	    y = x->out->dst;
	    if (y->flags != pi) {
		enqueue(&worklist, &y);
		y->flags = pi;
	    }
	    y = x->out->next_out->dst;
	    if (y->flags != pi) {
		enqueue(&worklist, &y);
		y->flags = pi;
	    }
	}
    }
    free_queue(&worklist);
}



static void
dump_btg()
{
    int		i, id;
    btg_edge_t	*p;

    printf("\ndump branch transition graph\n");

    for (i = 0; i < BHT_SIZE; i++) {
	if (root_bbb_out[i] == NULL)
	    continue;
	printf("root[%d]: ", i);
	for (p = root_bbb_out[i]; p != NULL; p = p->next_out)
	    printf("%d ", p->dst->id);
	printf("\n");
    }

    for (i = 0; i < num_bfg_nodes; i++) {
	if (!(bbi_type(vbbb[i]->bbi) == CTRL_COND))
	    continue;
	printf("OUT[%d]: ", i);

	for (p = btg_out[i]; p != NULL; p = p->next_out)
	    printf("%d(%c) ", p->dst->id, (p->branch == TAKEN) ? 'T' : 'N');
	printf("\n");

	printf("IN[%d]: ", i);
	for (p = btg_in[i]; p != NULL; p = p->next_in)
	    printf("%d(%c) ", p->src->id, (p->branch == TAKEN) ? 'T' : 'N');
	printf("\n");
    }

    for (i = 0; i < BHT_SIZE; i++) {
	if (end_bbb_in[i] == NULL)
	    continue;
	printf("end[%d]: ", i);
	for (p = end_bbb_in[i]; p != NULL; p = p->next_in)
	    if (p->src->id == root_bbb_id)
		printf("%d ", p->src->id);
	    else
		printf("%d(%c) ", p->src->id, (p->branch == TAKEN) ? 'T' : 'N');
	printf("\n");
    }
}



static void
build_btg()
{
    int		i;

    btg_in = (btg_edge_t **) calloc(num_bfg_nodes, sizeof(btg_edge_t *));
    btg_out = (btg_edge_t **) calloc(num_bfg_nodes, sizeof(btg_edge_t *));
    CHECK_MEM(btg_in);
    CHECK_MEM(btg_out);
    root_bbb_out = (btg_edge_t**) calloc(BHT_SIZE, sizeof(btg_edge_t *));
    end_bbb_in = (btg_edge_t**) calloc(BHT_SIZE, sizeof(btg_edge_t *));

    for (i = 0; i < num_bfg_nodes; i++) {
	collect_reachables(vbbb[i], TAKEN);
	collect_reachables(vbbb[i], NOT_TAKEN);
    }

    // will use: pi == flags to check whether a node has been visited, since pi is in
    // [0..BHT_SIZE-1], so set them to BHT_SIZE initially
    for (i = 0; i< num_bfg_nodes; i++)
	vbbb[i]->flags = BHT_SIZE;  
    end_bbb.flags = BHT_SIZE;
    for (i=0; i < BHT_SIZE; i++) {
	if (pi_table[i])
	    collect_root_reachable(i);
    }
    for (i=0; i < num_bfg_nodes; i++)
	vbbb[i]->flags = 0;	// reset flags

    //dump_btg();
}



void
bpred_analysis()
{
    //printf("bpred_analysis...\n");
    collect_mp_insts();
    build_bfg();
    build_btg();
}
