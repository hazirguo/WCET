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
 * 03/2007 conflicts.c
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "isa.h"
#include "cfg.h"
#include "tcfg.h"
#include "loops.h"
#include "infeasible.h"

extern prog_t prog;
extern isa_t  *isa;

extern tcfg_nlink_t ***bbi_map;
extern loop_t **loop_map;


int neg( int a ) {

  if( a == GT ) return LE;
  if( a == GE ) return LT;
  if( a == LE ) return GT;
  if( a == LT ) return GE;
  if( a == EQ ) return NE;
  if( a == NE ) return EQ;
  return NA;
}


/*
 * Testing conflict between "X a rhs_a" and "X b rhs_b".
 */
char testConflict( int a, int rhs_a, int b, int rhs_b ) {

  if( a == GT ) { 
    if( b == LT || b == LE || b == EQ ) {
      if( rhs_a < rhs_b ) return 0;
      else return 1;
    }
    else return 0;
  } 
  if( a == LT ) { 
    if( b == GT || b == GE || b == EQ ) {
      if( rhs_a > rhs_b ) return 0;
      return 1;
    }
    else return 0;
  }            
  if( a == GE ) { 
    if( b == LT ) {
      if( rhs_a < rhs_b ) return 0;
      else return 1;
    }
    else if( b == LE || b == EQ ) {
      if( rhs_a <= rhs_b ) return 0;
      else return 1;
    }
    else return 0; 
  }
  if( a == LE ) {
    if( b == GT ) {
      if( rhs_a > rhs_b ) return 0;
      else return 1;
    }
    else if( b == GE || b == EQ ) {
      if( rhs_a >= rhs_b ) return 0;
      else return 1;
    }
    else return 0;
  } 
  if( a == EQ ) { 
    if( b == NE ) {
      if( rhs_a != rhs_b ) return 0;
      else return 1;
    }
    if( b == EQ ) {
      if( rhs_a == rhs_b ) return 0;
      else return 1;
    }
    else if( b == LT ) {
      if( rhs_a < rhs_b ) return 0;
      else return 1;		
    }
    else if( b == GT ) {
      if( rhs_a > rhs_b ) return 0;
      else return 1;		
    }
    else if( b == LE ) {
      if( rhs_a <= rhs_b ) return 0;
      else return 1;		
    }
    else if( b == GE ) {
      if( rhs_a >= rhs_b ) return 0;
      else return 1;		
    }
  }	
  if( a == NE ) {
    if( b == EQ && ( rhs_a == rhs_b ))
      return 1; 
    else return 0;
  } 
  return 0;	
}


/*
 * Testing conflict between A and B.
 * r1, r2 are relational operators associated with A, B respectively.
 * Returns 1 if conflict, 0 otherwise.
 */
char isBBConflict( branch_t *A, branch_t *B, int r1, int r2 ) {  
  return testConflict( r1, A->rhs, r2, B->rhs );
}

char isBAConflict( assign_t *A, branch_t *B, int r ) {  
  if( A->rhs_var ) return 0;
  return testConflict( EQ, A->rhs, r, B->rhs ); 	
}


int addNullifier( assign_t *as, assign_t ***nullifier_list, int *num_nullifiers ) {

  int i, len;

  // check if already in list
  for( i = 0; i < *num_nullifiers; i++ )
    if( (*nullifier_list)[i] == as )
      return 1;

  (*num_nullifiers)++;
  len = *num_nullifiers;
  (*nullifier_list) = (assign_t**) realloc( *nullifier_list, len * sizeof(assign_t*) );
  (*nullifier_list)[len-1] = as;

  return 0;
}


/*
 * Recursive function, support to testReachableNoCancel.
 */
