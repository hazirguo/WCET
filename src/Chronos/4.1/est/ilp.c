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
 * $Id: ilp.c,v 1.4 2006/07/23 09:20:45 lixianfe Exp $
 *
 *******************************************************************************
 *
 * This file generates ILP constraints for estimation, some linear programming
 * solvers have stringent constraints on variable length, thus ILP variable
 * names used in this are kept short, and they are:
 *
 * - b<i>: exec count of the i(th) tcfg node
 *   tcfg_node_term() produces this variable
 *   example: b0
 *
 * - d<i>_<j>: exec count of edge i->j
 *   tcfg_edge_term() produces this variable
 *   example: "d0_1"
 *   it has three variations:
 *   - dc<i>_<j>: edge with cpred
 *   - dm<i>_<j>: edge with mpred
 *   - d<i>_<j>:  edge regardless of pred
 *
 * - d<i>_<j>.<k>: exec count of tcfg edge i->j with hit/miss combination k at j
 *   est_unit_term() produces this variable
 *   like d<i>_<j>, it has three variations ..., and
 *   dc<i>_<j>.<k>, dm<i>_<j>.<k> are estimation units for cond branches (if
 *   bpred is modeled), or d<i>_<j>.<k> for others
 *
 * - c<i>: exec count for i(th) bfg node
 *   bfg_node_term() produces this variable
 *   depending on bfg node type, it may have the following variations:
 *   - loop entry/loop exit: cI<i>/cE<i>
 *   - start/end: cstart/cend
 *
 * - f<i>_<j>N/T: exec count of bfg edge i->j (branch at i TAKEN/NOT TAKEN)
 *   bfg_edge_term() produces this variable
 *   example: "f100_105N"
 *   like d<i>_<j>, it has three variations ...
 *
 * - p<i>_<j>N/T: exec count of btg edge i->j (branch at i TAKEN/NOT TAKEN)
 *   btg_edge_term() produces this variable
 *
 * - hm<i>.<j>: exec count of j(th) hit/miss combination of edge i
 *   hm_term() produces this variable
 *
 * - mp<lp>.<set>: count of mispred exiting 'lp' w/ cache 'set' being affected
 *   lp_set_term() produces this variable
 *
 ******************************************************************************/ 


#include <string.h>
#include <stdlib.h>
#include "tcfg.h"
#include "loops.h"
#include "bpred.h"
#include "cache.h"
#include "pipeline.h"
#include "symexec.h"


extern int PROGRESS_STEP;

#define NOSIGN	    0
#define PLUS	    1
#define MINUS	    2
#define NO_PREFIX   ""
#define NO_SUFFIX   ""
#define BMP	    5
// define IC_NONE as something different from IC_HIT and IC_MISS
#define IC_NONE	    IC_UNCLEAR	


extern tcfg_nlink_t	***bbi_map;
extern int		**cpred_times, **mpred_times, start_time;
extern int		*num_hit_miss;
extern tcfg_edge_t	**tcfg_edges;
extern int		enable_icache;
/* For data cache analysis */
extern int		enable_dcache;
/* For L2 cache analysis */
extern int 		enable_il2cache;
extern int 		enable_ul2cache;
/* For abstract instruction cache analysis */
extern int 		enable_abs_inst_cache;
extern int		bpred_scheme;
extern int		num_tcfg_edges;
extern int		num_tcfg_nodes;
extern tcfg_node_t	**tcfg;
extern bfg_node_t	start_bbb, end_bbb;
extern int		num_bbi_bbb;
extern bfg_node_t	***bfg, **vbbb;
extern int		root_bbb_id, end_bbb_id;
extern int		num_bfg_nodes;
extern btg_edge_t	**btg_out, **btg_in;
extern int		num_tcfg_loops;
extern int		BHT_SIZE;
extern cache_t		cache;
extern char		***hit_miss;
extern int		icache_analy_method;
extern loop_t		**loop_map;
extern loop_t		**loops;
extern loop_t		***mblk_hit_loop;
extern loop_t		***bbi_hm_list;
extern int		*num_mblks;
extern loop_t		***loop_comm_ances;
extern mem_blk_t	**gen;


FILE	*filp, *fusr;
int	total_cons = 0, total_vars = 0;
char	start_str[] = "Sta", end_str[] = "End";

#define NUM_VAR_GRP	256
#define VAR_GRP_SIZE	1024
char	**var_grps[256];
int	curr_grp = 0, curr_idx = 0;
char	str[32], term[32];


extern prog_t prog;


static void
init_var_grps()
{
    var_grps[0] = (char **) calloc(VAR_GRP_SIZE, sizeof(char *));
}



void
add_var(char *var)
{
    int	    len = strlen(var) + 1;

    if (curr_idx == VAR_GRP_SIZE) {
	var_grps[++curr_grp] = (char **) calloc(VAR_GRP_SIZE, sizeof(char *));
	curr_idx = 0;
    }
    var_grps[curr_grp][curr_idx] = (char *) malloc(len);
    strcpy(var_grps[curr_grp][curr_idx], var);
    curr_idx++;
}



void
new_term(FILE *fp)
{
    if (fp == NULL)
	add_var(term);
    else
	fprintf(fp, "%s", term);
}


static void
tcfg_node_str(tcfg_node_t *bbi)
{
    sprintf(str, "%d", bbi->id);
}



// b<i>
static void
tcfg_node_term(FILE *fp, tcfg_node_t *bbi, char *prefix, char *suffix)
{
    tcfg_node_str(bbi);
    sprintf(term, "%sb%s%s", prefix, str, suffix);
    new_term(fp);
}



// b<i>.<hm>
static void
tcfg_node_hm_term(FILE *fp, tcfg_node_t *bbi, int hm, char *prefix, char *suffix)
{
    tcfg_node_str(bbi);
    sprintf(term, "%sb%s.%d%s", prefix, str, hm, suffix);
    new_term(fp);
}



static void
tcfg_edge_str(tcfg_edge_t *e, int bpred)
{
    char    str1[32], str2[32];

    tcfg_node_str(e->src);
    strcpy(str1, str);
    tcfg_node_str(e->dst);
    strcpy(str2, str);
    if (bpred == BP_NONE)
	sprintf(str, "%s_%s", str1, str2);
    else if (bpred == BP_CPRED)
	sprintf(str, "c%s_%s", str1, str2);
    else if (bpred == BP_MPRED)
	sprintf(str, "m%s_%s", str1, str2);
}



// f<i>_<j>, fc<i>_<j>, fm<i>_<j>
static void
tcfg_edge_term(FILE *fp, tcfg_edge_t *e, int bpred, char *prefix, char *suffix)
{
    tcfg_edge_str(e, bpred);
    sprintf(term, "%sd%s%s", prefix, str, suffix);
    new_term(fp);
}



// f<i>_<j>.hm, fc<i>_<j>.hm, fm<i>_<j>.hm
// it's just tcfg_edge_term with hit/miss combination info
static void
est_unit_term(FILE *fp, tcfg_edge_t *e, int bpred, int hm, char *prefix, char *suffix)
{
    tcfg_edge_str(e, bpred);
    if (enable_icache || enable_abs_inst_cache)
	sprintf(term, "%sd%s.%d%s", prefix, str, hm, suffix);
    else
	sprintf(term, "%sd%s%s", prefix, str, suffix);
    new_term(fp);
}



// a version with short variable name to avoid violation of lp_solve's length
// constraints on variables
static void
bfg_node_str(bfg_node_t *bbb)
{
   if (bbb->id == root_bbb_id)
	  sprintf(str, "%s", start_str);
   else if (bbb->id == end_bbb_id)
	  sprintf(str, "%s", end_str);
    else
	  sprintf(str, "%d", bbb->id);
}



// c<i>, or c<i>$<j> for debug purpose
static void
bfg_node_term(FILE *fp, bfg_node_t *bbb, char *prefix, char *suffix)
{
    bfg_node_str(bbb);
    sprintf(term, "%sc%s%s", prefix, str, suffix);
    new_term(fp);
}



