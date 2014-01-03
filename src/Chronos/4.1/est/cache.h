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
 *
 * $Id: cache.h,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ***********************************************************************************/

#ifndef CACHE_H
#define CACHE_H
#include "address.h"
#define MAX_CACHE_SETS	    1024

// 14-bit tag is selected for the following reason:
// - it is enough: can analyze program up to 16MB for a 1-KB cache
// - in cache.c, a valid bit is needed with tag in some cases, thus the valid
//   bit and the tag can be accomodated in a short int
#define	MAX_TAG_BITS	    14


#define CACHE_LINE(addr)    ((addr) & cache.l_msk)
#define	SET(addr)	    (((addr) & cache.s_msk) >> cache.lsb)
#define	TAG(addr)	    (((addr) & cache.t_msk) >> cache.s_lb)
#define TAGSET(addr)	    (((addr) & cache.t_s_msk) >> cache.lsb)
#define TAGSET2(tag, set)   (((tag) << cache.nsb) | (set))
#define LSB_OFF(addr)	    ((addr) >> cache.lsb)
#define LSB_ON(addr)	    ((addr) << cache.lsb)
// clear the LSB bits (all LSB bits are set to 0 whatever they are)
#define CLEAR_LSB(addr)	    (((addr) >> cache.lsb) << cache.lsb)
#define MBLK_ID(sa, addr)   (TAGSET(addr) - TAGSET(sa))

#define IC_HIT		    0
#define IC_MISS		    1
#define IC_UNCLEAR	    2
#define MC_INC_SIZE	    8

enum { IC_ILP, IC_CATEGORIZE };



// cache configuration
typedef struct {
    // decided by cache config parameters
    int		ns;	// #sets
    int		ls;	// cache line size
    int		nsb;    // nsb = log(ns)
    int		lsb;    // lsb = log(ls)
    int		cmp;    // cache miss penalty
    int		na;

    // decided by cache config + program size
    int		nt;	// #tags
    int		ntb;	// tag bits = log(nt)

    // combined bits
    int		s_lb;	// set + line bits
    int		t_sb;	// tag + set bits
    int		nt_s;	// # of tag + set

    // masks for set, tag, etc
    unsigned	l_msk;	// block mask
    unsigned	s_msk;	// set mask
    unsigned	t_msk;	// tag mask
    unsigned	t_s_msk;// set+tag mask
} cache_t;



// memory block data structure (complete memory block info)
typedef struct {
    unsigned short  set;    // cache line
    unsigned short  tag;    // valid tag
} mem_blk_t;


// memory block tag data structure (or partial memory block info w/o set) this
// implementation supports up to 2^16 = 64K memory blocks mapped to each cache
// line, thus for a 1KB cache, the maximum program size supported would be 64M
typedef struct {
    unsigned short  valid;  // valid == 0 means empty mempry block
    unsigned short  tag;    // memory block tag
} mblk_tag_t;

/* Describes the type for a set of memory blocks */
struct mem_blk_set 
{
	unsigned block;	  
	struct mem_blk_set* next;
};

typedef struct mem_blk_set mem_blk_set_t;

/* Abstract cache block */
struct cache_block {
	mem_blk_set_t* mem_blk_h;
};

struct ovr_mapping {
	unsigned old;
	unsigned newb;
	struct ovr_mapping* next;
};	

typedef struct ovr_mapping* ovr_map_p;
typedef struct ovr_mapping ovr_map_s;

#define CACHE_SIZE  (CACHE_SET_SIZE * MAX_CACHE_SET * SIZE_OF_BLOCK)
#define CACHE_SET_SIZE X
#define MAX_CACHE_SET  Y
#define PSEUDO CACHE_SET_SIZE
#define DATA_MISS_PENALTY 6
#define L1_MISS_PENALTY l1
#define L2_MISS_PENALTY l2
#define GET_SET(x) ((x) % MAX_CACHE_SET)
#define PENALTY 119400

enum ACCESS {
	ALL_HIT = 0,
	ALL_MISS,
	PS,
	ALL_X, /* Not accessed */	
	NOT_CLASSIFIED
};

enum ANALYSIS {
	MUST = 0,
	MAY,
	PERSISTENCE	  
};	

typedef enum ACCESS ACCESS_T;
typedef enum ANALYSIS ANALYSIS_T;
typedef struct cache_block* acs_p;
typedef struct cache_block acs_s;

void
set_cache_basic(int nsets, int assoc, int bsize, int miss_penalty);

void analyze_data_cache();
void analyze_abs_instr_cache();
void analyze_unified_cache();
#endif
