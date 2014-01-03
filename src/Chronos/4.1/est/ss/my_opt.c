
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
 * $Id: my_opt.c,v 1.1.1.1 2006/06/17 00:58:23 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "machine.h"
#include "options.h"
#include "resource.h"
#include "../bpred.h"



// option variables from SimpleScalar


/*
 * simulator options
 */

/* instruction fetch queue size (in insts) */
int ruu_ifq_size;

/* extra branch mis-prediction latency */
int ruu_branch_penalty;

/* branch predictor type {nottaken|taken|perfect|bimod|2lev} */
char *pred_type;

/* bimodal predictor config (<table_size>) */
int bimod_nelt = 1;
int bimod_config[1] =
  { /* bimod tbl size */4 };

/* 2-level predictor config (<l1size> <l2size> <hist_size> <xor>) */
int twolev_nelt = 4;
int twolev_config[4] =
  { /* l1size */1, /* l2size */128, /* hist */2, /* xor */TRUE};

/* combining predictor config (<meta_table_size> */
int comb_nelt = 1;
int comb_config[1] =
  { /* meta_table_size */4 };

/* return address stack (RAS) size */
int ras_size = 8;

/* BTB predictor config (<num_sets> <associativity>) */
int btb_nelt = 2;
int btb_config[2] =
  { /* nsets */4, /* assoc */1 };

/* instruction decode B/W (insts/cycle) */
int ruu_decode_width;

/* instruction issue B/W (insts/cycle) */
int ruu_issue_width;

/* run pipeline with in-order issue */
int ruu_inorder_issue;

/* issue instructions down wrong execution paths */
int ruu_include_spec = TRUE;

/* instruction commit B/W (insts/cycle) */
int ruu_commit_width;

/* register update unit (RUU) size */
int RUU_size = 8;

/* load/store queue (LSQ) size */
int LSQ_size = 8;

int fetch_speed = 1;

int fetch_width;

/* l1 data cache config, i.e., {<config>|none} */
char *cache_dl1_opt;

/* l1 data cache hit latency (in cycles) */
int cache_dl1_lat;

/* l2 data cache config, i.e., {<config>|none} */
char *cache_dl2_opt;

/* l2 data cache hit latency (in cycles) */
int cache_dl2_lat;

/* l1 instruction cache config, i.e., {<config>|dl1|dl2|none} */
char *cache_il1_opt;

/* l1 instruction cache hit latency (in cycles) */
int cache_il1_lat;

/* l2 instruction cache config, i.e., {<config>|dl1|dl2|none} */
char *cache_il2_opt;

/* l2 instruction cache hit latency (in cycles) */
int cache_il2_lat;

/* flush caches on system calls */
int flush_on_syscalls;

/* convert 64-bit inst addresses to 32-bit inst equivalents */
int compress_icache_addrs;

/* memory access latency (<first_chunk> <inter_chunk>) */
int mem_nelt = 2;
int mem_lat[2] =
  { /* lat to first chunk */30, /* lat between remaining chunks */1 };

/* memory access bus width (in bytes) */
int mem_bus_width;

/* instruction TLB config, i.e., {<config>|none} */
char *itlb_opt;

/* data TLB config, i.e., {<config>|none} */
char *dtlb_opt;

/* inst/data TLB miss latency (in cycles) */
int tlb_miss_lat;

/* total number of integer ALU's available */
int res_ialu;

/* total number of integer multiplier/dividers available */
int res_imult;

/* total number of memory system ports available (to CPU) */
int res_memport;

/* total number of floating point ALU's available */
int res_fpalu;

/* total number of floating point multiplier/dividers available */
int res_fpmult;

/* options for level 1 instruction cache */
int nsets, bsize, assoc;

/* sudiptac ::: adding options for level 1 data cache */
int nsets_dl1, bsize_dl1, assoc_dl1;

/* sudiptac ::: adding options for level 2 cache 
 * (it could be unified or separate instruction cache) */
