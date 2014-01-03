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
 * $Id: cfg.c,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "cfg.h"


extern isa_t	*isa;
extern prog_t	prog;



// lookup addr in code, return the index if found, otherwise return -1
// since instr. addresses are in sorted order, we can use binary search
static int
lookup_addr(de_inst_t *code, int num, addr_t addr)
{
    int	    hi = num - 1, lo = 0, i;

    while (hi >= lo) {
	i = (hi + lo) / 2;
	if (code[i].addr == addr)
	    return i;
	else if (code[i].addr > addr)
	    hi = i - 1;
	else
	    lo = i + 1;
    }
    return -1;
}



// scan the decoded instr. and mark the locations in proc_entries if the
// corresponding instr. is the proc entry; it returns number fo procs
static int
scan_procs(int *proc_ent)
{
    int	    main_id, i, tid;

    prog.num_procs = 0;
    for (i = 0; i < prog.num_inst; i++) {
	// check whether this instr. is the main entrance; mark it if so
	if (prog.code[i].addr == prog.main_addr) {
	    if (proc_ent[i] == 0) {
		proc_ent[i] = 1;
		prog.num_procs++;
		main_id = i;
	    }
	} else if (inst_type(&prog.code[i]) == INST_CALL) {
	    // check the target addr of a call; mark it as proc entrance if not
	    // marked yet.
	    tid = lookup_addr(prog.code, prog.num_inst, prog.code[i].target);
	    if (tid == -1) {
		fprintf(stderr, "no match for call: %x\n", prog.code[i].target);
		exit(1);
	    }
	    if (proc_ent[tid] == 0) {
		proc_ent[tid] = 1;
		prog.num_procs++;
	    }
	}
    }
    return main_id;
}

/* liangyun */
extern int bjptb;

// scan the code of a proc and mark the location if it corresponds to a block entry
static void
scan_blocks(int *bb_ent, proc_t *proc)
{
    int		i, type, tid;
    de_inst_t	*inst;

    bb_ent[0] = 1;
    proc->num_bb = 1;

    addr_t adr;
    int bfind;
    int num;

    for (i = 0; i < proc->num_inst; i++) {
	inst = &proc->code[i];
	type = inst_type(inst);
	if ((type == INST_COND) || (type == INST_UNCOND)) {
	    // check the branch target addr, mark it as block entry if not marked yet
	    tid = lookup_addr(proc->code, proc->num_inst, inst->target);
	    if (tid == -1) {
		fprintf(stderr, "no match for branch target: %x\n", inst->target);
		exit(1);
	    }
	    if (bb_ent[tid] == 0) {
		bb_ent[tid] = 1;
		proc->num_bb++;
	    }
	    // if the fall-through addr has not been marked as a block entry yet,
	    // mark it as block entry
	    if (bb_ent[i+1] == 0) {
		bb_ent[i+1] = 1;
		proc->num_bb++;
	    }
	} else if (type == INST_CALL) {
	    // for a call instr, simply mark the next instr as block entry
	    if (bb_ent[i+1] == 0) {
		bb_ent[i+1] = 1;
		proc->num_bb++;
	    }
	} else if (type == INST_RET) {
	    // for a return instr, if it is not the last instr in the proc, mark the
	    // next instr as block entry
	    if ((i < proc->num_inst - 1) && (bb_ent[i+1] == 0)) {
		bb_ent[i+1] = 1;
		proc->num_bb++;                
	    }
            /* liangyun: indirect jump, also the target of this jump*/
                     
            if(bjptb && i < proc->num_inst - 1){
               bfind = lookup_jptable(inst->addr);
               // if bind == 0, can be jr $31,succeeded by nop
               if(bfind > 0){
                  // check the jump target addr, mark it as block entry if not marked yet
                  num = 0;

                  bb_ent[i + 1] += (bfind - 1);       // flag turn 
                  proc->num_bb  += (bfind - 1); // add dummy block                  

                  while( num < bfind ){
                        get_jptable(inst->addr, num, &adr);
                        tid = lookup_addr(proc->code, proc->num_inst, adr);
                        if (tid == -1) {
                            printf("no id for jump target: %x\n", adr);
                            //exit(1);
                         }
                        if (bb_ent[tid] == 0) {
                            bb_ent[tid] = 1;
                            proc->num_bb++;
                        }
                        num++;
                   }
               }
            }
	}
    }
}


