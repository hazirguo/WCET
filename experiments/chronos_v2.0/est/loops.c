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
 * $Id: loops.c,v 1.2 2006/06/24 08:54:57 lixianfe Exp $
 *
 * traverse tcfg to form a loop hierarchy and associate each bbi its loop level
 *
 ******************************************************************************/


#include <stdlib.h>
#include "common.h"
#include "loops.h"



#define	loop_bbb_idx(id)    ((id) - lp_bbb_offset - num_bfg_nodes)

extern int	    num_tcfg_nodes;
extern int	    num_tcfg_edges;
extern tcfg_node_t  **tcfg;
extern tcfg_edge_t  **tcfg_edges;

loop_t	    **loops;
int	    num_tcfg_loops;
loop_t	    **loop_map;		// bbi => loop
loop_t	    ***loop_comm_ances;	// loop_comm_ances[lp1, lp2]



// create a new loop level with the head & tail being the src and dst of the edge e
static loop_t *
new_loop(tcfg_edge_t *e)
{
    int		i;
    static int	num = 0;
    loop_t	*lp;

    lp = (loop_t *) calloc(1, sizeof(loop_t));
    CHECK_MEM(lp);
    lp->id = num_tcfg_loops++;
    lp->head = e->dst; lp->tail = e->src;

    return lp;
}



void
set_loop_flags(int flag)
{
    int		i;

    for (i = 0; i < num_tcfg_loops; i++)
	loops[i]->flags = flag;
}



static void
find_loops()
{
    int		    i;
    tcfg_edge_t	    *e;

    num_tcfg_loops = 1;	    // the entire program is treated as a loop
    loop_map = (loop_t **) calloc(num_tcfg_nodes, sizeof(loop_t *));
    CHECK_MEM(loop_map);

    for (i = 0; i < num_tcfg_edges; i++) {
	e = tcfg_edges[i];
	if (bbi_backedge(e)) {
	    if (loop_map[e->dst->id] != NULL)
		continue;
	    if (bb_is_loop_head(e->dst->bb) && bb_is_loop_tail(e->src->bb))
		loop_map[e->dst->id] = loop_map[e->src->id] = new_loop(e);
	}
    }

    loops = (loop_t **) calloc(num_tcfg_loops, sizeof(loop_t *));
    CHECK_MEM(loops);
    // outmost loop: the entire program
    loops[0] = (loop_t *) calloc(1, sizeof(loop_t));
    loops[0]->head = tcfg[0];
    loop_map[0] = loops[0];
    CHECK_MEM(loops[0]);
    for (i = 0; i < num_tcfg_nodes; i++) {
	if (loop_map[i] != NULL)
	    loops[loop_map[i]->id] = loop_map[i];
    }
}



// return 1 if e->dst exits the loop to which e->src belongs
static int
exit_loop(tcfg_edge_t *e)
{
    loop_t	    *lp;
    int		    head_bid, tail_bid, dst_bid;

    lp = loop_map[e->src->id];
    if (lp == loops[0])
	return 0;
    if (bbi_pid(e->dst) != bbi_pid(lp->head))
	return 0;
    
    head_bid = bbi_bid(lp->head); tail_bid = bbi_bid(lp->tail);
    dst_bid = bbi_bid(e->dst);
    if ((dst_bid > tail_bid) || (dst_bid < head_bid))
	return 1;
    else
	return 0;
}



static void
reg_loop_exit(tcfg_edge_t *e)
{
    loop_t		*lp;
    tcfg_elink_t	*elink;

    lp = loop_map[e->src->id];
    elink = (tcfg_elink_t *) calloc(1, sizeof(tcfg_elink_t *));
    elink->edge = e; elink->next = lp->exits;
    lp->exits = elink;
}



static void
deal_exit_edge(tcfg_edge_t *e)
{
    loop_t	    *lp;
    tcfg_node_t	    *dst;
    int		    head_bid, tail_bid, lp_pid, bid, pid;

    reg_loop_exit(e);
    dst = e->dst;
    if (loop_map[dst->id] != NULL) {
	if (dst != loop_map[dst->id]->head)
	    return;
	// hit a loop head
	if (loop_map[dst->id]->parent != NULL)
	    return;
	for (lp = loop_map[e->src->id]->parent; lp != loops[0]; lp = lp->parent) {
	    head_bid = bbi_bid(lp->head); tail_bid = bbi_bid(lp->tail);
	    lp_pid = bbi_pid(lp->head);
	    bid = bbi_bid(dst); pid = bbi_pid(dst);
	    if ((pid != lp_pid) || ((bid >= head_bid) && (bid <= tail_bid))) {
		loop_map[dst->id]->parent = lp;
		break;
	    }
	}
	if (loop_map[dst->id]->parent == NULL)
	    loop_map[dst->id]->parent = loops[0];
	return;
    }
    for (lp = loop_map[e->src->id]->parent; lp != loops[0]; lp = lp->parent) {
	head_bid = bbi_bid(lp->head); tail_bid = bbi_bid(lp->tail);
	lp_pid = bbi_pid(lp->head);
	bid = bbi_bid(dst); pid = bbi_pid(dst);
	if ((pid != lp_pid) || ((bid >= head_bid) && (bid <= tail_bid))) {
	    loop_map[dst->id] = lp;
	    break;
	}
    }
    if (loop_map[dst->id] == NULL)
	loop_map[dst->id] = loops[0];
}