static void
bfg_edge_str(bfg_edge_t *e, int bpred)
{
    char    str1[32], str2[32], br;

    bfg_node_str(e->src);
    strcpy(str1, str);
    bfg_node_str(e->dst);
    strcpy(str2, str);
    if (e->branch == TAKEN)
	br = 'T';
    else
	br = 'N';
    if (bpred ==  BP_NONE)
	sprintf(str, "%s_%s%c", str1, str2, br);
    else if (bpred == BP_CPRED)
	sprintf(str, "c%s_%s%c", str1, str2, br);
    else if (bpred == BP_MPRED)
	sprintf(str, "m%s_%s%c", str1, str2, br);
}



// f<i>_<j>T/N, fc<i>_<j>T/N, fm<i>_<j>T/N
static void
bfg_edge_term(FILE *fp, bfg_edge_t *e, int bpred, char *prefix, char *suffix)
{
    bfg_edge_str(e, bpred);
    sprintf(term, "%sf%s%s", prefix, str, suffix);
    new_term(fp);
}



static void
btg_edge_str(btg_edge_t *e)
{
    char    str1[32], str2[32];

    bfg_node_str(e->src);
    strcpy(str1, str);
    bfg_node_str(e->dst);
    strcpy(str2, str);

    if (e->branch == BOTH_BRANCHES)
	sprintf(str, "%s_%s", str1, str2);
    if (e->branch == NOT_TAKEN)
	sprintf(str, "%s_%sN", str1, str2);
    if (e->branch == TAKEN)
	sprintf(str, "%s_%sT", str1, str2);
}



// p<i>_<j>T/N
static void
btg_edge_term(FILE *fp, btg_edge_t *e, char *prefix, char *suffix)
{
    btg_edge_str(e);
    sprintf(term, "%sp%s%s", prefix, str, suffix);
    new_term(fp);
}



static void
hm_str(int edge_id, int hm)
{
    sprintf(str, "%d.%d", edge_id, hm);
}



// hm<i>.<j>
static void
hm_term(FILE *fp, int edge_id, int hm, char *prefix, char *suffix)
{
    hm_str(edge_id, hm);
    sprintf(term, "%shm%s%s", prefix, str, suffix);
    new_term(fp);
}



static void
mpset_str(tcfg_edge_t *edge, int lp_id, int set)
{
    char    tmp_str[32];

    tcfg_edge_str(edge, BP_NONE);
    sprintf(tmp_str, "%s.%d.%d", str, lp_id, set);
    strcpy(str, tmp_str);
}



// mp<lp>.<set>
static void
mpset_term(FILE *fp, tcfg_edge_t *edge, int lp_id, int set, char *prefix, char *suffix)
{
    mpset_str(edge, lp_id, set);
    sprintf(term, "%smp%s%s", prefix, str, suffix);
    new_term(fp);
}



static void
mpunit_str(tcfg_edge_t *edge, int hm, int set)
{
    char    tmp_str[32];

    tcfg_edge_str(edge, BP_NONE);
    sprintf(tmp_str, "%s.%d.%d", str, hm, set);
    strcpy(str, tmp_str);
}



// d<j>_<i>.<hm>.<set>
static void
mpunit_term(FILE *fp, tcfg_edge_t *edge, int hm, int set, char *prefix, char *suffix)
{
    mpunit_str(edge, hm, set);
    sprintf(term, "%sd%s%s", prefix, str, suffix);
    new_term(fp);
}



// - map (proc, bb) to a set of bbi which are instances of (proc, bb)
// - for each bbi, map it to a set of execution paths
static char
map_bb_bbi(int proc, int bb, int first, int sign, int coeff)
{
    int		    i, j;
    tcfg_nlink_t    *nl;
    tcfg_node_t	    *bbi;
    char	    sign_s[3] = "", tmp[3], coeff_s[8] = "", prefix_str[32];

    if( bbi_map[proc][bb] == NULL )
      // vivy: no tcfg node has been created for bb because proc is never called.
      // so we ignore this constraint.
      return 1;
    
    if (sign == PLUS)
	strcpy(tmp, "+ ");
    else
	strcpy(tmp, "- ");
    if (!first)
	strcpy(sign_s, tmp);
	
    if (coeff != 1)
	sprintf(coeff_s, "%d ", coeff);

    // bb => bbi

    for (nl = bbi_map[proc][bb]; nl != NULL; nl = nl->next) {
	bbi = nl->bbi;
	// bb => bbi
	sprintf(prefix_str, "%s%s", sign_s, coeff_s);
	tcfg_node_term(filp, bbi, prefix_str, NO_SUFFIX);
	if (first) {
	    first = 0;
	    strcpy(sign_s, tmp);
	}
    }
    return 0;
}


// vivy
// map (proc, bb) to the k-th instance of (proc, bb)
static char
map_bb_bbi_context(int proc, int bb, int first, int sign, int coeff, int k)
{
    tcfg_nlink_t    *nl;
    tcfg_node_t	    *bbi;
    char	    sign_s[3] = "", tmp[3], coeff_s[8] = "", prefix_str[32];
    int count;

    if( bbi_map[proc][bb] == NULL )
      // vivy: no tcfg node has been created for bb because proc is never called.
      // so we ignore this constraint.
      return 1;

    if (sign == PLUS)
        strcpy(tmp, "+ ");
    else
        strcpy(tmp, "- ");
    if (!first)
        strcpy(sign_s, tmp);

    if (coeff != 1)
        sprintf(coeff_s, "%d ", coeff);

    // skip to the desired context
    for (count = 0, nl = bbi_map[proc][bb]; count < k && nl != NULL; count++, nl = nl->next);

    bbi = nl->bbi;
    // bb => bbi
    sprintf(prefix_str, "%s%s", sign_s, coeff_s);
    tcfg_node_term(filp, bbi, prefix_str, NO_SUFFIX);
    if (first) {
      first = 0;
      strcpy(sign_s, tmp);
    }
    return 0;
}


// trans user provided constraints in terms of basic blocks to bbb
static void
user_cons()
{
    char    line[128], *rhs, *p, tmp[128];
    int	    proc, bb, i, invalid = 0;
    int	    sign, first, coeff;
    char nomatch = 0;

    while (fgets(line, 128, fusr) != NULL) {
	if ((rhs = strchr(line, '<')) == NULL) {
	    if ((rhs = strchr(line, '>')) == NULL) 
		if ((rhs = strchr(line, '=')) == NULL) {
		    fprintf(stderr, "1. invalid constraint:\n%s", line);
		    exit(1);
		}
	}
	first = 1;
	coeff = 1;
	sign = PLUS;
	for (p = line; p != rhs; p++) {
	    if (*p == 'c') {		/* cfg node variable */
		i = sscanf(p+1, "%d.%d", &proc, &bb);
		if (i != 2) {
		    invalid = 1;
		    break;
		}
		// vivy: check if mapping is successful
		nomatch = map_bb_bbi(proc, bb, first, sign, coeff);
		if( nomatch )
		  break;

		sprintf(tmp, "%d.%d", proc, bb);
		p += strlen(tmp);
		first = 0;
		coeff = 1;
	    } else if (*p == '+') {	/* '+' operation */
		sign = PLUS;
	    } else if (*p == '-') {	/* '-' operation */
		sign = MINUS;
	    } else if ((*p >= '1') && (*p <= '9')) {	/* coefficient */
		sscanf(p, "%d", &coeff);
		sprintf(tmp, "%d", coeff);
		p += strlen(tmp) - 1;
	    } else if (*p == ' ' || *p == '\t') {
		continue;
	    } else {			/* unparsable */
		invalid = 1;
		break;
	    }
	}
	if (invalid) { 
	    fprintf(stderr, "2. invalid constraints:\n%s", line);
	    break;
	}
	if( nomatch )
	    continue;

	fprintf(filp, " %s", rhs);
	total_cons++;
    }
}