int reachableNoCancel( char *res, assign_t ***nullifier_list, int *num_nullifiers,
		       int pid, int srcid, int destid, char *deritree,
		       assign_t *assg, cfg_node_t *bblist, int num_bb, char **visited ) {
  int i, id;
  cfg_node_t *bb;
  inf_node_t *ib;

  // printf( "checking reachability p%d:: %d --> %d\n", pid, srcid, destid ); fflush( stdout );
  if( srcid == destid ) {
    *res |= 1;
    return 1;
  }

  // check cancellation of effect
  bb = &(bblist[srcid]);
  ib = &(inf_procs[pid].inf_cfg[bb->id]);
  (*visited)[bb->id] = 1;

  // by intermediary assignment
  if( assg == NULL || bb->id != assg->bb->id ) {
    for( i = 0; i < ib->num_assign; i++ ) {

      // printf( "---assg to %s\n", ib->assignlist[i]->deritree );
      //if( strstr( deritree, ib->assignlist[i]->deritree ) != NULL ) {
      if( strcmp( deritree, ib->assignlist[i]->deritree ) == 0 ) {
	int k;
	*res |= 0;
	addNullifier( ib->assignlist[i], nullifier_list, num_nullifiers );
	/*
	printf( "---%d nullifiers:", *num_nullifiers );
	for( k = 0; k < *num_nullifiers; k++ )
	  printf( " <%d::%d>", (*nullifier_list)[k]->bb->id, (*nullifier_list)[k]->lineno );
	printf( "\n" );
	*/
      }
    }
  }

  /*
  // by nested loop
  if( srcid != num_bb - 1 && bb->loop_role == LOOP_HEAD
      && modifiedInLoop( procs[pid]->loops[bb->loopid], br->deritree ))
    *res |= 0;
  */

  if( bb->out_n != NULL ) {
    id = bb->out_n->dst->id;
    // printf( "out_n to %d\n", id );
    if( !(*visited)[id] )
      reachableNoCancel( res, nullifier_list, num_nullifiers, pid, id, destid, deritree, assg, bblist, num_bb, visited );
  }
  if( bb->out_t != NULL ) {
    id = bb->out_t->dst->id;
    // printf( "out_t to %d\n", id ); 
    if( !(*visited)[id] )
      reachableNoCancel( res, nullifier_list, num_nullifiers, pid, id, destid, deritree, assg, bblist, num_bb, visited );
  }
  return 0;
}


/*
 * Checks if there is a path from bblist[srcid] to bblist[destid] in the CFG without
 * passing through a nested loophead nor an assignment to a component of deritree
 * (other than assg, which is the one being tested, if any),
 * and updates the nullifier list when one is found.
 * Returns the result (0/1) by updating *res.
 */
int testReachableNoCancel( char *res, assign_t ***nullifier_list, int *num_nullifiers,
			   int pid, int srcid, int destid, char *deritree,
			   assign_t *assg, cfg_node_t *bblist, int num_bb ) {

  char *visited;  // bit array to mark visited nodes

  // printf( "testReachableNoCancel p%d:: %d --> %d\n", pid, srcid, destid ); fflush( stdout );

  if( srcid == destid )
    *res = 1;

  visited = (char*) calloc( prog.procs[pid].num_bb, sizeof(char) );
  reachableNoCancel( res, nullifier_list, num_nullifiers, pid, srcid, destid, deritree, assg, bblist, num_bb, &visited );

  free( visited );
  return 0;
}


/*
 * Records conflict between assg and br.
 */
int setBAConflict( assign_t *assg, branch_t *br, char dir, assign_t **nullifier_list, int num_nullifiers ) {

  int i, n;

  br->num_BA_conflicts++;
  n = br->num_BA_conflicts;
  br->BA_conflict_list = (BA_conflict_t*) realloc( br->BA_conflict_list, n * sizeof(BA_conflict_t) );
  br->BA_conflict_list[n-1].conflict_src = assg;
  br->BA_conflict_list[n-1].conflict_dir = dir;
  br->BA_conflict_list[n-1].num_nullifiers = num_nullifiers;
  br->BA_conflict_list[n-1].nullifier_list = NULL;

  if( num_nullifiers > 0 ) {
    br->BA_conflict_list[n-1].nullifier_list = (assign_t**) malloc( num_nullifiers * sizeof(assign_t*) );
    for( i = 0; i < num_nullifiers; i++ )
      br->BA_conflict_list[n-1].nullifier_list[i] = nullifier_list[i];
  }
  return 0;
}


