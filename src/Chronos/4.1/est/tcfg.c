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

 * $Id: tcfg.c,v 1.2 2006/06/24 08:54:57 lixianfe Exp $

 *

 ******************************************************************************/



#include <stdlib.h>

#include "common.h"

#include "tcfg.h"



extern prog_t	prog;

tcfg_node_t	**tcfg;

int		num_tcfg_nodes = 0, tcfg_size = 0;

tcfg_edge_t	**tcfg_edges;

int		num_tcfg_edges = 0;

tcfg_nlink_t	***bbi_map;







// return the proc id of a tcfg node (basic block instance, or bbi)

int

bbi_pid(tcfg_node_t *bbi)

{

    return bbi->bb->proc->id;

}







// return the basic block id of a tcfg node

int

bbi_bid(tcfg_node_t *bbi)

{

    return bbi->bb->id;

}







// return 1 if the tcfg node corresponds to a conditional basic block

int

cond_bbi(tcfg_node_t *bbi)

{

    return (bbi->bb->type == CTRL_COND);

}







// return 1 if the tcfg edge corresponds to a back edge in the cfg

int

bbi_backedge(tcfg_edge_t *edge)

{

    proc_t  *proc1, *proc2;

    int	    bid1, bid2;



    proc1 = edge->src->bb->proc;

    proc2 = edge->dst->bb->proc;

    bid1 = edge->src->bb->id;

    bid2 = edge->dst->bb->id;



    if ((proc1 == proc2) && (bid1 >= bid2))

	return 1;

    else

	return 0;

}







// return 1 if the tcfg edge goes to the loop entry

int

bbi_is_loopback(tcfg_edge_t *edge, int head, int tail)

{

    if ((edge->dst->bb->id == head) 

	&& (BETWEEN(edge->src->bb->id, head, tail))

	&& (edge->dst->bb->proc == edge->src->bb->proc))

	return 1;

    else

	return 0;

}







// return 1 if the tcfg node is return

int

bbi_is_return(tcfg_node_t *bbi)

{

    if ((bbi->bb->out_n == NULL) && (bbi->bb->out_t == NULL))

	return 1;

    else

	return 0;

}







static tcfg_node_t *

new_tcfg_node(cfg_node_t *bb)

{

    tcfg_node_t	*bbi;



    bbi = (tcfg_node_t *) calloc(1, sizeof(tcfg_node_t));

    CHECK_MEM(bbi);

    bbi->bb = bb;

    bbi->id = num_tcfg_nodes;



    return bbi;

}







static tcfg_edge_t *

new_tcfg_edge(tcfg_node_t *src, tcfg_node_t *dst, int branch)

{

    tcfg_edge_t	*edge, *edge1;



    edge = (tcfg_edge_t *) calloc(1, sizeof(tcfg_edge_t));

    CHECK_MEM(edge);

    edge->src = src; edge->dst = dst; edge->branch = branch;



    if (src->out == NULL)

	src->out = edge;

    else {

	edge1 = src->out;

	while (edge1->next_out != NULL)

	    edge1 = edge1->next_out;

	edge1->next_out = edge;

    }

    if (dst->in == NULL)

	dst->in = edge;

    else {

	edge1 = dst->in;

	while (edge1->next_in != NULL)

	    edge1 = edge1->next_in;

	edge1->next_in = edge;

    }

    return edge;

}



extern int bdepth;
int test_depth(int pid, int depth);

// create a proc instance (virtual inlining), it may recursively call itself if proc

// calls are encountered in current proc, if this proc is called by some block,

// call_bb is the caller and ret_bb is the block to return to (thus correponding tcfg

// edges should be constructed); otherwise they are NULL

static void

proc_inline(proc_t *proc, tcfg_node_t *call_bbi, tcfg_node_t *ret_bbi, int depth)