// vivy
// ASSUMPTION: variable contexts are aligned; e.g. for "X - cY <= 0",
// if X appears in 3 contexts, then so does Y, and
// the i-th node in X's bbi_map is in the same context as the i-th node in Y's bbi map.
static void
user_cons_context()
{
    char    line[128], *rhs, *p, tmp[128];
    int	    proc, bb, i, k, invalid = 0;
    int	    sign, first, coeff;

    char nomatch = 0;

    int num_contexts;
    tcfg_nlink_t *nl;
    int count = 0;

    fprintf(filp,"\n\\ === User constraints (e.g. loop bound) ====\n");
    while (fgets(line, 128, fusr) != NULL) {

      if ((rhs = strchr(line, '<')) == NULL) {
	if ((rhs = strchr(line, '>')) == NULL) {
	  if ((rhs = strchr(line, '=')) == NULL) {
	    fprintf(stderr, "1. invalid constraint:\n%s", line);
	    exit(1);
	  }
	}
      }

      // contexts should be aggregated if the constraint specifies an absolute count
      if( strchr(line, '<') == NULL && strchr(line, '>') == NULL ) {
	first = 1;
	coeff = 1;
	sign = PLUS;
	for (p = line; p != rhs; p++) {

	    if (*p == 'c') {		/* cfg node variable */
		i = sscanf(p+1, "%d.%d", &proc, &bb);
		if (i != 2) {
		    invalid = 1;
		    break;
		}
		// vivy: check if mapping is successful
		nomatch = map_bb_bbi(proc, bb, first, sign, coeff);
		if( nomatch )
		  break;

		sprintf(tmp, "%d.%d", proc, bb);
		p += strlen(tmp);
		first = 0;
		coeff = 1;
	    } else if (*p == '+') {	/* '+' operation */
		sign = PLUS;
	    } else if (*p == '-') {	/* '-' operation */
		sign = MINUS;
	    } else if ((*p >= '1') && (*p <= '9')) {	/* coefficient */
		sscanf(p, "%d", &coeff);
		sprintf(tmp, "%d", coeff);
		p += strlen(tmp) - 1;
	    } else if (*p == ' ' || *p == '\t') {
		continue;
	    } else {			/* unparsable */
		invalid = 1;
		break;
	    }
	}
	if (invalid) { 
	    fprintf(stderr, "2. invalid constraints:\n%s", line);
	    break;
	}
	if( nomatch )
	    continue;

	fprintf(filp, " %s", rhs);
	total_cons++;

	continue;
      }

      // first get the number of contexts for variables involved
      // assume contexts are aligned, so just test for the first appearing variable
      for (p = line; p != rhs; p++) {
	if (*p == 'c') {		/* cfg node variable */
	  i = sscanf(p+1, "%d.%d", &proc, &bb);
	  if (i != 2) {
	    invalid = 1;
	    break;
	  }
	  num_contexts = 0;
	  for (nl = bbi_map[proc][bb]; nl != NULL; nl = nl->next)
	    num_contexts++;

	  break;
	}
      }
      if (invalid) { 
	fprintf(stderr, "2a. invalid constraints:\n%s", line);
	break;
      }

      // generate the corresponding constraint for each context
      for( k = 0; k < num_contexts; k++ ) {
        first = 1;
        coeff = 1;
        sign = PLUS;
        for (p = line; p != rhs; p++) {
            if (*p == 'c') {		/* cfg node variable */
                i = sscanf(p+1, "%d.%d", &proc, &bb);
                if (i != 2) {
                    invalid = 1;
                    break;
                }
		// vivy: check if mapping is successful
                nomatch = map_bb_bbi_context(proc, bb, first, sign, coeff, k);
		if( nomatch )
		  break;

                sprintf(tmp, "%d.%d", proc, bb);
                p += strlen(tmp);
                first = 0;
                coeff = 1;
            } else if (*p == '+') {	/* '+' operation */
                sign = PLUS;
            } else if (*p == '-') {	/* '-' operation */
                sign = MINUS;
            } else if ((*p >= '1') && (*p <= '9')) {	/* coefficient */
                sscanf(p, "%d", &coeff);
                sprintf(tmp, "%d", coeff);
                p += strlen(tmp) - 1;
            } else if (*p == ' ' || *p == '\t') {
                continue;
            } else {			/* unparsable */
                invalid = 1;
                break;
            }
        }
        if (invalid) { 
            fprintf(stderr, "2b. invalid constraints:\n%s", line);
            break;
        }
	if( nomatch )
	    continue;

        fprintf(filp, " %s", rhs);
        total_cons++;
      }
    }
    printf("\n");
}


static void
vstart_cost_term()
{
    fprintf(filp, "%d d%s_0", start_time, start_str);
}



static void
cost_term(int edge_id, int bpred)
{
    int		i, tm, num_hm;
    tcfg_edge_t	*e;
	 extern int l1,l2;

    e = tcfg_edges[edge_id];
    tcfg_edge_str(e, bpred);

    if (enable_icache)
		  num_hm = num_hit_miss[tcfg_edges[edge_id]->dst->id];
    else
		  num_hm = 1;

    for (i = 0; i < num_hm; i++) {
		  if (bpred == BP_MPRED)
				tm = mpred_times[edge_id][i];
		  else
				tm = cpred_times[edge_id][i];
		  if (enable_icache) {
				fprintf(filp, " + %d d%s.%d", tm, str, i);
		  } 
		  else {
				fprintf(filp, " + %d d%s", tm, str);
				break;
		  }
    }
	
	/* For data cache analysis */
	if(enable_dcache)
	{
	  tcfg_node_t* bbi = tcfg_edges[edge_id]->src;  	  
	  fprintf(filp, " + %d d%s", bbi->dcache_delay, str);
     fprintf(filp, " + %d d%s.0", L1_MISS_PENALTY * bbi->n_data_persistence,
		  str);
	  if(enable_ul2cache) 	  
		  fprintf(filp, " + %d d%s.0", L2_MISS_PENALTY * bbi->n_u1_data_persistence,
					 str);
	}
	/* For abstract instruction cache analysis */
	if(enable_abs_inst_cache)
	{
	  tcfg_node_t* bbi = tcfg_edges[edge_id]->src;  	  
	  fprintf(filp, " + %d d%s", bbi->inst_cache_delay, str);
     fprintf(filp, " + %d d%s.0", L1_MISS_PENALTY * bbi->n_persistence, str);
	  if(enable_il2cache)
		  fprintf(filp, " + %d d%s.0", L2_MISS_PENALTY * bbi->n_l2_persistence, str);
	  else if(enable_ul2cache)	  
		  fprintf(filp, " + %d d%s.0", L2_MISS_PENALTY * bbi->n_u1_persistence, str);
	}
}



