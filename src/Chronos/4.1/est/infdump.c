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
 * 03/2007 infdump.c
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "isa.h"
#include "cfg.h"
#include "infeasible.h"


extern prog_t prog;




void printInstr( insn_t *is ) {
  printf( "0x%s %s %s %s %s", is->addr, is->op, is->r1, is->r2, is->r3 );
  fflush( stdout );
}
void fprintInstr(FILE *fp, insn_t *is ) {
  fprintf(fp, "0x%s %s %s %s %s", is->addr, is->op, is->r1, is->r2, is->r3 );
  fflush( fp );
}


int printInstructions() {

  int i, j, k;
  inf_proc_t *ip;
  inf_node_t *ib;
  insn_t     *is;

  for( i = 0; i < num_insn_st; i++ ) {
    is = &(insnlist_st[i]);
    printf( "[st,--,%2d] 0x%s %s %s %s %s\n", i, is->addr, is->op, is->r1, is->r2, is->r3 );
  }

  for( i = 0; i < prog.num_procs; i++ ) {
    ip = &(inf_procs[i]);
    for( j = 0; j < ip->num_bb; j++ ) {
      ib = &(ip->inf_cfg[j]);
      for( k = 0; k < ib->num_insn; k++ ) {
	is = &(ib->insnlist[k]);
	printf( "[%2d,%2d,%2d] 0x%s %s %s %s %s\n", i, j, k, is->addr, is->op, is->r1, is->r2, is->r3 );
      }
    }
  }
  return 0;
}


char *getDirName( int dir ) {

  switch( dir ) {
  case GT: return "GT";
  case GE: return "GE";
  case LT: return "LT";
  case LE: return "LE";
  case EQ: return "EQ";
  case NE: return "NE";
  case NA: return "NA";
  default:
    return "";
  }
}


int printBAConflict( BA_conflict_t *cf ) {

  int i;

  if( cf->conflict_dir == JUMP )
    printf( "-- <%d::%d> JUMP", cf->conflict_src->bb->id, cf->conflict_src->lineno );
  else if( cf->conflict_dir == FALL )
    printf( "-- <%d::%d> FALL", cf->conflict_src->bb->id, cf->conflict_src->lineno );

  for( i = 0; i < cf->num_nullifiers; i++ )
    printf( "  _<%d::%d> ", cf->nullifier_list[i]->bb->id, cf->nullifier_list[i]->lineno );
  printf( "\n" );

  return 0;
}

int printBBConflict( BB_conflict_t *cf ) {

  int i;

  if( cf->conflict_dir == JJ )
    printf( "-- <%d> JUMP/JUMP", cf->conflict_src->bb->id );
  else if( cf->conflict_dir == JF )
    printf( "-- <%d> JUMP/FALL", cf->conflict_src->bb->id );
  else if( cf->conflict_dir == FJ )
    printf( "-- <%d> FALL/JUMP", cf->conflict_src->bb->id );
  else if( cf->conflict_dir == FF )
    printf( "-- <%d> FALL/FALL", cf->conflict_src->bb->id );

  for( i = 0; i < cf->num_nullifiers; i++ )
    printf( "  _<%d::%d>", cf->nullifier_list[i]->bb->id, cf->nullifier_list[i]->lineno );
  printf( "\n" );

  return 0;
}


int printBranch( branch_t *br, char printcf ) {

  int i, k;

  if( br == NULL )
    return -1;

  printf( "B <%d,%3d>", br->bb->proc->id, br->bb->id );
  printf( "  rhs:" );
  if( br->rhs_var )
    printf( "var" );
  else
    printf( "%3d", br->rhs );

  printf( "  jump:%s", getDirName( br->jump_cond ));
  printf( "  deri:%s\n", br->deritree );

  if( !printcf )
    return 0;

  /*
  if( br->num_conflicts ) {
    printf( "- outgoing conflicts:" );
    for( i = 0; i < br->num_conflicts; i++ ) {
      if( br->conflictdir_jump[i] != -1 )
	printf( " %d (%sx%s)", br->conflicts[i]->bb->id, getDirName( br->jump_cond ), getDirName( br->conflictdir_jump[i] ));
      if( br->conflictdir_fall[i] != -1 )
	printf( " %d (%sx%s)", br->conflicts[i]->bb->id, getDirName( neg(br->jump_cond) ), getDirName( br->conflictdir_fall[i] ));
    }
    printf( "\n" );
  }
  if( br->num_incfs ) {
    printf( "- incoming conflicts:" );
    for( i = 0; i < inf_procs[br->bb->proc->id].num_bb; i++ )
      if( br->in_conflict[i] )
	printf( " %d", i );
    printf( "\n" );
  }
  */
  if( br->num_BA_conflicts > 0 ) {
    printf( "- BA conflicts:\n" );
    for( i = 0; i < br->num_BA_conflicts; i++ )
      printBAConflict( &(br->BA_conflict_list[i]) );
  }
  if( br->num_BB_conflicts > 0 ) {
    printf( "- BB conflicts:\n" );
    for( i = 0; i < br->num_BB_conflicts; i++ )
      printBBConflict( &(br->BB_conflict_list[i]) );
  }

  return 0;
}


int printAssign( assign_t *assg, char printcf ) {

  int i;

  printf( "A <%d,%3d,%3d> ", assg->bb->proc->id, assg->bb->id, assg->lineno );
  printf( "rhs:" );
  if( assg->rhs_var )
    printf( "var " );
  else
    printf( "%3d ", assg->rhs );
  printf( "deri:%s\n", assg->deritree );

  if( !printcf )
    return 0;

  /*
  if( assg->num_conflicts ) {
    printf( "- outgoing conflicts:" );
    for( i = 0; i < assg->num_conflicts; i++ )
      printf( " %d(%d)", assg->conflicts[i]->bb->id, assg->conflictdir[i] );
    printf( "\n" );
  }
  */
  return 0;
}


int printEffects( char printcf ) {

  int i, j, k;
  inf_node_t *ib;

  printf( "--------------------------------\n" );
  for( i = 0; i < prog.num_procs; i++ ) {
    for( j = 0; j < inf_procs[i].num_bb; j++ ) {
      ib = &(inf_procs[i].inf_cfg[j]);
      for( k = 0; k < ib->num_assign; k++ )
	printAssign( ib->assignlist[k], printcf );
      printBranch( ib->branch, printcf );
    }
  }
  printf( "--------------------------------\n" );
  return 0;
}