int nsets_l2, bsize_l2, assoc_l2;

/*
 * functional unit resource configuration
 */

/* resource pool indices, NOTE: update these if you change FU_CONFIG */
#define FU_IALU_INDEX			0
#define FU_IMULT_INDEX			1
#define FU_MEMPORT_INDEX		2
#define FU_FPALU_INDEX			3
#define FU_FPMULT_INDEX			4

/* resource pool definition, NOTE: update FU_*_INDEX defs if you change this */
struct res_desc fu_config[] = {
  {
    "integer-ALU",
    1,
    0,
    {
      { IntALU, 1, 1 }
    }
  },
  {
    "integer-MULT/DIV",
    1,
    0,
    {
      { IntMULT, 4, 4 },
      { IntDIV, 4, 4 }
    }
  },
  {
    "memory-port",
    2,
    0,
    {
      { RdPort, 1, 1 },
      { WrPort, 1, 1 }
    }
  },
  {
    "FP-adder",
    1,
    0,
    {
      { FloatADD, 2, 2 },
      { FloatCMP, 2, 2 },
      { FloatCVT, 2, 2 }
    }
  },
  {
    "FP-MULT/DIV",
    1,
    0,
    {
      { FloatMULT, 12, 12 },
      { FloatDIV, 12, 12 },
      { FloatSQRT, 12, 12 }
    }
  },
};


// my options
char	*run_opt;	// EST (estimation) or CFG (generate CFG file)


/* options database */
struct opt_odb_t *sim_odb;


// hardware configuration: derived from SimpleScalar options
extern int  bpred_scheme;
extern int  enable_icache;	    
extern int  enable_pipeline;

/* sudiptac ::: for level 2 cache analysis */
extern int enable_dcache;
extern int enable_il2cache;
extern int enable_ul2cache;

// branch predictor parameters
extern int  BHR, BHR_PWR, BHR_MSK;
extern int  BHT_SIZE, BHT, BHT_MSK;

// IBUF & ROB
extern int  pipe_ibuf_size, pipe_iwin_size;
int	    PROLOG_SIZE, EPILOG_SIZE;



static int
orphan_fn(int i, int argc, char **argv)
{
  return /* done */FALSE;
}



