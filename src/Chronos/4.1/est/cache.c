/************************************************************************************
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

 * $Id: cache.c,v 1.4 2006/07/23 09:20:45 lixianfe Exp $
 *
 ***********************************************************************************/


#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "tcfg.h"
#include "cache.h"
#include "bpred.h"
#include "loops.h"


extern prog_t	    prog;
extern int	    num_tcfg_nodes;
extern tcfg_node_t  **tcfg;
extern int	    num_tcfg_edges;
extern tcfg_edge_t  **tcfg_edges;
extern int	    pipe_ibuf_size;
extern int	    pipe_iwin_size;
extern int	    bpred_scheme;
extern int	    *num_mp_insts;
extern de_inst_t    ***mp_insts;
extern int	    num_tcfg_loops;
extern loop_t	    **loop_map;
extern loop_t	    **loops;
extern loop_t	    ***loop_comm_ances;

// num_hit_miss[i]: number of hit/miss combinations at entry to edge[i]->dst
int		    *num_hit_miss;
cache_t		    cache;
// number of memory blocks in each basic block
int		    *num_mblks;
// gen[i][j]: the j-th memory update in gen[i] -- the function updating a cache
// state due to the execution of basic block i; 
// note: gen[i][j].set == MAX_CACHE_SETS means this entry is invalid and it
// indicates the end of updates in this gen function
mem_blk_t	    **gen;
// memory update function due to mispred along alternative path of edge[i]; note
// - the speculative execution is along the alternative path of edge[i]
// - mp_gen[i][j].set == MAX_CACHE_SETS means this entry is invalid and it
//   indicates the end of updates in this speculative gen function
mem_blk_t	    **mp_gen;

typedef struct tag_link_t   tag_link_t;
struct tag_link_t {
    unsigned short  tag;
    tag_link_t	    *next;
};

tag_link_t	***loop_cache_tags;
int		**num_mblk_conflicts;
loop_t		***mblk_hit_loop;
loop_t		***bbi_hm_list;
extern int	*num_hit_miss;



void
dump_gen();

void
dump_loop_tags();

void
dump_mblk_hitloop();

void
dump_hm_list();




void
set_cache_basic(int nsets, int assoc, int bsize, int miss_penalty)
{
    cache.ns = nsets;
    cache.na = assoc;
    cache.ls = bsize;
    cache.cmp = miss_penalty;
}



void
set_cache()
{
    int		i, n;

    cache.nsb = log_base2(cache.ns);
    cache.lsb = log_base2(cache.ls);
    // tag bits, #tags mapping to each set
    cache.ntb = MAX_TAG_BITS;
    cache.nt = 1 << cache.ntb;
    // tag + set bits, set + line bits, # of tag + set
    cache.t_sb = cache.ntb + cache.nsb;
    cache.s_lb = cache.nsb + cache.lsb;
    cache.nt_s = 1 << cache.t_sb;
    // cache line mask
    cache.l_msk = (1 << cache.lsb) - 1;
    // set mask
    cache.s_msk = 0;
    for (i = 1; i < cache.ns; i <<= 1)
	cache.s_msk |= i;
    cache.s_msk <<= cache.lsb;
    // tag mask
    cache.t_msk = 0;
    for (i = 1; i < cache.nt; i <<= 1)
	cache.t_msk |= i;
    cache.t_msk <<= cache.lsb + cache.nsb;
    // set+tag mask
    cache.t_s_msk = cache.t_msk | cache.s_msk;
}