// lookup a basic block in a proc with a match of its start addr to the searched addr
static cfg_node_t *
lookup_bb(cfg_node_t *cfg, int num, addr_t addr)
{
    int	    hi = num - 1, lo = 0, i;

    while (hi >= lo) {
	i = (hi + lo) / 2;
	if (cfg[i].sa == addr)
	    return &cfg[i];
	else if (cfg[i].sa > addr)
	    hi = i - 1;
	else
	    lo = i + 1;
    }
    return NULL;
}



// create a new cfg edge from src->dst, "taken" specifies whether this edge is a
// taken edge or a fall-through edge; note since the number of incoming edges of the
// dst block cannot be statically determined, the storage for its incoming edges is
// dynamically scaled.
static void
new_edge(cfg_node_t *src, cfg_node_t *dst, int taken)
{
    cfg_edge_t	*e = (cfg_edge_t *) malloc(sizeof(cfg_edge_t));
    CHECK_MEM(e);
    e->src = src; e->dst = dst;
    if (taken == NOT_TAKEN)
	src->out_n = e;
    else
	src->out_t = e;
    dst->num_in++;
    dst->in = (cfg_edge_t **) realloc(dst->in, dst->num_in * sizeof(cfg_edge_t *));
    CHECK_MEM(dst->in);
    dst->in[dst->num_in - 1] = e;
}

/* liangyun */
extern  int bjptb;

// create outgoing cfg edges for each basic block in the following way:
// - first check the type of the block's last instr
// - create fall-through edge or/and a control transfer edge according to its type
static void
create_cfg_edges(proc_t *proc)
{
    int		i, type;
    cfg_node_t	*bb, *bb1;
 
    addr_t adr;
    int    bfind;

    for (i = 0; i < proc->num_bb; i++) {
	bb = &proc->cfg[i];
	type = inst_type(&bb->code[bb->num_inst-1]);
	if (type == INST_COND) {
	    // create fall-through edge
	    bb1 = &proc->cfg[i+1];
	    new_edge(bb, bb1, NOT_TAKEN);
	    // create taken edge
	    bb1 = lookup_bb(proc->cfg, proc->num_bb, bb->code[bb->num_inst-1].target);
	    if (bb1 == NULL) {
		fprintf(stderr, "cannot find block start with addr: %x\n",
			bb->code[bb->num_inst-1].target);
		exit(1);
	    }
	    new_edge(bb, bb1, TAKEN);
	    bb->type = CTRL_COND;
	} else if (type == INST_UNCOND) {
	    // create taken edges
	    bb1 = lookup_bb(proc->cfg, proc->num_bb, bb->code[bb->num_inst-1].target);
	    if (bb1 == NULL) {
		fprintf(stderr, "cannot find block start with addr: %x\n",
			bb->code[bb->num_inst-1].target);
		exit(1);
	    }
	    new_edge(bb, bb1, TAKEN);
	    bb->type = CTRL_UNCOND;
	} else if (type == INST_RET) {
	    // do not create any edge
	    bb->type = CTRL_RET;
            
            /* liangyun */
            // if it is return jr $31, then it must be the last basic block.
            if(bjptb && i != proc->num_bb - 1 ){
               /*
               //create taken edges
               bfind = lookup_jptable(bb->code[bb->num_inst-1].addr);
               if(bfind == 0){
                   printf("cannot find target addr for %x\n",bb->code[bb->num_inst-1].addr);
                   //exit(1);
               }
               bb1 = lookup_bb(proc->cfg,proc->num_bb,adr);
               if (bb1 == NULL) {
                   printf("cannot find block start with addr: %x\n", adr);
                   //exit(1);
               }
               if(bb1->id == bb->id + 1)
                  new_edge(bb,bb1,TAKEN);
               else 
                  new_edge(bb, bb1, TAKEN);
               bb->type = CTRL_UNCOND; // make it unconditional
               */
               bfind = get_jptable_static(bb->code[bb->num_inst - 1].addr, &adr);
               if(bfind >= 0){
                  //printf("%08x -> %08x\n",bb->code[bb->num_inst - 1].addr, adr);
                  bb1 = lookup_bb(proc->cfg,proc->num_bb,adr);
                  if (bb1 == NULL){
                    printf("cannot find block start with addr: %x\n",adr);
                  } 
                  new_edge(bb,bb1,TAKEN);
                  //printf("edge %d %d\n",bb->id,bb1->id);
                  if(bfind){ //not tail;
                      bb1 = &proc->cfg[i+1];
	              new_edge(bb, bb1, NOT_TAKEN);   
                      //printf("edge %d %d\n",bb->id,bb1->id); 
                  }
                  bb->type = CTRL_UNCOND;
               } 
            } 
	} else {
	    // create fall-through edge (for seqencial block and call block) if
	    // current block is not the last one in the proc
	    if (i < proc->num_bb - 1) {
		bb1 = &proc->cfg[i+1];
		new_edge(bb, bb1, NOT_TAKEN);
	    }
	    if (type == INST_CALL)
		bb->type = CTRL_CALL;
	    else
		bb->type = CTRL_SEQ;
	}
    }
}



