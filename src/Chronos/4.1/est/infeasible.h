/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Infeasible path analysis for Chronos
 * Vivy Suhendra
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
 * 03/2007 infeasible.h
 *
 ******************************************************************************/
#include "common.h"
#include "cfg.h"
#include "symexec.h"

extern char DEBUG_INFEAS;

extern prog_t prog;
extern isa_t  *isa;

extern char *include_proc;


#define REG_RETURN regList[2].name
// the register where return value of a function call is stored (by observation)

#define streq(a, b) (a[0] != '\0' && strcmp(a,b) == 0)


// mnemonics for register/variable values and flags
#define INVALID   0
#define VALID     1
#define CONST     0
#define VAR       1
#define DEF_VAL   0
#define DEF_DERI  ""


#define OP_LEN 9       // length of assembly token
#define DERI_LEN 128   // length of register derivation tree
#define INSN_LEN 50    // length of assembly line

#define NONE 0
#define JUMP 1
#define FALL 2

#define JJ 3      // jump-jump BB conflict
#define JF 4      // jump-fall BB conflict
#define FJ 5      // fall-jump BB conflict
#define FF 6      // fall-fall BB conflict


#define GT 1      // greater
#define GE 2      // greater or equal
#define LT 3      // less
#define LE 4      // less or equal
#define EQ 5      // equal
#define NE 6      // not equal
#define NA 0      // no decision made


// flags in reg_t to determine branch jump direction
#define KO  -2    // a situation not supported
#define NIL -1
#define SLTI 1
#define SLT  2


#if 0
/*HBK: change symbolic execution for greater accuracy*/
typedef struct {
  char name[OP_LEN];
  char deritree[DERI_LEN];  // a sequence of operations on mem. accesses
  int  value;               // a constant; 0 if mem. access (not resolved)
  char valid;   // 1 if value is a valid constant (not unresolved mem. access)
  char flag;    // any special kind of instruction; -1 if not specified 	
} reg_t;
#endif

/*HBK: induction var for data cache*/
#define BIV_INST 0xa   /*is basic induction inst flag*/
typedef struct { 
  char  addr[OP_LEN];
  char  op[OP_LEN];
  char  r1[OP_LEN];
  char  r2[OP_LEN];
  char  r3[OP_LEN];
  int   flag; //mark special opr, e.g. induction operation
} insn_t;


typedef struct branch_t branch_t;

typedef struct {
  cfg_node_t *bb;               // associated block
  char deritree[DERI_LEN];      // derivation tree of affected variable
  int  rhs;                     // the rhs constant
  char rhs_var;                 // 1 if rhs is a variable
  int  lineno;                  // line number in bb
} assign_t;

typedef struct {
  assign_t *conflict_src;
  char     conflict_dir;        // branch direction in the BA conflict: JUMP or FALL
  int      num_nullifiers;
  assign_t **nullifier_list;
} BA_conflict_t;

typedef struct {
  branch_t *conflict_src;
  char     conflict_dir;        // combination of branch directions in the BB conflict: JJ, JF, FJ, or JJ
  int      num_nullifiers;
  assign_t **nullifier_list;
} BB_conflict_t;

struct branch_t {
  cfg_node_t *bb;               // associated block
  char deritree[DERI_LEN];      // derivation tree of tested variable
  int  rhs;                     // the rhs constant
  char rhs_var;                 // 1 if rhs is a variable
  char jump_cond;               // condition that makes a branch instruction jump
  int  num_BA_conflicts;
  BA_conflict_t *BA_conflict_list;
  int  num_BB_conflicts;
  BB_conflict_t *BB_conflict_list;
};


typedef struct {
  cfg_node_t *bb;
  int        num_insn;
  insn_t     *insnlist;
  int        num_assign;      // #assign effects in this node
  assign_t   **assignlist;    // list of assign effects (ptr) in this node
  branch_t   *branch;         // ptr to branch effect associated with this node; NULL if not a branch
  int        loop_id;
  int        exec_count;      // constraint for execution count as determined from DFA (-1 if undetermined)

/*HBK: for more detailed symbolic exec*/
  /*register value tracing*/
  void         *regListIn;  
  void         *regListOut;
  /*memory value tracing*/
  worklist_p    memListIn;
  worklist_p    memListOut;
} inf_node_t;

typedef struct {
  proc_t *proc;
  int num_bb;
  inf_node_t *inf_cfg;
} inf_proc_t;

typedef struct {
  int pid;                // procedure id
  int entry;              // block id of loop entry
  int bound;              // constraint for execution count by user-specified loopbound
  int loop_id;            /*HBK: id of corresponding loop_t*/
} inf_loop_t;

inf_proc_t *inf_procs;    // extension to cfg structure for infeasibility analysis

int *callgraph;

int     num_insn_st;
insn_t *insnlist_st;      // preprocessing instructions


int num_inf_loops;        // loops detected for infeasible path analysis, as specified in .cons (global across procedures)
inf_loop_t *inf_loops;

//int *inf_loop_pid;        // proc id of the loop
//int *inf_loop_entry;      // block id of entry to the loop
//int *inf_loop_branch;     // block id of loop branch
//int *inf_loop_bound;      // constraint for execution count by user-specified loopbound

int num_BA;               // global number of BA conflict pairs
int num_BB;               // global number of BB conflict pairs

void infeas_analysis( char *obj_file );