static void
deal_other_edge(tcfg_edge_t *e)
{
    if (loop_map[e->dst->id] == NULL) {
	loop_map[e->dst->id] = loop_map[e->src->id];
    } else if (loop_map[e->dst->id] != loop_map[e->src->id]) {
	// hit a loop head
	loop_map[e->dst->id]->parent = loop_map[e->src->id];
    }
}



static void
map_bbi_loop()
{
    Queue	    worklist;
    tcfg_node_t	    *src, *dst;
    tcfg_edge_t	    *e;

    init_queue(&worklist, sizeof(tcfg_node_t *));
    enqueue(&worklist, &tcfg[0]);
    tcfg[0]->flags = 1;
    while (!queue_empty(&worklist)) {
	src = *((tcfg_node_t **) dequeue(&worklist));
	for (e = src->out; e != NULL; e = e->next_out) {
	    dst = e->dst;
	    if (exit_loop(e))
		deal_exit_edge(e);
	    else if (dst->flags == 0)
		deal_other_edge(e);
	    
	    if (dst->flags == 0) {
		enqueue(&worklist, &dst);
		dst->flags = 1;
	    }
	}
    }
    clear_bbi_flags();
    free_queue(&worklist);
}



static void
search_common_ancestor(loop_t *x, loop_t *y)
{
    loop_t  *lp;
    int	    i, flag = (int) x;

    for (lp = x; lp != NULL; lp = lp->parent) {
	if (lp == y) {
	    loop_comm_ances[x->id][y->id] = y;
	    loop_comm_ances[y->id][x->id] = y;
	    return;
	}
	lp->flags = flag;
    }
    for (lp = y; lp != NULL; lp = lp->parent) {
	if (lp->flags == flag) {
	    loop_comm_ances[x->id][y->id] = lp;
	    loop_comm_ances[y->id][x->id] = lp;
	    return;
	}
    }
    /* liangyun */
    /* 
    fprintf(stderr, "Oops, no common ancestor for two loops: %d, %d is found!\n",
	    x->id, y->id);
    exit(1);
    */
}



// find common ancestors of any two loops
static void
loop_relations()
{
    int		i, j;

    // alloc the matrix for common ancestors
    loop_comm_ances = (loop_t ***) calloc(num_tcfg_loops, sizeof(loop_t **));
    for (i = 0; i < num_tcfg_loops; i++)
	loop_comm_ances[i] = (loop_t **) calloc(num_tcfg_loops, sizeof(loop_t *));

    for (i = 0; i < num_tcfg_loops; i++) {
	loop_comm_ances[i][i] = loops[i];
	for (j = i + 1; j < num_tcfg_loops; j++)
	    search_common_ancestor(loops[i], loops[j]);
    }
}



static void
dump_loops()
{
    int		i;
    tcfg_node_t	*x, *y, *z;

    printf("\ndump loops\n");
    printf("----------------\n");
    for (i = 0; i < num_tcfg_nodes; i++) {
	x = tcfg[i];
	if (loop_map[i] == NULL)
	    continue;	// for non-reachable nods, such as block 2.51 in minver
	y = loop_map[i]->head; z = loop_map[i]->tail;
	if (y == tcfg[0])
	    printf("%d.%d: \t%d[start - end]\n",
		    bbi_pid(x), bbi_bid(x), loop_map[i]->id);
	else
	    printf("%d.%d: \t%d[%d.%d - %d.%d]  / parent:%d\n",
		    bbi_pid(x), bbi_bid(x), loop_map[i]->id,
		    bbi_pid(y), bbi_bid(y), bbi_pid(z), bbi_bid(z),
		    loop_map[i]->parent->id);
    }
}



void
dump_loop_comm_ances()
{
    int		i, j;

    printf("\ndump loop common ancestors:\n");
    for (i = 0; i < num_tcfg_loops; i++) {
	for (j = 0; j < num_tcfg_loops; j++) {
	    printf("%2d  ", loop_comm_ances[i][j]->id);
	}
	printf("\n");
    }
}



void
loop_process()
{
    find_loops();
    map_bbi_loop();
    //dump_loops();
    loop_relations();
    //dump_loop_comm_ances();
}