// lookup a proc with a match of its start addr to the searched addr
static proc_t *
lookup_proc(addr_t addr)
{
    int	    hi = prog.num_procs - 1, lo = 0, i;

    while (hi >= lo) {
	i = (hi + lo) / 2;
	if (prog.procs[i].sa == addr)
	    return &prog.procs[i];
	else if (prog.procs[i].sa > addr)
	    hi = i - 1;
	else
	    lo = i + 1;
    }
    return NULL;
}



inline int
bb_is_loop_head(cfg_node_t *bb)
{
    return (bb->loop_role & LOOP_HEAD);
}



inline int
bb_is_loop_tail(cfg_node_t *bb)
{
    return (bb->loop_role & LOOP_TAIL);
}



static void
loop_check(proc_t *proc, int start, int end)
{
    int		is_loop;
    int		bb_id;
    cfg_node_t	*bb;
    cfg_edge_t	*e;
    Queue	worklist;

    bb = &proc->cfg[start];
    if (bb_is_loop_head(bb))
	return;
    if (start == end) {
	proc->cfg[start].loop_role = LOOP_HEAD | LOOP_TAIL;
	return;
    }

    init_queue(&worklist, sizeof(cfg_node_t *));
    enqueue(&worklist, &bb);
    bb->flags = end;
    while (!queue_empty(&worklist)) {
	bb = *((cfg_node_t **) dequeue(&worklist));
	if (bb->id == end) {
	    proc->cfg[start].loop_role = LOOP_HEAD;
	    proc->cfg[end].loop_role = LOOP_TAIL;
	    break;
	} 
	if ((bb->out_n != NULL) && (bb->out_n->dst->flags != end)) {
	    enqueue(&worklist, &bb->out_n->dst);
	    bb->out_n->dst->flags = end;
	}
	if ((bb->out_t != NULL) && (bb->out_t->dst->flags != end)) {
	    enqueue(&worklist, &bb->out_t->dst);
	    bb->out_t->dst->flags = end;
	}
    }
    free_queue(&worklist);
}



static void
identify_loops(proc_t *proc)
{
    int		i;
    cfg_node_t	*bb;
    cfg_edge_t	*e;

    for (i = proc->num_bb - 1; i >= 0; i--) {
	bb = &proc->cfg[i];
	if (bb->out_t == NULL)
	    continue;
	if (bb->out_t->dst->id <= i)
	    loop_check(proc, bb->out_t->dst->id, i);
    }
    for (i = 0; i < proc->num_bb; i++)
	proc->cfg[i].flags = 0;
}