/*
 * Records conflict between br1 and br2.
 */
int setBBConflict( branch_t *br1, branch_t *br2, char dir, assign_t **nullifier_list, int num_nullifiers ) {

  int i, n;

  br2->num_BB_conflicts++;
  n = br2->num_BB_conflicts;
  br2->BB_conflict_list = (BB_conflict_t*) realloc( br2->BB_conflict_list, n * sizeof(BB_conflict_t) );
  br2->BB_conflict_list[n-1].conflict_src = br1;
  br2->BB_conflict_list[n-1].conflict_dir = dir;
  br2->BB_conflict_list[n-1].num_nullifiers = num_nullifiers;
  br2->BB_conflict_list[n-1].nullifier_list = NULL;
  
  if( num_nullifiers > 0 ) {
    br2->BB_conflict_list[n-1].nullifier_list = (assign_t**) malloc( num_nullifiers * sizeof(assign_t*) );
    for( i = 0; i < num_nullifiers; i++ )
      br2->BB_conflict_list[n-1].nullifier_list[i] = nullifier_list[i];
  }
  return 0;
}


char isLoopBranch( cfg_node_t *bb ) {

  tcfg_elink_t *elink;
  tcfg_node_t *node = bbi_map[bb->proc->id][bb->id]->bbi;

  for( elink = loop_map[node->id]->exits; elink != NULL; elink = elink->next ) {
    if( node == elink->edge->src )
      return 1;
  }
  return 0;
}


/*
 * Returns 1 if b1 and b2 are in the same loop.
 * This definition includes the case where both are not in loop;
 * excludes the case where one is nested inside the other.
 */
char loopEqual( cfg_node_t *b1, cfg_node_t *b2 ) {

  int nd1 = bbi_map[b1->proc->id][b1->id]->bbi->id;
  int nd2 = bbi_map[b2->proc->id][b2->id]->bbi->id;
  return (loop_map[nd1] == loop_map[nd2]);
}


/*
 * Checks pairwise effects to identify conflicts.
 */