{

    int		i;
    cfg_node_t	*bb;
    tcfg_node_t	**sub_tcfg;


    sub_tcfg = (tcfg_node_t **) calloc(proc->num_bb + 1, sizeof(tcfg_node_t *));

    CHECK_MEM(sub_tcfg);


    // 1. create tcfg nodes of the corresponding proc & remember them in the map

    for (i = 0; i < proc->num_bb; i++) {

	if (num_tcfg_nodes >= tcfg_size) {

	    // tcfg store is full, need to realloc memory for more tcfg nodes

	    tcfg_size += 64;

	    tcfg = (tcfg_node_t **) realloc(tcfg, tcfg_size * sizeof(tcfg_node_t *));

	}
	sub_tcfg[i] = tcfg[num_tcfg_nodes] = new_tcfg_node(&proc->cfg[i]);

	num_tcfg_nodes++;

    }


    // 2. recursively create proc instance for callees

    for (i = 0; i < proc->num_bb; i++) {

	   if (proc->cfg[i].type == CTRL_CALL) {


	  /* vivy: guard against recursive function
	   * for now, ignore the call
	   * needs to refine later
	   */
	  if( proc->cfg[i].callee != proc->cfg[i].proc ){
	    proc_inline(proc->cfg[i].callee, sub_tcfg[i], sub_tcfg[i+1],0);
          }
	  else{
	    //fprintf( stderr, "Recursive function detected: call to self at proc %d bb %d.\n", proc->cfg[i].proc->id, proc->cfg[i].id );
              if(!bdepth)
                 continue;
              if(test_depth(proc->id, depth + 1) ){
                  //printf("%d-> ",proc->id);  
                  proc_inline(proc->cfg[i].callee, sub_tcfg[i], sub_tcfg[i+1], depth + 1); 
               } else{
                //fprintf(stderr,"out of depth limit for procedure %d\n",proc->id);
             }
          }
	}
    }



    // 3. create tcfg nodes with the help of the map

    // first, create the edge from the caller, if any, to the first tcfg node

    if (call_bbi != NULL)

	new_tcfg_edge(call_bbi, sub_tcfg[0], TAKEN);

    for (i = 0; i < proc->num_bb; i++) {

	// if the block calls a proc, the tcfg edge has been created in the recursive

	// invokation of the callee, thus we do nothing here

	if (proc->cfg[i].type == CTRL_CALL)

	    continue;

	// if the block is return, create an edge from it to ret_bbi if not NULL

	if (proc->cfg[i].type == CTRL_RET) {

	    if (ret_bbi != NULL)

		new_tcfg_edge(sub_tcfg[i], ret_bbi, TAKEN);

	    continue;

	}



	if (proc->cfg[i].out_n != NULL) {

	    bb = proc->cfg[i].out_n->dst;

	    new_tcfg_edge(sub_tcfg[i], sub_tcfg[bb->id], NOT_TAKEN);

	}

	if (proc->cfg[i].out_t != NULL) {

	    bb = proc->cfg[i].out_t->dst;

	    new_tcfg_edge(sub_tcfg[i], sub_tcfg[bb->id], TAKEN);

	}

    }



    free(sub_tcfg);

}







static void

collect_tcfg_edges()

{

    int         i, edge_id = 0;

    tcfg_edge_t *e;





    num_tcfg_edges = 0;

    for (i = 0; i < num_tcfg_nodes; i++) {

	for (e = tcfg[i]->out; e != NULL; e = e->next_out)

	    num_tcfg_edges++;

    }



    tcfg_edges = (tcfg_edge_t **) calloc(num_tcfg_edges, sizeof(tcfg_edge_t *));

    CHECK_MEM(tcfg_edges);

    for (i = 0; i < num_tcfg_nodes; i++) {

	for (e = tcfg[i]->out; e != NULL; e = e->next_out) {

	    e->id = edge_id++;

	    tcfg_edges[e->id] = e;

	}

    }

}







static void

update_bbi_map(tcfg_node_t *bbi)

{

    tcfg_nlink_t	*nl;

    int			pid, bid;



    nl = (tcfg_nlink_t *) calloc(1, sizeof(tcfg_nlink_t));

    nl->bbi = bbi;

    pid = bbi_pid(bbi); bid = bbi_bid(bbi);

    nl->next = bbi_map[pid][bid];

    bbi_map[pid][bid] = nl;

}







static void

build_bbi_map()

{

    int		i;



    bbi_map = (tcfg_nlink_t ***) calloc(prog.num_procs,

	    sizeof(tcfg_nlink_t **));

    CHECK_MEM(bbi_map);

    for (i = 0; i < prog.num_procs; i++) {

	bbi_map[i] = (tcfg_nlink_t **) calloc(prog.procs[i].num_bb,

		sizeof(tcfg_nlink_t *));

	CHECK_MEM(bbi_map[i]);

    }



    for (i = 0; i < num_tcfg_nodes; i++)

	update_bbi_map(tcfg[i]);

}







// transform the CFGs of the procs into a global flow graph (transformed-CFG)

void

prog_tran()

{

    proc_t	    *proc;

    FILE *ftcfg;

    proc = &prog.procs[prog.main_proc];

    proc_inline(proc, NULL, NULL,0);

    collect_tcfg_edges();

    ftcfg = fopen( "tcfg.map", "w" );
    dump_tcfg( ftcfg );
    fclose( ftcfg );

    build_bbi_map();

}







void

clear_bbi_flags()

{

    int		i;



    for (i = 0; i < num_tcfg_nodes; i++)

	tcfg[i]->flags = 0;

}







void

clear_tcfg_edge_flags()

{

    int		i;



    for (i = 0; i < num_tcfg_edges; i++)

	tcfg_edges[i]->flags = 0;

}







int

bbi_type(tcfg_node_t *bbi)

{

    return bbi->bb->type;

}

























void

dump_tcfg(FILE *fp)

{

    tcfg_node_t *bbi;

    tcfg_edge_t	*edge;

    int         i;



    fprintf(fp, "dump tcfg...\n");

    for (i = 0; i < num_tcfg_nodes; i++) {

	bbi = tcfg[i];

	fprintf(fp, "%d(%d.%d): [ ", bbi->id, bbi_pid(bbi), bbi_bid(bbi));



	for (edge = bbi->out; edge != NULL; edge = edge->next_out) {

	    fprintf(fp, "%d ", edge->dst->id);

	}

	fprintf(fp, "]\n");

    }

    fprintf(fp, "\n");

}