// calculate gen function due to the execution of bbi
static void
tcfg_node_gen(tcfg_node_t *bbi)
{
    addr_t	    addr, ea;
    mem_blk_t	    mblk, *p;
    int		    i, set, tag, num_updates = 0;
    Queue	    worklist;

    init_queue(&worklist, sizeof(mem_blk_t));

    addr = CLEAR_LSB(bbi->bb->sa);
    ea = bbi->bb->sa + bbi->bb->size;
    while (addr < ea) {
	mblk.set = SET(addr);
	mblk.tag = TAG(addr);
	enqueue(&worklist, &mblk);
	addr += cache.ls;
	num_updates++;
    }

    gen[bbi->id] = (mem_blk_t *) calloc(num_updates+1, sizeof(mem_blk_t));
    CHECK_MEM(gen[bbi->id]);
    for (i = 0; i < num_updates; i++) {
	p = (mem_blk_t *) dequeue(&worklist);
	memmove(&gen[bbi->id][i], p, sizeof(mem_blk_t));
    }
    gen[bbi->id][i].set = MAX_CACHE_SETS;

    free_queue(&worklist);
}



// calculate mp_gen function due to mispred along the path from edge e
static void
tcfg_edge_mp_gen(tcfg_edge_t *e)
{
    int		i, set, tag, num_updates = 0;
    addr_t	addr, ea;
    mem_blk_t	mblk, *p;
    Queue	worklist;

    init_queue(&worklist, sizeof(mem_blk_t));
    
    mblk.set = MAX_CACHE_SETS;
    for (i = 0; i < num_mp_insts[e->id]; i++) {
	addr = CLEAR_LSB(mp_insts[e->id][i]->addr);
	ea = mp_insts[e->id][i]->addr + mp_insts[e->id][i]->size;
	while (addr < ea) {
	    set = SET(addr);
	    tag = TAG(addr);
	    if ((set != mblk.set) || (tag != mblk.tag)) {
		mblk.set = set;
		mblk.tag = tag;
		enqueue(&worklist, &mblk);
		num_updates++;
	    }
	    addr += cache.ls;
	}
    }
    mp_gen[e->id] = (mem_blk_t *) calloc(num_updates+1, sizeof(mem_blk_t));
    CHECK_MEM(mp_gen[e->id]);
    for (i = 0; i < num_updates; i++) {
	p = (mem_blk_t *) dequeue(&worklist);
	memmove(&mp_gen[e->id][i], p, sizeof(mem_blk_t));
    }
    mp_gen[e->id][i].set = MAX_CACHE_SETS;

    free_queue(&worklist);
}



// calculate gen function: cache lines updated by execution of tcfg edge src
static void
calc_gen()
{
    int		i;

    // allocate memory for gen functions
    gen = (mem_blk_t **) calloc(num_tcfg_nodes, sizeof(mem_blk_t *));
    CHECK_MEM(gen);
    mp_gen = (mem_blk_t **) calloc(num_tcfg_edges, sizeof(mem_blk_t *));
    CHECK_MEM(mp_gen);

    for (i = 0; i < num_tcfg_nodes; i++) {
	tcfg_node_gen(tcfg[i]);
    }

    if (bpred_scheme == NO_BPRED)
	return;

    for (i = 0; i < num_tcfg_edges; i++) {
	if (num_mp_insts[i] > 0)
	    tcfg_edge_mp_gen(tcfg_edges[i]);
    }
}



// calculate the number of memory blocks in each basic block
static void
calc_num_mblks()
{
    int		i;
    addr_t	sa, ea;

    num_mblks = (int *) calloc(num_tcfg_nodes, sizeof(int));
    CHECK_MEM(num_mblks);

    for (i = 0; i < num_tcfg_nodes; i++) {
	sa = LSB_OFF(tcfg[i]->bb->sa);
	ea = tcfg[i]->bb->sa + tcfg[i]->bb->size;
	ea = LSB_OFF(ea - 1);
	num_mblks[i] = ea - sa + 1;
    }
}



void
get_mblks()
{
    calc_num_mblks();
    calc_gen();
}


static tag_link_t *
search_tag(int lp_id, unsigned short set, unsigned short tag)
{
    tag_link_t	*tag_list, *p;

    tag_list = loop_cache_tags[lp_id][set];
    for (p = tag_list; p != NULL; p = p->next) {
	if (p->tag == tag)
	    return p;
    }
    return NULL;
}