// tcfg flow constraints for a bbi
// b<i> = sum{ d<i>_<j> } = sum{ d<j>_<i> }
static void
tcfg_node_cons(tcfg_node_t *bbi)
{
    tcfg_edge_t	*e;

    tcfg_node_term(NULL, bbi, NO_PREFIX, NO_SUFFIX);
    // b<i> = sum{ d<i>_<j> }
    if (bbi->out != NULL) {
	tcfg_node_term(filp, bbi, NO_PREFIX, NO_SUFFIX);
	for (e = bbi->out; e != NULL; e = e->next_out) {
	    tcfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	    tcfg_edge_term(NULL, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	}
	fprintf(filp, " = 0\n");
	total_cons++;
    }

    // b<i> = sum{ d<j>_<i> }
    if (bbi->in != NULL) {
	tcfg_node_term(filp, bbi, NO_PREFIX, NO_SUFFIX);
	for (e = bbi->in; e != NULL; e = e->next_in)
	    tcfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	// there is a virtual in edge (executed once) for root
	if (bbi->id == 0)
	    fprintf(filp, " - d%s_0", start_str);
	fprintf(filp, " = 0\n");
	total_cons++;
    } else if (bbi->id == 0) {
	tcfg_node_term(filp, bbi, NO_PREFIX, NO_SUFFIX);
	fprintf(filp, " - d%s_0 = 0\n", start_str);
	total_cons++;
    } else {
	tcfg_node_term(filp, bbi, NO_PREFIX, " = 0\n");
	total_cons++;
    }
}



// tcfg flow constraints
static void
tcfg_cons()
{
    int		i;

    fprintf(filp, "\\ === tcfg constraints ===\n");
	
	 if(enable_abs_inst_cache)
	 {
		 int edge_id;
		 tcfg_edge_t* e;

		 /* for (edge_id = 0; edge_id < num_tcfg_edges; edge_id++) {
		   e = tcfg_edges[edge_id];
			tcfg_edge_str(e, BP_NONE);
			sprintf(str, "%s.0", str);
		   fprintf(filp, "d%s <= 1\n", str);
		   add_var(str);
			tcfg_edge_str(e, BP_NONE);
		   fprintf(filp, "d%s.0 <= d%s\n", str, str);
	    } */
	 }

    fprintf(filp, "d%s_0 = 1\n", start_str);
    total_cons++;
    sprintf(str, "d%s_0", start_str);
    add_var(start_str);
    

    for (i = 0; i < num_tcfg_nodes; i++)
	tcfg_node_cons(tcfg[i]);
    fprintf(filp, "\n");
}



// branch flow graph constraints
// b(i, pi) = sum(d(i, j, pi)) = sum(d(j, i, pi'))
static void
bfg_cons()
{
    int		i, has_root, has_end;
    bfg_node_t	*bbb;
    bfg_edge_t	*e;
    tcfg_node_t	*bbi;
    tcfg_edge_t	*tcfg_e;

    for(i=0; i<num_bfg_nodes; i++) {
	bbb = vbbb[i];
	bfg_node_term(NULL, bbb, NO_PREFIX, NO_SUFFIX);
	// outflows
	bfg_node_term(filp, bbb, NO_PREFIX, NO_SUFFIX);
	has_end = 0;
	for (e = bbb->out; e != NULL; e = e->next_out) {
	    if (e->dst->id == end_bbb_id) {
		has_end = 1;
		continue;
	    }
	    bfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	    bfg_edge_term(NULL, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	}
	fprintf(filp, " <= %d\n", has_end);
	total_cons++;
	bfg_node_term(filp, bbb, NO_PREFIX, NO_SUFFIX);
	has_end = 0;
	for (e = bbb->out; e != NULL; e = e->next_out) {
	    if (e->dst->id == end_bbb_id) {
		continue;
	    }
	    bfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	    bfg_edge_term(NULL, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	}
	fprintf(filp, " >= 0\n");
	total_cons++;

	// inflows
	bfg_node_term(filp, bbb, NO_PREFIX, NO_SUFFIX);
	has_root = 0;
	for (e = bbb->in; e != NULL; e = e->next_in) {
	    if (e->src->id == root_bbb_id) {
		has_root = 1;
		continue;
	    }
	    bfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	}
	fprintf(filp, " <= %d\n", has_root);
	total_cons++;
	bfg_node_term(filp, bbb, NO_PREFIX, NO_SUFFIX);
	has_root = 0;
	for (e = bbb->in; e != NULL; e = e->next_in) {
	    if (e->src->id == root_bbb_id) {
		continue;
	    }
	    bfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	}
	fprintf(filp, " >= 0\n");
	total_cons++;
    }
    fprintf(filp, "\n");
}



// branch pattern transition graph constraints:
// (a) d(i, j, pi) = d(i, j, pi, c) + d(i, j, pi, m)
// (b) b(i, pi) = sum(p(k, i, pi, x)) = sum(p(i, k, pi, x))
// (c) d(i, j, pi) = sum(p(i, k, pi, x)) (x is the outomce of d(i,j))
// (d) d(i, j, pi, m) <= d(i, j, pi)
//     d(i, j, pi, m) <= sum(p(k, i, pi, ~x))
static void
btg_cons()
{
    int		i, pi, has_root, has_end;
    bfg_node_t	*x, *y;
    bfg_edge_t	*e;
    btg_edge_t	*p;

    // (a)
    fprintf(filp, "\n");
    for (i = 0; i < num_bfg_nodes; i++) {
	x = vbbb[i];
	for (e = x->out; e != NULL; e = e->next_out) {
	    if (e->dst->id == end_bbb_id)
		continue;
	    bfg_edge_term(NULL, e, BP_CPRED, NO_PREFIX, NO_SUFFIX);
	    bfg_edge_term(NULL, e, BP_MPRED, NO_PREFIX, NO_SUFFIX);
	    bfg_edge_term(filp, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	    bfg_edge_term(filp, e, BP_CPRED, " - ", NO_SUFFIX);
	    bfg_edge_term(filp, e, BP_MPRED, " - ", " = 0\n");
	    total_cons++;
	}
    }

    // (b)
    fprintf(filp, "\n");

    // (b.2) (branch blocks)
    for (i = 0; i < num_bfg_nodes; i++) {
	x = vbbb[i];
	// in
	bfg_node_term(filp, x, NO_PREFIX, NO_SUFFIX);
	has_root = 0;
	for (p = btg_in[i]; p != NULL; p = p->next_in) {
	    if (p->src->id == root_bbb_id) {
		has_root = 1;
		continue;
	    }
	    btg_edge_term(filp, p, " - ", NO_SUFFIX);
	}
	fprintf(filp, " <= %d\n", has_root);
	total_cons++;
	bfg_node_term(filp, x, NO_PREFIX, NO_SUFFIX);
	for (p = btg_in[i]; p != NULL; p = p->next_in) {
	    if (p->src->id == root_bbb_id) {
		continue;
	    }
	    btg_edge_term(filp, p, " - ", NO_SUFFIX);
	}
	fprintf(filp, " >= 0\n");
	total_cons++;

    }

    // (c)
    fprintf(filp, "\n");
    for (i = 0; i < num_bfg_nodes; i++) {
	x = vbbb[i];
	for (e = x->out; e != NULL; e = e->next_out) {
	    if (e->dst->id == end_bbb_id)
		continue;
	    bfg_edge_term(filp, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	    has_end = 0;
	    for (p = btg_out[i]; p != NULL; p = p->next_out) {
		if (e->branch != p->branch)
		    continue;
		if (p->dst->id == end_bbb_id) {
		    has_end = 1;
		    continue;
		}
		btg_edge_term(filp, p, " - ", NO_SUFFIX);
	    }
	    fprintf(filp, " <= %d\n", has_end);
	    total_cons++;
	    bfg_edge_term(filp, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	    for (p = btg_out[i]; p != NULL; p = p->next_out) {
		if (e->branch != p->branch)
		    continue;
		if (p->dst->id == end_bbb_id) {
		    continue;
		}
		btg_edge_term(filp, p, " - ", NO_SUFFIX);
	    }
	    fprintf(filp, " >= 0\n");
	    total_cons++;
	}
    }

    // (d)
    fprintf(filp, "\n");
    for (i = 0; i < num_bfg_nodes; i++) {
	x = vbbb[i];
	for (e = x->out; e != NULL; e = e->next_out) {
	    // in
	    if (e->dst->id == end_bbb_id)
		continue;
	    bfg_edge_term(filp, e, BP_MPRED, NO_PREFIX, NO_SUFFIX);
	    has_root == 0;
	    for (p = btg_in[i]; p != NULL; p = p->next_in) {
		if (e->branch == p->branch)
		    continue;
		if (p->src->id == root_bbb_id) {
		    has_root = 1;
		    continue;
		}
		btg_edge_term(filp, p, " - ", NO_SUFFIX);
	    }
	    fprintf(filp, " <= %d\n", has_root);
	    total_cons++;
	    // out
#if 0
	    bfg_edge_term(filp, e, BP_MPRED, NO_PREFIX, NO_SUFFIX);
	    bfg_edge_term(filp, e, BP_NONE, " - ", " <= 0\n");
	    total_cons++;
#endif
	}
    }
    fprintf(filp, "\n");
}



// (a) b(i) = sum(b(i, pi))
// (b) (1) d(i, j) = d(i, j, c) + d(i, j, m)
//     (2) d(i, j) = sum(d(i, k, pi));  k is the 1st branch from along i->j
//     (3) d(i, j, m) = sum(d(i, k, pi, m)); 
static void
tcfg_bfg_cons()
{
    int		i, j, pi, branch, bp[2] = {BP_NONE, BP_MPRED};
    tcfg_node_t	*bbi;
    tcfg_edge_t	*e;
    bfg_node_t	*x, *y;
    bfg_edge_t	*bbb_e;

    //(a)
    for (i = 0; i < num_tcfg_nodes; i++) {
	if (bbi_type(tcfg[i]) != CTRL_COND)
	    continue;
	tcfg_node_term(filp, tcfg[i], NO_PREFIX, NO_SUFFIX);
	for (pi = 0; pi < BHT_SIZE; pi++) {
	    if (bfg[i][pi] != NULL)
		bfg_node_term(filp, bfg[i][pi], " - ", NO_SUFFIX);
	}
	fprintf(filp, " = 0\n");
	total_cons++;
    }

    //(b) 
    //(1)
    for (i = 0; i < num_tcfg_nodes; i++) {
	bbi = tcfg[i];
	if (bbi_type(bbi) != CTRL_COND)
	    continue;
	for (e = bbi->out; e != NULL; e = e->next_out) {
	    j = e->dst->id;
	    tcfg_edge_term(filp, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	    tcfg_edge_term(filp, e, BP_CPRED, " - ", NO_SUFFIX);
	    tcfg_edge_term(filp, e, BP_MPRED, " - ", " = 0\n");
	    total_cons++;
	    tcfg_edge_term(NULL, e, BP_CPRED, NO_PREFIX, NO_SUFFIX);
	    tcfg_edge_term(NULL, e, BP_MPRED, NO_PREFIX, NO_SUFFIX);
	}
    }

    for (i = 0; i < num_tcfg_nodes; i++) {
	bbi = tcfg[i];
	if (bbi_type(bbi) != CTRL_COND)
	    continue;
	// (2) & (3)
	for (e = bbi->out; e != NULL; e = e->next_out) {
	    for (j = 0; j < 2; j++) {
		tcfg_edge_term(filp, e, bp[j], NO_PREFIX, NO_SUFFIX);
		branch = e->branch;
		for (pi = 0; pi < BHT_SIZE; pi++) {
		    if ((x = bfg[i][pi]) == NULL) 
			continue;
		    if (x->out->branch == branch)
			bbb_e = x->out;
		    else
			bbb_e = x->out->next_out;
		    if (bbb_e->dst->id == end_bbb_id) {
			break;
		    }
		    bfg_edge_term(filp, bbb_e, bp[j], " - ", NO_SUFFIX);
		}
		if (pi < BHT_SIZE)
		    fprintf(filp, " <= 1\n");
		else
		    fprintf(filp, " = 0\n");
		total_cons++;
	    }
	}
    }
    fprintf(filp, "\n");
}

static void tcfg_estunit_cons_ps()
{
	 int edge_id, hm, num;
	 tcfg_edge_t* e;

    fprintf(filp, "\n\\ === tcfg_estunit_cons_ps ===\n");
    
	 for (edge_id = 0; edge_id < num_tcfg_edges; edge_id++) {
		e = tcfg_edges[edge_id];
		tcfg_edge_term(filp, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	   est_unit_term(filp, e, BP_NONE, 0, " - ", NO_SUFFIX);
		fprintf(filp, " >= 0\n");
		total_cons++;
	   est_unit_term(filp, e, BP_NONE, 0, NO_PREFIX , NO_SUFFIX);
		fprintf(filp, " <= 1\n");
		total_cons++;
	 }	  
}

/*
HBK: scope-aware data cache cost
*/
extern int      enable_scp_dcache;      
extern int      lp_coldms[36];
extern int      l1;

static void tcfg_dinst_str(int d_inst_id, tcfg_node_t *bbi) {
    sprintf(str,"b%d.d%d", bbi->id, d_inst_id);
}
static void scp_dcache_cost() {
    int         i,j;
    loop_t      *lp;
    tcfg_node_t *bbi;
    cfg_node_t  *bb;
    tcfg_edge_t *edge;
    dat_inst_t  *d_inst;
    int         coldms;
    //print cold miss penalty each time the program enters a loop
    for (i=1; i<num_tcfg_loops; i++) {
        lp      = loops[i];
        coldms  += lp_coldms[i];
    }
    fprintf(filp," + %d d%s_0",coldms*l1, start_str);
    //print dcache miss penalty for each data instruction
    for (i=0; i<num_tcfg_nodes; i++) {
        bbi     = tcfg[i];
        bb      = bbi->bb;
        for (j=0; j<bb->num_d_inst; j++) {
            d_inst      = ((dat_inst_t*)bb->d_instlist)+j;
            tcfg_dinst_str(j,bbi);
            fprintf(filp," + %d %s",l1, str);
        }
    }
}
/*
HBK: scope aware dcache constraints
*/
static void scp_dcache_cons() {
    int i,j;
    tcfg_node_t *bbi;
    cfg_node_t  *bb;
    dat_inst_t  *d_inst;
    char        bb_str[10];

    fprintf(filp,"\n\\ ==== scope-aware dcache miss constraints ===\n");
    for (i=0; i<num_tcfg_nodes; i++) {
        bbi     = tcfg[i];
        bb      = bbi->bb;
        tcfg_node_str(bbi);
        strcpy(bb_str,str);
        for (j=0; j<bb->num_d_inst; j++) {
            d_inst      = ((dat_inst_t*)bb->d_instlist)+j;
            tcfg_dinst_str(j,bbi);
            fprintf(filp,"%s - b%s <= 0\n", str,bb_str);
            fprintf(filp,"%s <= %d\n", str,d_inst->max_miss);
        }
    }
}

static void
tcfg_estunit_cons()
{
    int		edge_id, hm, num;
    tcfg_edge_t	*e;

    fprintf(filp, "\n\\ === tcfg_estunit_cons ===\n");
    for (edge_id = 0; edge_id < num_tcfg_edges; edge_id++) {
	e = tcfg_edges[edge_id];
	tcfg_edge_term(filp, e, BP_NONE, NO_PREFIX, NO_SUFFIX);
	num = num_hit_miss[tcfg_edges[edge_id]->dst->id];
	for (hm = 0; hm < num; hm++)
	    est_unit_term(filp, e, BP_NONE, hm, " - ", NO_SUFFIX);
	fprintf(filp, " = 0\n");
	total_cons++;
	if ((bpred_scheme == NO_BPRED) || !cond_bbi(e->src))
	    continue;

	for (hm = 0; hm < num; hm++) {
	    est_unit_term(filp, e, BP_NONE, hm, NO_PREFIX, NO_SUFFIX);
	    est_unit_term(filp, e, BP_CPRED, hm, " - ", NO_SUFFIX);
	    est_unit_term(filp, e, BP_MPRED, hm, " - ", " = 0\n");
	    total_cons++;
	}

	// CPRED
	e = tcfg_edges[edge_id];
	tcfg_edge_term(filp, e, BP_CPRED, NO_PREFIX, NO_SUFFIX);
	for (hm = 0; hm < num; hm++)
	    est_unit_term(filp, e, BP_CPRED, hm, " - ", NO_SUFFIX);
	fprintf(filp, " = 0\n");
	total_cons++;
	// MPRED
	e = tcfg_edges[edge_id];
	tcfg_edge_term(filp, e, BP_MPRED, NO_PREFIX, NO_SUFFIX);
	for (hm = 0; hm < num; hm++)
	    est_unit_term(filp, e, BP_MPRED, hm, " - ", NO_SUFFIX);
	fprintf(filp, " = 0\n");
	total_cons++;
    }
}



static void
bpred_misses()
{
    int		    i;
    tcfg_node_t	    *bbi;
    tcfg_edge_t	    *e;

    fprintf(filp, "bm");
    add_var("bm");
    for (i = 0; i < num_tcfg_nodes; i++) {
	bbi = tcfg[i];
	for (e = bbi->in; e != NULL; e = e->next_in) {
	    if (cond_bbi(e->src))
		tcfg_edge_term(filp, e, BP_MPRED, " - ", NO_SUFFIX);
	}
    }
    fprintf(filp, " = 0\n");
    total_cons++;
}



static void
mpcache_misses();


static void
cache_misses()
{
    int		i, j, hm, num, cold_misses = 0, ts0, ts1;
    char	prefix_str[32];
    loop_t	*lp1, *lp2, *lp3;
    tcfg_node_t	*lp_head;
    tcfg_edge_t	*e;

    fprintf(filp, "cm");
    add_var("cm");

    for (i = 0; i < num_tcfg_nodes; i++) {
	ts0 = TAGSET(tcfg[i]->bb->sa);
	for (hm = 0; hm < num_hit_miss[i]; hm++) {
	    lp1 = bbi_hm_list[i][hm];
	    for (e = tcfg[i]->in; e != NULL; e = e->next_in) {
		num = 0;
		for (j = 0; j < num_mblks[i]; j++) {
		    lp2 = mblk_hit_loop[i][j];
		    if ((lp1 == NULL) || (lp2 == NULL))
			num++; 
		    else if (lp1 != lp2) {
			lp3 = loop_comm_ances[lp1->id][lp2->id];
			if (lp3 == lp1)
			    num++;
		    }
		}
		if (num > 1) {
		    sprintf(prefix_str, " - %d ", num);
		    est_unit_term(filp, e, BP_NONE, hm, prefix_str, NO_SUFFIX);
		} else if (num == 1) {
		    sprintf(prefix_str, " - ", num);
		    est_unit_term(filp, e, BP_NONE, hm, prefix_str, NO_SUFFIX);
		}
	    }
	}
    }
    if (bpred_scheme != BP_NONE)
	mpcache_misses();
    fprintf(filp, " = %d\n", num_mblks[0]);
    total_cons++;
}



// (a) bbi = sum(bbi, hm)
// (b) (bbi, hm) = sum(in_edge, hm)
// (c) (bbi, hm) < sum(loop_head, loop_entry_edge) - sum(bbi, hm'), hm' > hm
// (d) (back_edge, hm) = 0 if hm is on the outside of back_edge and hm is not
//     the inner-most loop level
static void
cache_cons()
{
    int		i, j, hm, hm1;
    tcfg_node_t	*bbi, *lp_head;
    tcfg_edge_t	*e;
    loop_t	*lp;

    fprintf(filp, "\n\\ === cache_cons ===\n");
    // (a)
    tcfg_node_hm_term(filp, tcfg[0], 0, NO_PREFIX, " <= 1\n");
    tcfg_node_hm_term(NULL, tcfg[0], 0, NO_PREFIX, NO_SUFFIX);
    for (i = 1; i < num_tcfg_nodes; i++) {
	tcfg_node_term(filp, tcfg[i], NO_PREFIX, NO_SUFFIX);
	for (hm = 0; hm < num_hit_miss[i]; hm++) {
	    tcfg_node_hm_term(filp, tcfg[i], hm, " - ", NO_SUFFIX);
	    tcfg_node_hm_term(NULL, tcfg[i], hm, NO_PREFIX, NO_SUFFIX);
	}
	fprintf(filp, " = 0\n");
	total_cons++;
    }
    fprintf(filp, "\n");
    // (b)
    tcfg_node_hm_term(filp, tcfg[0], 0, NO_PREFIX, " <= 1\n");
    total_cons++;
    tcfg_node_hm_term(NULL, tcfg[0], 0, NO_PREFIX, NO_SUFFIX);
    for (i = 1; i < num_tcfg_nodes; i++) {
	bbi = tcfg[i];
	if (bbi->in == NULL)
	    continue;
	for (hm = 0; hm < num_hit_miss[i]; hm++) {
	    tcfg_node_hm_term(filp, bbi, hm, NO_PREFIX, NO_SUFFIX);
	    for (e = bbi->in; e != NULL; e = e->next_in)
		est_unit_term(filp, e, BP_NONE, hm, " - ", NO_SUFFIX);
	    fprintf(filp, " = 0\n");
	    total_cons++;
	    tcfg_node_hm_term(NULL, bbi, hm, NO_PREFIX, NO_SUFFIX);
	}
    }
    fprintf(filp, "\n");
    // (c)
    for (i = 0; i < num_tcfg_nodes; i++) {
	bbi = tcfg[i];
	// (c.1) processing the outmost loop level in hm_list
	hm = num_hit_miss[i] - 1;
	if (hm == 0) {
	    tcfg_node_hm_term(filp, bbi, hm, NO_PREFIX, NO_SUFFIX);
	    tcfg_node_term(filp, bbi, " - ", " = 0\n");
	    total_cons++;
	    continue;
	} 
	lp = bbi_hm_list[bbi->id][hm];
	if ((lp == NULL) || (lp->head == NULL)) {
	    tcfg_node_hm_term(filp, bbi, hm, NO_PREFIX, " <= 1\n");
	    total_cons++;
	} else {
	    tcfg_node_hm_term(filp, bbi, hm, NO_PREFIX, NO_SUFFIX);
	    tcfg_node_term(filp, lp->head, " - ", " <= 0\n");
	    total_cons++;
	}
	// (c.2) processing other loop levels
	for (hm--; hm > 0 ; hm--) {
	    tcfg_node_hm_term(filp, bbi, hm, NO_PREFIX, NO_SUFFIX);
	    lp = bbi_hm_list[bbi->id][hm-1];
	    lp_head = lp->head;
	    for (e = lp_head->in; e != NULL; e = e->next_in) {
		if (loop_map[e->src->id] == lp->parent)
		    tcfg_edge_term(filp, e, BP_NONE, " - ", NO_SUFFIX);
	    }
	    for (hm1 = hm+1; hm1 < num_hit_miss[i]; hm1++) {
		tcfg_node_hm_term(filp, bbi, hm1, " + ", NO_SUFFIX);
	    }
	    fprintf(filp, " <= 0\n");
	    total_cons++;
	}
    }

    // (d)
    fprintf(filp, "\n");
    for (i = 1; i < num_tcfg_loops; i++) {
	lp_head = loops[i]->head;
	for (e = lp_head->in; e != NULL; e = e->next_in) {
	    if (loop_map[e->src->id] != loops[i])
		continue;
	    for (hm = 1; hm < num_hit_miss[lp_head->id]; hm++) {
		est_unit_term(filp, e, BP_NONE, hm, NO_PREFIX, " = 0\n");
		total_cons++;
	    }
	}
    }
}



extern tcfg_elink_t ***mp_affected_sets;
extern int	    ***mp_times;

static void
edge_mpset_cons(int lp_id, tcfg_edge_t *edge)
{
    int		    set, affected = 0;
    tcfg_elink_t    **mp_afs, *elink;

    mp_afs = mp_affected_sets[lp_id];
    for (set = 0; set < cache.ns; set++) {
	for (elink = mp_afs[set]; elink != NULL; elink = elink->next) {
	    if (elink->edge == edge)
		break;
	}
	if (elink != NULL) {
	    if (affected == 0) {
		affected = 1;
		mpset_term(filp, edge, lp_id, set, NO_PREFIX, NO_SUFFIX);
	    } else {
		mpset_term(filp, edge, lp_id, set, " + ", NO_SUFFIX);
	    }
	    mpset_term(NULL, edge, lp_id, set, NO_PREFIX, NO_SUFFIX);
	}
    }
    if (affected) {
	tcfg_edge_term(filp, edge, BP_MPRED, " - ", " <= 0\n");
	total_cons++;
    }
}



static void
find_cond_exit(int lp_id, tcfg_edge_t *edge)
{
    tcfg_elink_t	*elink;

    if (cond_bbi(edge->src)) {
	if (edge->src->out == edge)
	    edge_mpset_cons(lp_id, edge->next_out);
	else
	    edge_mpset_cons(lp_id, edge->src->out);
    } else {
	for (edge = edge->src->in; edge != NULL; edge = edge->next_in)
	    find_cond_exit(lp_id, edge);
    }
}



// sum(mp<i>_<j>.<lp>.<set>) <= sum(dm<i>_<j>),
// where mp<i>_<j>.<lp>.<set> is #cache misses of set due to mispred along
// loop-exit edge <i>_<j>
static void
mpset_cons()
{
    int		    lp_id;
    tcfg_elink_t    *elink;

    fprintf(filp, "\n\\ === mpset_cons ===\n");
    for (lp_id = 1; lp_id < num_tcfg_loops; lp_id++) {
	for (elink = loops[lp_id]->exits; elink != NULL; elink = elink->next) {
	    find_cond_exit(lp_id, elink->edge);
	}
    }
}



static void
set_mpunit_cons(int bbi_id, int hm, int lp_id)
{
    int		    set;
    tcfg_elink_t    *elink;
    tcfg_edge_t	    *edge;

    for (set = 0; set < cache.ns; set++) {
	elink = mp_affected_sets[lp_id][set];
	if (elink == NULL)
	    continue;
	edge = tcfg[bbi_id]->in;
	mpunit_term(filp, edge, hm, set, NO_PREFIX, NO_SUFFIX);
	mpunit_term(NULL, edge, hm, set, NO_PREFIX, NO_SUFFIX);
	for (edge = edge->next_in; edge != NULL; edge = edge->next_in) {
	    mpunit_term(filp, edge, hm, set, " + ", NO_SUFFIX);
	    mpunit_term(NULL, edge, hm, set, NO_PREFIX, NO_SUFFIX);
	}
	for (; elink != NULL; elink = elink->next) {
	    mpset_term(filp, elink->edge, lp_id, set, " - ", " <= 0\n");
	    total_cons++;
	}
    }
}



static void
estunit_mpunit_cons(int bbi_id, int hm, int lp_id)
{
    int		    set, first;
    tcfg_elink_t    *elink;
    tcfg_edge_t	    *edge;

    for (edge = tcfg[bbi_id]->in; edge != NULL; edge = edge->next_in) {
	first = 1;
	for (set = 0; set < cache.ns; set++) {
	    elink = mp_affected_sets[lp_id][set];
	    if (elink == NULL)
		continue;
	    if (first) {
		mpunit_term(filp, edge, hm, set, NO_PREFIX, NO_SUFFIX);
		first = 0;
	    } else {
		mpunit_term(filp, edge, hm, set, " + ", NO_SUFFIX);
	    }
	}
	if (first == 0) {
	    est_unit_term(filp, edge, BP_NONE, hm, " - ", " <= 0\n");
	    total_cons++;
	}
    }
}



static void
mpunit_cons()
{
    int		set, i, num_hm, hm, first;
    tcfg_edge_t	*edge;
    loop_t	*hm_lp;

    fprintf(filp, "\n\\ === mpunit_cons ===\n");
    for (i = 1; i < num_tcfg_nodes; i++) {
	if ( loop_map[i] == 0)
	    continue;
	num_hm = num_hit_miss[i];
	for (hm = 0; hm < num_hm; hm++) {
	    hm_lp = bbi_hm_list[i][hm];
	    if ((hm_lp == NULL) || (hm_lp->id == 0))
		continue;
	    set_mpunit_cons(i, hm, hm_lp->id);
	    estunit_mpunit_cons(i, hm, hm_lp->id);
	}
    }
}



// mispred related cache constraints for categorization-based cache analysis
static void
mp_cache_cons()
{
    mpset_cons();
    mpunit_cons();
}



static void
estunit_mpcost_func(int bbi_id, int hm, int lp_id)
{
    int		    set, penalty;
    char	    prefix_str[16]; 
    tcfg_edge_t	    *edge;
    tcfg_elink_t    *elink;

    for (edge = tcfg[bbi_id]->in; edge != NULL; edge = edge->next_in) {
	for (set = 0; set < cache.ns; set++) {
	    elink = mp_affected_sets[lp_id][set];
	    if (elink == NULL)
		continue;
	    penalty = mp_times[edge->id][hm][set] -
		cpred_times[edge->id][hm];
	    sprintf(prefix_str, " + %d ", penalty);
	    mpunit_term(filp, edge, hm, set, prefix_str, NO_SUFFIX);
	}
    }
}



static void
mpcost_func()
{
    int		set, i, num_hm, hm, penalty;
    tcfg_edge_t	*edge;
    loop_t	*hm_lp;

    for (i = 1; i < num_tcfg_nodes; i++) {
	if (loop_map[i] == 0)
	    continue;
	num_hm = num_hit_miss[i];
	for (hm = 0; hm < num_hm; hm++) {
	    hm_lp = bbi_hm_list[i][hm];
	    if ((hm_lp == NULL) || (hm_lp->id == 0))
		continue;
	    estunit_mpcost_func(i, hm, hm_lp->id);
	}
    }
}



static void
mpunit_cache_misses(int bbi_id, int hm, int lp_id)
{
    int		    set, i, num_tags;
    tcfg_elink_t    *elink;
    tcfg_edge_t	    *edge;
    char	    prefix_str[8];

    for (edge = tcfg[bbi_id]->in; edge != NULL; edge = edge->next_in) {
	for (set = 0; set < cache.ns; set++) {
	    elink = mp_affected_sets[lp_id][set];
	    if (elink == NULL)
		continue;
	    num_tags = 0;
	    for (i = 0; i < num_mblks[bbi_id]; i++) {
		if (gen[bbi_id][i].set == set)
		    num_tags++;
	    }
	    if (num_tags == 1)
		sprintf(prefix_str, " - ");
	    else
		sprintf(prefix_str, " - %d ", num_tags);
	    mpunit_term(filp, edge, hm, set, prefix_str, NO_SUFFIX);
	}
    }
}



static void
mpcache_misses()
{
    int		set, i, num_hm, hm;
    tcfg_edge_t	*edge;
    loop_t	*hm_lp;

    for (i = 1; i < num_tcfg_nodes; i++) {
	if (loop_map[i] == 0)
	    continue;
	num_hm = num_hit_miss[i];
	for (hm = 0; hm < num_hm; hm++) {
	    hm_lp = bbi_hm_list[i][hm];
	    if ((hm_lp == NULL) || (hm_lp->id == 0))
		continue;
	    mpunit_cache_misses(i, hm, hm_lp->id);
	}
    }
}


#if 0
static void
cost_func()
{
    int		i;

    fprintf(filp, "%d b%d", tcfg[0]->bb->num_inst, 0);
    for (i = 1; i < num_tcfg_nodes; i++)
	fprintf(filp, " + %d b%d", tcfg[i]->bb->num_inst, i);
}
#endif



// output cost function
static void
cost_func()
{
    int		edge_id = 0;

    vstart_cost_term();
    for (edge_id = 0; edge_id < num_tcfg_edges; edge_id++) {
        if ((bpred_scheme == NO_BPRED) || !cond_bbi(tcfg_edges[edge_id]->src))
            cost_term(edge_id, BP_NONE);
        else {
            cost_term(edge_id, BP_CPRED);
            cost_term(edge_id, BP_MPRED);
        }
    }
    if ((bpred_scheme != NO_BPRED) && enable_icache)
        mpcost_func();

    /*HBK: scope-aware data cache cost*/
    if (enable_scp_dcache) {
        scp_dcache_cost();
    }
    fprintf(filp, "\n");
}



static void
write_vars()
{
    int	        gid;
    int         i,j;
    tcfg_node_t *bbi;
    cfg_node_t  *bb;
    dat_inst_t  *d_inst;

    fprintf(filp, "\nGenerals\n\n");

    for (gid = 0; gid < curr_grp; gid++) {
	for (i = 0; i < VAR_GRP_SIZE; i++)
	    fprintf(filp, "%s\n", var_grps[gid][i]);
    }
    for (i = 0; i < curr_idx; i++)
	fprintf(filp, "%s\n", var_grps[curr_grp][i]);

    total_vars = curr_grp * VAR_GRP_SIZE + curr_idx;

    for (i=0; i<num_tcfg_nodes; i++) {
        bbi     = tcfg[i];
        bb      = bbi->bb;
        for (j=0; j<bb->num_d_inst; j++) {
            d_inst      = ((dat_inst_t*)bb->d_instlist)+j;
            tcfg_dinst_str(j,bbi);
            fprintf(filp,"%s\n",str);
            total_vars++;
        }
    }
    fprintf(filp, "\nEnd\n");
    fprintf(filp, "\\ total_cons: %d \ttotal_vars: %d\n", total_cons, total_vars);
}


/* vivy */
#include "infeasible.h"

extern char enable_infeas;


/*
 * vivy: infeasible path constraints
 * Prints terms that nullify the conflict in currently processed context.
 * ctxt gives the position in the bbi_map linked list corresponding to the branch tcfg node in current context.
 * We assume that contexts are ordered, so the nullifier nodes corresponding to this context
 * also appear at position ctxt in their own bbi_map.
 */
int infeas_nullifiers( assign_t **nullifier_list, int num_nullifiers, int pid, int ctxt ) {
    int i, count;

    for( i = 0; i < num_nullifiers; i++ ) {
        tcfg_nlink_t *ndlink = bbi_map[pid][nullifier_list[i]->bb->id];

        // advance to the same context
        count = 0;
        while( count < ctxt && ndlink != NULL ) {
            ndlink = ndlink->next;
            count++;
        }
        if( ndlink == NULL )
            printf( "Unbalanced context: required %d; found %d for nullifier(%d::%d)\n",
                    ctxt, count-1, pid, nullifier_list[i]->bb->id ), exit(1);

        fprintf( filp, " - " );
        tcfg_node_term( filp, ndlink->bbi, NO_PREFIX, NO_SUFFIX );
    }
    return 0;
}


/*
 * vivy: infeasible path constraints
 * Prints the term to constrain execution count.
 * Absolute counts are printed on RHS; loop bound constraints on LHS.
 */
//int infeas_blockcount( inf_node_t *ib, int ctxt ) {
int infeas_blockcount( tcfg_node_t *ib, int ctxt ) {
    int count;
    tcfg_nlink_t *ndlink;

    int lpid = ib->loop_id;
    inf_loop_t *lp;

    if( lpid == -1 ) {
        // absolute count, or no information
        if( ib->exec_count > -1 )   // from DFA
            fprintf( filp, " <= %d\n", ib->exec_count );
        else
            fprintf( filp, " <= 1\n" );
        return 0;
    }

    // loop bound
    lp = &(inf_loops[lpid]);

    // find the corresponding tcfg
    ndlink = bbi_map[lp->pid][lp->entry];

    // advance to the same context
    count = 0;
    while( count < ctxt && ndlink != NULL ) {
        ndlink = ndlink->next;
        count++;
    }
    if( ndlink == NULL )
        printf( "Unbalanced context: required %d for node(%d::%d); found %d for entry(%d::%d)\n",
                ctxt, ib->bb->proc->id, ib->bb->id, count-1, lp->pid, lp->entry ), exit(1);

    fprintf( filp, " - %d ", lp->bound );
    tcfg_node_term( filp, ndlink->bbi, NO_PREFIX, NO_SUFFIX );
    fprintf( filp, " <= 0\n" );

    return 0;
}


/*
 * vivy: infeasible path constraints
 */
int infeas_cons() {
    int i, j, k;

    inf_proc_t *ip;
    int pid;

    fprintf( filp, "\n\\ === infeasible path constraints ===\n" );

    for( i = 0; i < prog.num_procs; i++ ) {

        if( !include_proc[i] )
            continue;

        ip = &(inf_procs[i]);
        pid = ip->proc->id;

        for( j = 0; j < ip->num_bb; j++ ) {
            inf_node_t *ib = &(ip->inf_cfg[j]);
            branch_t   *br = ib->branch;

            if( br == NULL )
                continue;

            // BA conflicts
            for( k = 0; k < br->num_BA_conflicts; k++ ) {
                BA_conflict_t *cf = &(br->BA_conflict_list[k]);

                int cf_src = cf->conflict_src->bb->id;
                int br_dst = (cf->conflict_dir == JUMP) ? br->bb->out_t->dst->id : br->bb->out_n->dst->id;

                // corresponding tcfg nodes
                tcfg_nlink_t *cflink = bbi_map[pid][cf_src];
                tcfg_nlink_t *brlink = bbi_map[pid][br->bb->id];
                int ctxt = 0;

                while( cflink != NULL && brlink != NULL ) {

                    // find edge corresponding to branch direction in conflict
                    tcfg_edge_t *br_out = brlink->bbi->out;
                    while( br_out != NULL && bbi_bid(br_out->dst) != br_dst )
                        br_out = br_out->next_out;
                    if( br_out == NULL )
                        printf( "Edge not found.\n" ), exit(1);

                    // print constraint
                    tcfg_node_term( filp, cflink->bbi, NO_PREFIX, NO_SUFFIX );
                    fprintf( filp, " + " );
                    tcfg_edge_term( filp, br_out, BP_NONE, NO_PREFIX, NO_SUFFIX );
                    infeas_nullifiers( cf->nullifier_list, cf->num_nullifiers, pid, ctxt );
                    //infeas_blockcount( ib, ctxt );
                    infeas_blockcount( cflink->bbi, ctxt );

                    // next context
                    cflink = cflink->next;
                    brlink = brlink->next;
                    ctxt++;
                }
            }

            // BB conflicts
            for( k = 0; k < br->num_BB_conflicts; k++ ) {
                BB_conflict_t *cf = &(br->BB_conflict_list[k]);
                cfg_node_t *cfbb = cf->conflict_src->bb;

                int cf_src = cfbb->id;
                int cf_dst = (cf->conflict_dir == JJ || cf->conflict_dir == JF) ? cfbb->out_t->dst->id : cfbb->out_n->dst->id;
                int br_dst = (cf->conflict_dir == JJ || cf->conflict_dir == FJ) ? br->bb->out_t->dst->id : br->bb->out_n->dst->id;

                // corresponding tcfg nodes
                tcfg_nlink_t *cflink = bbi_map[pid][cf_src];
                tcfg_nlink_t *brlink = bbi_map[pid][br->bb->id];
                int ctxt = 0;

                while( cflink != NULL && brlink != NULL ) {

                    // find edge corresponding to first branch direction in conflict
                    tcfg_edge_t *cf_out = cflink->bbi->out;
                    while( cf_out != NULL && bbi_bid(cf_out->dst) != cf_dst )
                        cf_out = cf_out->next_out;
                    if( cf_out == NULL )
                        printf( "Edge not found.\n" ), exit(1);

                    // find edge corresponding to second branch direction in conflict
                    tcfg_edge_t *br_out = brlink->bbi->out;
                    while( br_out != NULL && bbi_bid(br_out->dst) != br_dst )
                        br_out = br_out->next_out;
                    if( br_out == NULL )
                        printf( "Edge not found.\n" ), exit(1);

                    // print constraint
                    tcfg_edge_term( filp, cf_out, BP_NONE, NO_PREFIX, NO_SUFFIX );
                    fprintf( filp, " + " );
                    tcfg_edge_term( filp, br_out, BP_NONE, NO_PREFIX, NO_SUFFIX );
                    infeas_nullifiers( cf->nullifier_list, cf->num_nullifiers, pid, ctxt );
                    //infeas_blockcount( ib, ctxt );
                    infeas_blockcount( cflink->bbi, ctxt );

                    // next context
                    cflink = cflink->next;
                    brlink = brlink->next;
                    ctxt++;
                }
            }
        }
    }
    fprintf( filp, "\n" );

    return 0;
}


void
constraints()
{
    init_var_grps();

    fprintf(filp, "Maximize\n\n");
    cost_func();
    fprintf(filp, "\n\nSubject to\n\n");

    if (bpred_scheme != NO_BPRED)
        bpred_misses();
    if (enable_icache)
        cache_misses();
    fprintf(filp, "\n");

    // tcfg flow constraints
    tcfg_cons();

    if (bpred_scheme != NO_BPRED) {
        bfg_cons();
        btg_cons();
        tcfg_bfg_cons();
    }

    if (enable_icache) {
        cache_cons();
        if (bpred_scheme != NO_BPRED)
            mp_cache_cons();
        tcfg_estunit_cons(); 
    }

    if(enable_dcache || enable_il2cache) {
        tcfg_estunit_cons_ps(); 
    }

    if (enable_scp_dcache) {
        scp_dcache_cons();
    }

    /* vivy: infeasible path constraints */
    if( enable_infeas )
        infeas_cons();

    // user constraints
    //user_cons();
    user_cons_context();

    write_vars();
}



#undef PLUS
#undef MINUS