// create a CFG for a proc in three steps:
// - find basic block entries and create basic blocks
// - set basic info for blocks
// - finish up the construction of CFG by connecting blocks with edges
static void
create_cfg(proc_t *proc)
{
    int		*bb_ent, i, bb_id = 0;
    int          num;

    cfg_node_t	*bb;
    addr_t	addr;
    proc_t	*callee;
    
    

    bb_ent = (int *) calloc(proc->num_inst, sizeof(int));
    CHECK_MEM(bb_ent);
    
    // find & create blocks
    scan_blocks(bb_ent, proc);
    proc->cfg = (cfg_node_t *) calloc(proc->num_bb, sizeof(cfg_node_t));
    CHECK_MEM(proc->cfg);

    // set basic info for blocks
    for (i = 0; i < proc->num_inst; i++) {
	if (bb_ent[i]) {
	    // start of a new block
            num = bb_ent[i];
            while(num > 1){
                 // add dummy node
                 bb = &proc->cfg[bb_id];
                 bb->id = bb_id;
                 bb->proc = proc;
                 bb->sa   = proc->code[i - 1].addr; // previous instruction 
                 bb->size = proc->code[i - 1].size;
                 bb->num_inst = 1;
                 bb->code  = &proc->code[i - 1];
                 bb_id++;
                 num--;
            }
	    bb = &proc->cfg[bb_id];
	    bb->id = bb_id;
	    bb->proc = proc;
	    bb->sa = proc->code[i].addr;
	    bb->size = proc->code[i].size;
	    bb->num_inst = 1;
	    bb->code = &proc->code[i];
	    bb_id++;
	} else {
	    // continuation of current block
	    bb->size += proc->code[i].size;
	    bb->num_inst++;
	}
    }
    free(bb_ent);

    // connect block with control transfer edges
    create_cfg_edges(proc);

    // build links from callers (basic blocks) to its callees (procs)
    for (i = 0; i < proc->num_bb; i++) {
	bb = &proc->cfg[i];
	if (bb->type == CTRL_CALL) {
	    addr = bb->code[bb->num_inst-1].target;
	    callee = lookup_proc(addr);
	    if (callee == NULL) {
		fprintf(stderr, "no proc mathces the callee addr: %x\n", addr);
		exit(1);
	    }
	    bb->callee = callee;
	}
    }

    // identify loop heads/tails
    identify_loops(proc);
}



// identify procedures, and construct a control flow graph for each
void
build_cfgs()
{
    int	    *proc_ent, i, proc_id = 0, main_id;
    proc_t  *proc;

    proc_ent = (int *) calloc(prog.num_inst, sizeof(int));
    CHECK_MEM(proc_ent);

    // find & create procs
    main_id = scan_procs(proc_ent);
    prog.procs = (proc_t *) calloc(prog.num_procs, sizeof(proc_t));
    CHECK_MEM(prog.procs);

    // set basic info for procs
    for (i = 0; i < prog.num_inst; i++) {
	if (proc_ent[i]) {
	    // start of a new proc
	    proc = &prog.procs[proc_id];
	    proc->id = proc_id;
	    proc->sa = prog.code[i].addr;
	    proc->size = prog.code[i].size;
	    proc->num_inst = 1;
	    proc->code = &prog.code[i];
	    if (i == main_id)
		prog.main_proc = proc_id;
	    proc_id++;
	} else {
	    // continuation of current proc
	    proc->size += prog.code[i].size;
	    proc->num_inst++;
	}
    }
    free(proc_ent);

    // create CFG for each proc
    for (i = 0; i < prog.num_procs; i++) {
	create_cfg(&prog.procs[i]);
	//dump_cfg(stdout, &prog.procs[i]);
    }
}










void
dump_cfg(FILE *fp, proc_t *proc)
{
    cfg_node_t  *bb;
    int         i;

    fprintf(fp, "\nproc[%d] cfg:\n", proc->id);
    for (i = 0; i < proc->num_bb; i++) {
	bb = &proc->cfg[i];
	fprintf(fp, " %d : %08x : [ ", bb->id, bb->sa);
	if (bb->out_n != NULL)
	    fprintf(fp, " %d",  bb->out_n->dst->id);
	else
	    fprintf(fp, " ");
	fprintf(fp, " , ");
	if (bb->out_t != NULL)
	    fprintf(fp, " %d ",  bb->out_t->dst->id);
	else
	    fprintf(fp, " ");
	fprintf(fp, " ] ");
	if (bb->callee != NULL) {
	    fprintf(fp, " P%d", bb->callee->id);
	}
#if 0
	if (bb_is_loop_head(bb))
	    fprintf(fp, "/");
	if (bb_is_loop_tail(bb))
	    fprintf(fp, "\\");
#endif
	fprintf(fp, "\n");
    }
}