static void
add_tag(int lp_id, unsigned short set, unsigned short tag)
{
    tag_link_t	*x;

    x = (tag_link_t *) calloc(1, sizeof(tag_link_t));
    x->tag = tag;
    x->next = loop_cache_tags[lp_id][set];
    loop_cache_tags[lp_id][set] = x;
}



// for each pair of (cache_set, loop_level), get its #TAGS (conflicting memory
// blocks)
static void
get_loop_tags()
{
    int		    i, j;
    unsigned short  set, tag;
    mem_blk_t	    *mblk;
    loop_t	    *lp;
    tag_link_t	    *p;
    addr_t	    addr;

    loop_cache_tags = (tag_link_t ***) calloc(num_tcfg_loops, sizeof(tag_link_t **));
    for (i = 0; i < num_tcfg_loops; i++)
	loop_cache_tags[i] = (tag_link_t **) calloc(cache.ns, sizeof(tag_link_t *));
    num_mblk_conflicts = (int **) calloc(num_tcfg_loops, sizeof(int *));
    for (i = 0; i < num_tcfg_loops; i++)
	num_mblk_conflicts[i] = (int *) calloc(cache.ns, sizeof(int));

    for (i = 0; i < num_tcfg_nodes; i++) {
	lp = loop_map[i];
	if (lp == NULL)
	    continue;
	for (j = 0; j < num_mblks[i]; j++) {
	    set = gen[i][j].set;
	    tag = gen[i][j].tag;
	    if (search_tag(lp->id, set, tag) == NULL)
		add_tag(lp->id, set, tag);
	}
	// sequential fetch before branch redirection happens
	addr = tcfg[i]->bb->sa +  tcfg[i]->bb->size;
	for (j = 0; j < pipe_ibuf_size - 1; j++) {
	    set = SET(addr);
	    tag = TAG(addr);
	    if (search_tag(lp->id, set, tag) == NULL)
		add_tag(lp->id, set, tag);
	    addr += tcfg[i]->bb->code[0].size;
	}
    }
    //dump_loop_tags();

    // XXX: can be optimized if children info is maintained
    for (i = 0; i < num_tcfg_loops; i++) {
	lp = loops[i];
	for (j = 0; j < num_tcfg_loops; j++) {
	    if (i == j)
		continue;
	    if (loop_comm_ances[i][j] != lp)
		continue;
	    for (set = 0; set < cache.ns; set++) {
		for (p = loop_cache_tags[j][set]; p != NULL; p = p->next) {
		    if (search_tag(lp->id, set, p->tag) == NULL)
			add_tag(lp->id, set, p->tag);
		}
	    }
	}
    }
    //dump_loop_tags();

    for (i = 0; i < num_tcfg_loops; i++) {
	for (set = 0; set < cache.ns; set++) {
	    for (p = loop_cache_tags[i][set]; p != NULL; p = p->next) {
		num_mblk_conflicts[i][set]++;
	    }
	    //printf("conflicts: (%d, %d): %d\n", i, set, num_mblk_conflicts[i][set]);
	}
    }
}



// collect conflicting memory blocks to (tag0, set0) along the mispred path
// starting from edge
static int
get_mp_conflicts(tcfg_node_t *bbi)
{
    tcfg_node_t	    *src;
    tcfg_edge_t	    *e, *mp_e;
    mem_blk_t	    *mblks;
    unsigned short  tag0, set0, conflict_tags[32];
    int		    num_conflicts, tmp, i, j, max_conflicts = 0;

    tag0 = TAG(bbi->bb->sa);
    set0 = SET(bbi->bb->sa);
    for (e = bbi->in; e != NULL; e = e->next_in) {
	src = e->src;
	if (bbi_type(src) != CTRL_COND)
	    continue;
	mp_e = src->out;
	if (mp_e->dst == bbi)
	    mp_e = mp_e->next_out;
	mblks = mp_gen[mp_e->id];
	num_conflicts = 0; i = 0;
	while (mblks[i].set != MAX_CACHE_SETS) {
	    if ((mblks[i].set == set0) && (mblks[i].tag != tag0)) {
		for (j = 0; j < num_conflicts; j++) {
		    if (conflict_tags[j] == mblks[i].tag)
			break;
		}
		if (j == num_conflicts) {
		    conflict_tags[num_conflicts] = mblks[i].tag;
		    num_conflicts++;
		    if (num_conflicts == 32)
			break;
		}
	    }
	    i++;
	}
	if (num_conflicts > max_conflicts)
	    max_conflicts = num_conflicts;
    }
    return max_conflicts;
}