int detectConflicts() {

  int i, j, k, n;

  // initializations
  num_BA = 0;
  num_BB = 0;

  // test each branch
  for( i = 0; i < prog.num_procs; i++ ) {
    proc_t *p = &(prog.procs[i]);
    inf_proc_t *ip = &(inf_procs[i]);

    if( !include_proc[i] )
      continue;

    for( j = 0; j < ip->num_bb; j++ ) {
      inf_node_t *ib = &(ip->inf_cfg[j]);
      branch_t *br = ib->branch;

      if( br == NULL )
	continue;
      // printBranch( br, 0 );

      // exclude loop branch test
      if( isLoopBranch( ib->bb ))
	continue;

      // check self-effect
      for( k = 0; k < ib->num_assign; k++ ) {
	assign_t *as = ib->assignlist[k];
	// printf( "- " ); printAssign( as, 0 );

	// same tested variable
	if( streq( br->deritree, as->deritree )) {

	  if( isBAConflict( as, br, br->jump_cond )) {
	    setBAConflict( as, br, JUMP, NULL, 0 );
	    // printf( ">> BA jump conflict\n" );
	    num_BA++;
	  }
	  else if( isBAConflict( as, br, neg( br->jump_cond ))) {
	    setBAConflict( as, br, FALL, NULL, 0 );
	    // printf( ">> BA fall conflict\n" );
	    num_BA++;
	  }
	  // stop once a match is found (can have at most one match)
	  break;
	}
      }

      // check against effects coming before this node in the CFG
      //for( k = 0; k < ip->num_bb; k++ ) {
      for( k = 0; k < j; k++ ) {
	inf_node_t *ibk  = &(ip->inf_cfg[k]);
	branch_t *brk = ibk->branch;

	if( j == k )
	  continue;

	// isolate effects within loop body
	if( !loopEqual( ib->bb, ibk->bb ))
	  continue;

	// check BA conflict
	for( n = 0; n < ibk->num_assign; n++ ) {
	  assign_t *as = ibk->assignlist[n];
	  // printf( "- " ); printAssign( as, 0 );

	  if( streq( as->deritree, br->deritree )) {
	    char res = 0;
	    int num_nullifiers = 0;
	    assign_t **nullifier_list = NULL;

	    // printf( "testBA: %s (%d --> %d)\n", br->deritree, k, j );
	    testReachableNoCancel( &res, &nullifier_list, &num_nullifiers, p->id, k, j, br->deritree, as, p->cfg, p->num_bb );

	    if( res ) {
	      if( isBAConflict( as, br, br->jump_cond )) {
		setBAConflict( as, br, JUMP, nullifier_list, num_nullifiers );
		// printf( ">> BA jump conflict\n" );
		num_BA++;
	      }
	      else if( isBAConflict( as, br, neg( br->jump_cond ))) {
		setBAConflict( as, br, FALL, nullifier_list, num_nullifiers );
		// printf( ">> BA fall conflict\n" );
		num_BA++;
	      }
	    }
	    free( nullifier_list );
	    // stop once a match is found (can have at most one match)
	    break;
	  }
	}

	// check BB conflict
	if( brk == NULL )
	  continue;
	// printf( "- " ); printBranch( brk, 0 );

	if( streq( brk->deritree, br->deritree )) {

	  int jump = p->cfg[k].out_t->dst->id;
	  int fall = p->cfg[k].out_n->dst->id;

	  // test all combinations of edge directions
	  char res = 0;
	  int num_nullifiers = 0;
	  assign_t **nullifier_list = NULL;

	  // printf( "testBB: %s (%d,%d --> %d)\n", br->deritree, k, jump, j );
	  testReachableNoCancel( &res, &nullifier_list, &num_nullifiers, p->id, jump, j, br->deritree, NULL, p->cfg, p->num_bb );

	  if( res ) {
	    if( isBBConflict( brk, br, brk->jump_cond, br->jump_cond )) {
	      setBBConflict( brk, br, JJ, nullifier_list, num_nullifiers );
	      // printf( ">> BB jump-jump conflict\n" );
	      num_BB++;
	    }
	    else if( isBBConflict( brk, br, brk->jump_cond, neg( br->jump_cond ))) {
	      setBBConflict( brk, br, JF, nullifier_list, num_nullifiers );
	      // printf( ">> BB jump-fall conflict\n" );
	      num_BB++;
	    }
	  }
	  free( nullifier_list );

	  res = 0;
	  num_nullifiers = 0;
	  nullifier_list = NULL;

	  // printf( "testBB: %s (%d,%d --> %d)\n", br->deritree, k, fall, j );
	  testReachableNoCancel( &res, &nullifier_list, &num_nullifiers, p->id, fall, j, br->deritree, NULL, p->cfg, p->num_bb );
  
	  if( res ) {
	    if( isBBConflict( brk, br, neg( brk->jump_cond ), br->jump_cond )) {
	      setBBConflict( brk, br, FJ, nullifier_list, num_nullifiers );
	      // printf( ">> BB fall-jump conflict\n" );
	      num_BB++;
	    }
	    else if( isBBConflict( brk, br, neg( brk->jump_cond ), neg( br->jump_cond ))) {
	      setBBConflict( brk, br, FF, nullifier_list, num_nullifiers );
	      // printf( ">> BB fall-fall conflict\n" );
	      num_BB++;
	    }
	  }
	  free( nullifier_list );
	}
      } // end for each incoming block

    } // for nodes

  } // for procs

  return 0;
}