/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  

  /* ifetch options */

  opt_reg_int(odb, "-fetch:ifqsize", "instruction fetch queue size (in insts)",
	      &ruu_ifq_size, /* default */4,
	      /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-fetch:mplat", "extra branch mis-prediction latency",
	      &ruu_branch_penalty, /* default */0,
	      /* print */TRUE, /* format */NULL);


  opt_reg_int(odb, "-fetch:speed", "speed of front-end of machine relative to execution core",
	      &fetch_speed, /* default */1,
	      /* print */TRUE, /* format */NULL);


  /* branch predictor options */

  opt_reg_string(odb, "-bpred",
		 "branch predictor type {nottaken|taken|perfect|bimod|2lev|comb}",
                 &pred_type, /* default */"2lev",
                 /* print */TRUE, /* format */NULL);

  opt_reg_int_list(odb, "-bpred:bimod",
		   "bimodal predictor config (<table size>)",
		   bimod_config, bimod_nelt, &bimod_nelt,
		   /* default */bimod_config,
		   /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int_list(odb, "-bpred:2lev",
                   "2-level predictor config "
		   "(<l1size> <l2size> <hist_size> <xor>)",
                   twolev_config, twolev_nelt, &twolev_nelt,
		   /* default */twolev_config,
                   /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int_list(odb, "-bpred:comb",
		   "combining predictor config (<meta_table_size>)",
		   comb_config, comb_nelt, &comb_nelt,
		   /* default */comb_config,
		   /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int_list(odb, "-bpred:btb",
		   "BTB config (<num_sets> <associativity>)",
		   btb_config, btb_nelt, &btb_nelt,
		   /* default */btb_config,
		   /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  /* decode options */

  opt_reg_int(odb, "-decode:width",
	      "instruction decode B/W (insts/cycle)",
	      &ruu_decode_width, /* default */2,
	      /* print */TRUE, /* format */NULL);

  /* issue options */

  opt_reg_int(odb, "-issue:width",
	      "instruction issue B/W (insts/cycle)",
	      &ruu_issue_width, /* default */8,
	      /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-issue:inorder", "run pipeline with in-order issue",
	       &ruu_inorder_issue, /* default */FALSE,
	       /* print */TRUE, /* format */NULL);

  opt_reg_flag(odb, "-issue:wrongpath",
	       "issue instructions down wrong execution paths",
	       &ruu_include_spec, /* default */TRUE,
	       /* print */TRUE, /* format */NULL);

  /* commit options */

  opt_reg_int(odb, "-commit:width",
	      "instruction commit B/W (insts/cycle)",
	      &ruu_commit_width, /* default */1,
	      /* print */TRUE, /* format */NULL);

  /* register scheduler options */

  opt_reg_int(odb, "-ruu:size",
	      "register update unit (RUU) size",
	      &RUU_size, /* default */8,
	      /* print */TRUE, /* format */NULL);

  /* memory scheduler options  */

  opt_reg_int(odb, "-lsq:size",
	      "load/store queue (LSQ) size",
	      &LSQ_size, /* default */8,
	      /* print */TRUE, /* format */NULL);

  /* cache options */

  opt_reg_string(odb, "-cache:dl1",
		 "l1 data cache config, i.e., {<config>|none}",
		 &cache_dl1_opt, "none",
		 /* print */TRUE, NULL);

  opt_reg_int(odb, "-cache:dl1lat",
	      "l1 data cache hit latency (in cycles)",
	      &cache_dl1_lat, /* default */1,
	      /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-cache:dl2",
		 "l2 data cache config, i.e., {<config>|none}",
		 &cache_dl2_opt, "none",
		 /* print */TRUE, NULL);

  opt_reg_int(odb, "-cache:dl2lat",
	      "l2 data cache hit latency (in cycles)",
	      &cache_dl2_lat, /* default */6,
	      /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-cache:il1",
		 "l1 inst cache config, i.e., {<config>|dl1|dl2|none}",
		 &cache_il1_opt, "il1:16:32:2:l",
		 /* print */TRUE, NULL);

  opt_reg_int(odb, "-cache:il1lat",
	      "l1 instruction cache hit latency (in cycles)",
	      &cache_il1_lat, /* default */1,
	      /* print */TRUE, /* format */NULL);

  opt_reg_string(odb, "-cache:il2",
		 "l2 instruction cache config, i.e., {<config>|dl2|none}",
		 &cache_il2_opt, "none",
		 /* print */TRUE, NULL);

  opt_reg_int(odb, "-cache:il2lat",
	      "l2 instruction cache hit latency (in cycles)",
	      &cache_il2_lat, /* default */6,
	      /* print */TRUE, /* format */NULL);

  /* mem options */
  opt_reg_int_list(odb, "-mem:lat",
		   "memory access latency (<first_chunk> <inter_chunk>)",
		   mem_lat, mem_nelt, &mem_nelt, mem_lat,
		   /* print */TRUE, /* format */NULL, /* !accrue */FALSE);

  opt_reg_int(odb, "-mem:width", "memory access bus width (in bytes)",
	      &mem_bus_width, /* default */64,
	      /* print */TRUE, /* format */NULL);

  /* TLB options */

  opt_reg_string(odb, "-tlb:itlb",
		 "instruction TLB config, i.e., {<config>|none}",
		 &itlb_opt, "none", /* print */TRUE, NULL);

  opt_reg_string(odb, "-tlb:dtlb",
		 "data TLB config, i.e., {<config>|none}",
		 &dtlb_opt, "none", /* print */TRUE, NULL);

  opt_reg_int(odb, "-tlb:lat",
	      "inst/data TLB miss latency (in cycles)",
	      &tlb_miss_lat, /* default */30,
	      /* print */TRUE, /* format */NULL);

  /* resource configuration */

  opt_reg_int(odb, "-res:ialu",
	      "total number of integer ALU's available",
	      &res_ialu, /* default */fu_config[FU_IALU_INDEX].quantity,
	      /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:imult",
	      "total number of integer multiplier/dividers available",
	      &res_imult, /* default */fu_config[FU_IMULT_INDEX].quantity,
	      /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:memport",
	      "total number of memory system ports available (to CPU)",
	      &res_memport, /* default */fu_config[FU_MEMPORT_INDEX].quantity,
	      /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:fpalu",
	      "total number of floating point ALU's available",
	      &res_fpalu, /* default */fu_config[FU_FPALU_INDEX].quantity,
	      /* print */TRUE, /* format */NULL);

  opt_reg_int(odb, "-res:fpmult",
	      "total number of floating point multiplier/dividers available",
	      &res_fpmult, /* default */fu_config[FU_FPMULT_INDEX].quantity,
	      /* print */TRUE, /* format */NULL);

  // register my options
  opt_reg_string(odb, "-run",
		 "functionality {CFG EST}",
                 &run_opt, /* default */"EST",
                 /* print */TRUE, /* format */NULL);

}



void
sim_check_options(struct opt_odb_t *odb,        /* options database */
		  int argc, char **argv)        /* command line arguments */
{
  char name[128], c;

  if (ruu_ifq_size < 1 || (ruu_ifq_size & (ruu_ifq_size - 1)) != 0)
    fatal("inst fetch queue size must be positive > 0 and a power of two");
  pipe_ibuf_size = ruu_ifq_size;

  fetch_width = ruu_decode_width * fetch_speed;

  //if (ruu_branch_penalty < 1)
   // fatal("mis-prediction penalty must be at least 1 cycle");

  if (!mystricmp(pred_type, "perfect"))
    {
      /* perfect predictor */
      bpred_scheme = NO_BPRED;
    }
#if 0
  else if (!mystricmp(pred_type, "taken"))
    {
      /* static predictor, not taken */
      //pred = bpred_create(BPredTaken, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
  else if (!mystricmp(pred_type, "nottaken"))
    {
      /* static predictor, taken */
      //pred = bpred_create(BPredNotTaken, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
#endif
  else if (!mystricmp(pred_type, "bimod"))
    {
      /* bimodal predictor, bpred_create() checks BTB_SIZE */
      if (bimod_nelt != 1)
	fatal("bad bimod predictor config (<table_size>)");
      if (btb_nelt != 2)
	fatal("bad btb config (<num_sets> <associativity>)");

      bpred_scheme = LOCAL;
      BHT_SIZE = btb_config[0];
      BHT = log_base2(BHT_SIZE);
      BHT_MSK = BHT_SIZE - 1;
    }
  else if (!mystricmp(pred_type, "2lev"))
    {
      if (twolev_nelt != 4)
	fatal("bad 2-level pred config (<l1size> <l2size> <hist_size> <xor>)");
      if (btb_nelt != 2)
	fatal("bad btb config (<num_sets> <associativity>)");
      if ((twolev_config[1] & (twolev_config[1] - 1)) != 0)
	fatal("Branch History Table (BHT) size %d is not power of 2!\n", twolev_config[1]);
      if ((twolev_config[2] < 0) || (twolev_config[2] > 4))
	fatal("Branch History Register (BHR) bits %d is not within [0, 4]\n", twolev_config[2]);

      if (twolev_config[2] == 0)
	  bpred_scheme = LOCAL;
      if (twolev_config[3] == FALSE)
	  bpred_scheme = GAG;
      else
	  bpred_scheme = GSHARE;
      BHT_SIZE = twolev_config[1];
      BHT = log_base2(BHT_SIZE);
      BHT_MSK = BHT_SIZE - 1;
      BHR = twolev_config[2];
      BHR_PWR = 1 << BHR;
      BHR_MSK = BHR_PWR - 1;
      
      if ( (1 << BHR) > BHT_SIZE)
	fatal("The combination of Branch History Register (BHR) bits %d and \nBranch History Table size %d is invalid because 2^BHR > BHT \n", BHR, BHT_SIZE);
    }
#if 0
  else if (!mystricmp(pred_type, "comb"))
    {
      /* combining predictor, bpred_create() checks args */
      if (twolev_nelt != 4)
	fatal("bad 2-level pred config (<l1size> <l2size> <hist_size> <xor>)");
      if (bimod_nelt != 1)
	fatal("bad bimod predictor config (<table_size>)");
      if (comb_nelt != 1)
	fatal("bad combining predictor config (<meta_table_size>)");
      if (btb_nelt != 2)
	fatal("bad btb config (<num_sets> <associativity>)");

      pred = bpred_create(BPredComb,
			  /* bimod table size */bimod_config[0],
			  /* l1 size */twolev_config[0],
			  /* l2 size */twolev_config[1],
			  /* meta table size */comb_config[0],
			  /* history reg size */twolev_config[2],
			  /* history xor address */twolev_config[3],
			  /* btb sets */btb_config[0],
			  /* btb assoc */btb_config[1],
			  /* ret-addr stack size */ras_size);
    }
#endif
  else
    fatal("cannot parse predictor type `%s'", pred_type);

#if 0
  if (ruu_decode_width < 1 || (ruu_decode_width & (ruu_decode_width-1)) != 0)
    fatal("issue width must be positive non-zero and a power of two");

  if (ruu_issue_width < 1 || (ruu_issue_width & (ruu_issue_width-1)) != 0)
    fatal("issue width must be positive non-zero and a power of two");

  if (ruu_commit_width < 1)
    fatal("commit width must be positive non-zero");
#endif

  if (RUU_size < 2)
    fatal("Re-order buffer size must be greater than one!");
  pipe_iwin_size = RUU_size;
  PROLOG_SIZE = pipe_iwin_size + pipe_ibuf_size;
  EPILOG_SIZE = pipe_iwin_size * 2;
  LSQ_size = pipe_iwin_size;

#if 0
  if (LSQ_size < 2 || (LSQ_size & (LSQ_size-1)) != 0)
    fatal("LSQ size must be a positive number > 1 and a power of two");
#endif

  /* use a level 1 D-cache? */
  if (!mystricmp(cache_dl1_opt, "none"))
    {
#if 0
      cache_dl1 = NULL;
#endif
		enable_dcache = 0;  
      /* the level 2 D-cache cannot be defined */
      if (strcmp(cache_dl2_opt, "none"))
	fatal("the l1 data cache must defined if the l2 cache is defined");
#if 0
      cache_dl2 = NULL;
#endif
    }
  else /* dl1 is defined */
    {
      if (sscanf(cache_dl1_opt, "%[^:]:%d:%d:%d:%c",
		 name, &nsets_dl1, &bsize_dl1, &assoc_dl1, &c) != 5)
		fatal("bad l1 D-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");

		/* sudiptac ::: enable level 1 data cache analysis */
		enable_dcache = 1; 	
#if 0
      cache_dl1 = cache_create(name, nsets, bsize, /* balloc */FALSE,
			       /* usize */0, assoc, cache_char2policy(c),
			       dl1_access_fn, /* hit lat */cache_dl1_lat);
#endif
    /* is the level 2 D-cache defined? */
    if (!mystricmp(cache_dl2_opt, "none"))
		  enable_ul2cache = 0;
#if 0
		  cache_dl2 = NULL;
#endif
    else
	 {
	   if (sscanf(cache_dl2_opt, "%[^:]:%d:%d:%d:%c",
		     name, &nsets_l2, &bsize_l2, &assoc_l2, &c) != 5)
	     fatal("bad l2 D-cache parms: "
		  "<name>:<nsets>:<bsize>:<assoc>:<repl>");

	   enable_ul2cache = 1;
	 }
  } 	 
#if 0
	  cache_dl2 = cache_create(name, nsets, bsize, /* balloc */FALSE,
				   /* usize */0, assoc, cache_char2policy(c),
				   dl2_access_fn, /* hit lat */cache_dl2_lat);
	}
    }
#endif

  /* use a level 1 I-cache? */
  if (!mystricmp(cache_il1_opt, "none"))
    {
      enable_icache = 0;
#if 0
      cache_il1 = NULL;
      /* the level 2 I-cache cannot be defined */
      if (strcmp(cache_il2_opt, "none"))
	fatal("the l1 inst cache must defined if the l2 cache is defined");
      cache_il2 = NULL;
#endif
    }
#if 0
  else if (!mystricmp(cache_il1_opt, "dl1"))
    {
      if (!cache_dl1)
	fatal("I-cache l1 cannot access D-cache l1 as it's undefined");
      cache_il1 = cache_dl1;

      /* the level 2 I-cache cannot be defined */
      if (strcmp(cache_il2_opt, "none"))
	fatal("the l1 inst cache must defined if the l2 cache is defined");
      cache_il2 = NULL;
    }
  else if (!mystricmp(cache_il1_opt, "dl2"))
    {
      if (!cache_dl2)
	fatal("I-cache l1 cannot access D-cache l2 as it's undefined");

      /* the level 2 I-cache cannot be defined */
      if (strcmp(cache_il2_opt, "none"))
	fatal("the l1 inst cache must defined if the l2 cache is defined");
      cache_il2 = NULL;
    }
#endif
  else /* il1 is defined */
    {
      if (sscanf(cache_il1_opt, "%[^:]:%d:%d:%d:%c",
				name, &nsets, &bsize, &assoc, &c) != 5)
		fatal("bad l1 I-cache parms: <name>:<nsets>:<bsize>:<assoc>:<repl>");
#if 0
      cache_il1 = cache_create(name, nsets, bsize, /* balloc */FALSE,
			       /* usize */0, assoc, cache_char2policy(c),
			       il1_access_fn, /* hit lat */cache_il1_lat);

      /* is the level 2 I-cache defined? */
      if (!mystricmp(cache_il2_opt, "none"))
		  cache_il2 = NULL;
#endif
   if (!mystricmp(cache_il2_opt, "none"))
	  enable_il2cache = 0;	  
	else if (!mystricmp(cache_il2_opt, "dl2"))
	{
	  enable_ul2cache = 1;
#if 0
	  if (!cache_dl2)
	    fatal("I-cache l2 cannot access D-cache l2 as it's undefined");
	  cache_il2 = cache_dl2;
#endif
	}
      else
	{
	  enable_il2cache = 1;	  
	  if (sscanf(cache_il2_opt, "%[^:]:%d:%d:%d:%c",
		     name, &nsets_l2, &bsize_l2, &assoc_l2, &c) != 5)
	    fatal("bad l2 I-cache parms: "
		  "<name>:<nsets>:<bsize>:<assoc>:<repl>");

#if 0
	  cache_il2 = cache_create(name, nsets, bsize, /* balloc */FALSE,
				   /* usize */0, assoc, cache_char2policy(c),
				   il2_access_fn, /* hit lat */cache_il2_lat);
#endif

	}
   
	enable_icache = 1;
  
  }

#if 0
  /* use an I-TLB? */
  if (!mystricmp(itlb_opt, "none"))
    itlb = NULL;
  else
    {
      if (sscanf(itlb_opt, "%[^:]:%d:%d:%d:%c",
		 name, &nsets, &bsize, &assoc, &c) != 5)
	fatal("bad TLB parms: <name>:<nsets>:<page_size>:<assoc>:<repl>");
      itlb = cache_create(name, nsets, bsize, /* balloc */FALSE,
			  /* usize */sizeof(md_addr_t), assoc,
			  cache_char2policy(c), itlb_access_fn,
			  /* hit latency */1);
    }

  /* use a D-TLB? */
  if (!mystricmp(dtlb_opt, "none"))
    dtlb = NULL;
  else
    {
      if (sscanf(dtlb_opt, "%[^:]:%d:%d:%d:%c",
		 name, &nsets, &bsize, &assoc, &c) != 5)
	fatal("bad TLB parms: <name>:<nsets>:<page_size>:<assoc>:<repl>");
      dtlb = cache_create(name, nsets, bsize, /* balloc */FALSE,
			  /* usize */sizeof(md_addr_t), assoc,
			  cache_char2policy(c), dtlb_access_fn,
			  /* hit latency */1);
    }

  if (cache_dl1_lat < 1)
    fatal("l1 data cache latency must be greater than zero");

  if (cache_dl2_lat < 1)
    fatal("l2 data cache latency must be greater than zero");

  if (cache_il1_lat < 1)
    fatal("l1 instruction cache latency must be greater than zero");

  if (cache_il2_lat < 1)
    fatal("l2 instruction cache latency must be greater than zero");

  if (mem_nelt != 2)
    fatal("bad memory access latency (<first_chunk> <inter_chunk>)");
#endif

  if (mem_lat[0] < 1 || mem_lat[1] < 1)
    fatal("all memory access latencies must be greater than zero");
  if (enable_icache)
    set_cache_basic(nsets, assoc, bsize, mem_lat[0]);

#ifdef _DEBUG
	printf("nsets_dl1 = %d , bsize_dl1 = %d, assoc_dl1 = %d\n", 
		  nsets_dl1, bsize_dl1, assoc_dl1);	  
	printf("nsets_l2 = %d , bsize_l2 = %d, assoc_l2 = %d\n", 
		  nsets_l2, bsize_l2, assoc_l2);	  
#endif

#if 0
  if (mem_bus_width < 1 || (mem_bus_width & (mem_bus_width-1)) != 0)
    fatal("memory bus width must be positive non-zero and a power of two");

  if (tlb_miss_lat < 1)
    fatal("TLB miss latency must be greater than zero");

  if (res_ialu < 1)
    fatal("number of integer ALU's must be greater than zero");
  if (res_ialu > MAX_INSTS_PER_CLASS)
    fatal("number of integer ALU's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_IALU_INDEX].quantity = res_ialu;
  
  if (res_imult < 1)
    fatal("number of integer multiplier/dividers must be greater than zero");
  if (res_imult > MAX_INSTS_PER_CLASS)
    fatal("number of integer mult/div's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_IMULT_INDEX].quantity = res_imult;
  
  if (res_memport < 1)
    fatal("number of memory system ports must be greater than zero");
  if (res_memport > MAX_INSTS_PER_CLASS)
    fatal("number of memory system ports must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_MEMPORT_INDEX].quantity = res_memport;
  
  if (res_fpalu < 1)
    fatal("number of floating point ALU's must be greater than zero");
  if (res_fpalu > MAX_INSTS_PER_CLASS)
    fatal("number of floating point ALU's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_FPALU_INDEX].quantity = res_fpalu;
  
  if (res_fpmult < 1)
    fatal("number of floating point multiplier/dividers must be > zero");
  if (res_fpmult > MAX_INSTS_PER_CLASS)
    fatal("number of FP mult/div's must be <= MAX_INSTS_PER_CLASS");
  fu_config[FU_FPMULT_INDEX].quantity = res_fpmult;
#endif
}



int
read_opt_ss(int argc, char **argv)
{
  char *s;

  /* register global options */
  sim_odb = opt_new(orphan_fn);

  /* register all simulator-specific options */
  sim_reg_options(sim_odb);

  /* parse simulator options */
  opt_process_options(sim_odb, argc, argv);

  /* check simulator-specific options */
  sim_check_options(sim_odb, argc, argv);

  //opt_print_options(sim_odb, stdout, /* long */FALSE, /* notes */TRUE);

  return 0;
}