static int
handle_first_mblk(int bbi_id)
{
    int		ts1, ts2, num_conflicts;
    tcfg_edge_t	*e;
    cfg_node_t	*bb;

    if (bbi_id == 0)
	return 0;

    bb = tcfg[bbi_id]->bb;
    ts1 = TAGSET(bb->sa);
    for (e = tcfg[bbi_id]->in; e != NULL; e = e->next_in) {
	bb = e->src->bb;
	ts2 = TAGSET(bb->sa + bb->size - 1);
	if (ts1 != ts2)
	    break;
    }
    if (e != NULL)
	return 0;

    if (bpred_scheme == NO_BPRED) {
	mblk_hit_loop[bbi_id][0] = loops[0];
    } else {
	num_conflicts = get_mp_conflicts(tcfg[bbi_id]);
	if (num_conflicts < cache.na)
	    mblk_hit_loop[bbi_id][0] = loops[0];
	else
	    mblk_hit_loop[bbi_id][0] = NULL;
    }
    return 1;
}



static void
handle_other_mblk(int bbi_id, int start_mb)
{
    int		mb_id, set;
    loop_t	*lp;

    for (mb_id = start_mb; mb_id < num_mblks[bbi_id]; mb_id++) {
	set = gen[bbi_id][mb_id].set;
	for (lp = loop_map[bbi_id]; lp != NULL; lp = lp->parent) {
	    if (num_mblk_conflicts[lp->id][set] > cache.na)
		break;
	    mblk_hit_loop[bbi_id][mb_id] = lp;
	}
    }
}



// for each memory block mb, find the loop level, where no sufficient
// conflicting memory blocks in the same set in the loop, and in upper loop
// levels, mb either has more conflicting memory blocks than assoicativity, or a
// cold miss if the loop level is the whole program
static void
find_hitloop()
{
    int		    i, start_mb;
    loop_t	    *lp;
    tcfg_edge_t	    *e;

    get_loop_tags();

    mblk_hit_loop = (loop_t ***) calloc(num_tcfg_nodes, sizeof(loop_t **));
    for (i = 0; i < num_tcfg_nodes; i++)
	mblk_hit_loop[i] = (loop_t **) calloc(num_mblks[i], sizeof(loop_t *));
    for (i = 0; i < num_tcfg_nodes; i++) {
	start_mb = handle_first_mblk(i);
	handle_other_mblk(i, start_mb);
    }
    //dump_mblk_hitloop();
}



static void
bbi_categorize(tcfg_node_t *bbi, loop_t **bbi_hit_loops, int len)
{
    int		i;

    if (len == 0) {
	bbi_hm_list[bbi->id] = (loop_t **) calloc(1, sizeof(loop_t *));
	bbi_hm_list[bbi->id][0] = NULL;
	num_hit_miss[bbi->id] = 1;
	return;
    }
    bbi_hm_list[bbi->id] = (loop_t **) calloc(len+1, sizeof(loop_t *));
    memmove(bbi_hm_list[bbi->id], bbi_hit_loops, len * sizeof(loop_t *));
    bbi_hm_list[bbi->id][len] = bbi_hm_list[bbi->id][len-1]->parent;
    num_hit_miss[bbi->id] = len+1;
}



// get patterns of hits/misses of memory blocks. each pattern is
// associated with a loop level (the number of times that the basic block is
// executed under this pattern of hits/misses is less than the number of times
// that the execution is repeated by loop backs of this level)
static void
categorize()
{
    int		i, j, all_miss = 0, len;
    loop_t	*queue[MAX_LOOP_NEST], *lp;

    bbi_hm_list = (loop_t ***) calloc(num_tcfg_nodes, sizeof(loop_t **));
    num_hit_miss = (int *) calloc(num_tcfg_nodes, sizeof(int));

    set_loop_flags(0);
    for (i = 0; i < num_tcfg_nodes; i++) {
	len = 0;
	for (lp = loop_map[i]; lp != NULL; lp = lp->parent) {
	    for (j = 0; j < num_mblks[i]; j++) {
		if (lp == mblk_hit_loop[i][j]) {
		    queue[len++] = lp;
		    break;
		}
	    }
	}
	bbi_categorize(tcfg[i], queue, len);
    }
    set_loop_flags(0);
    //dump_hm_list();
}



void
cache_analysis()
{
    //printf("mblk categorization (hit or unknown)\n");
    set_cache();
    get_mblks();
    find_hitloop();
    categorize();
}



int
get_mblk_hitmiss(tcfg_node_t *bbi, int mblk_id, loop_t *lp)
{
    loop_t	*lp1, *lp2;

    lp1 = mblk_hit_loop[bbi->id][mblk_id];
    if ((lp == NULL) || (lp1 == NULL))
	return IC_MISS;
    else if (lp == lp1)
	return IC_HIT;
    else {
	lp2 = loop_comm_ances[lp->id][lp1->id];
	if (lp2 == lp1)
	    return IC_HIT;
	else
	    return IC_MISS;
    }
}






void
dump_gen()
{
    tcfg_node_t	*src, *dst;
    mem_blk_t	*mblk;
    int		i;

    for (i = 0; i < num_tcfg_edges; i++) {
	src = tcfg_edges[i]->src;
	dst = tcfg_edges[i]->dst;
	printf("tcfg_edge: %d.%d -> %d.%d\n", bbi_pid(src), bbi_bid(src),
		bbi_pid(dst), bbi_bid(dst));
	for (mblk = gen[src->id]; mblk->set != MAX_CACHE_SETS; mblk++)
		printf("\tset=%x: %x\n", mblk->set, mblk->tag);
	if (mp_gen[i] == NULL)
	    continue;
	printf("\t---------------\n");
	for (mblk = mp_gen[i]; mblk->set != MAX_CACHE_SETS; mblk++)
		printf("\tset=%x: %x\n", mblk->set, mblk->tag);

    }
}



void
dump_loop_tags()
{
    int		    i;
    unsigned short  set, tag;
    tag_link_t	    *p;

    printf("dump loop tags...\n");
    for (i = 0; i < num_tcfg_loops; i++) {
	printf("loop[%d]\n", i);
	for (set = 0; set < cache.ns; set++) {
	    printf("\t%d: ", set);
	    for (p = loop_cache_tags[i][set]; p != NULL; p = p->next)
		printf("%x ", p->tag);
	    printf("\n");
	}
    }
}



void
dump_mblk_hitloop()
{
    int		i, j;
    printf("\ndump_mblk_hit_loop...\n");
    for (i = 0; i < num_tcfg_nodes; i++) {
	printf("bbi[%d]/%d:\n", i, num_mblks[i]);
	for (j = 0; j < num_mblks[i]; j++) {
	    if (mblk_hit_loop[i][j] == NULL)
		printf("\t%d: loop[ ]\n", j);
	    else
		printf("\t%d: loop[%d]\n", j, mblk_hit_loop[i][j]->id);
	}
    }
}



void
dump_hm_list()
{
    int		i, j;

    printf("dump bbi_hm_list...\n");
    for (i = 0; i < num_tcfg_nodes; i++) {
	printf("bbi[%d]: ", i);
	for (j = 0; j < num_hit_miss[i]; j++) {
	    if (bbi_hm_list[i][j] == NULL)
		printf("\tloop[ ] ");
	    else
		printf("\tloop[%d] ", bbi_hm_list[i][j]->id); 
	}
	printf("\n");
    }
}
