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
 * $Id: main.c,v 1.3 2006/07/15 03:22:50 lixianfe Exp $
 *
 ******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"
#include "bpred.h"
#include "cache.h"
#include "address.h"

char DEBUG_INFEAS = 0;

int PROGRESS_STEP = 10000;
extern int num_tcfg_edges;

extern char *run_opt;
extern FILE *filp, *fusr;

int	    bpred_scheme;
int	    enable_icache = 1;
int     enable_scp_dcache = 1;      /* Enable scope-aware dcache analysis*/
int 	enable_dcache = 0;			/* For anabling dcache analysis */
int 	enable_il2cache = 0;		/* For enabling level 2 icache analysis */	
int 	enable_ul2cache = 0;		/* For enabling level 2 ucache analysis */	
int 	enable_abs_inst_cache = 0;	/* For anabling abstract icache analysis */
prog_t	    prog;


// vivy: infeasible path analysis
#include "infeasible.h"
char enable_infeas;

// vivy: marker for procedures to include in estimation
char *include_proc;

/* sudiptac:: For performance measurement */
int findproc( int addr ) {
  int i;
  for( i = 0; i < prog.num_procs; i++ )
    if( prog.procs[i].sa == addr )
      return i;
  return -1;
}

// vivy: read list of functions to include in analysis
// Some analysis steps are expensive so avoid processing unnecessary functions.
int read_functions( char *obj_file ) {
    FILE *fptr;
    char fname[80];

    int id;
    int addr;
    char name[80];

    include_proc = (char*) calloc( prog.num_procs, sizeof(char) );

    sprintf( fname, "%s.fnlist", obj_file );
    fptr = fopen( fname, "r" );

    if( !fptr ) {
        if( DEBUG_INFEAS )
            printf( "%s.fnlist not found -- all procedures will be included in estimation.\n", obj_file );
        for( id = 0; id < prog.num_procs; id++ )
            include_proc[id] = 1;
        return -1;
    }

    while( fscanf( fptr, "%x %s", &addr, name ) != EOF ) {
        id = findproc( addr );
        if( id != -1 )
            include_proc[id] = 1;
        else
            printf( "Warning: procedure [0x%x] %s not found.\n", addr, name );
    }
    fclose( fptr );
    return 0;
}


// program flow analysis to construct control flow graphs from objective code
static void
path_analysis(char *fName)
{
    char obj_file[256];
    // read object code, decode it
    strcpy(obj_file,fName);
    read_code(obj_file);

    // create procs and their CFGs from the decoded text
    build_cfgs();

    // vivy: read list of functions to include in estimation
    // do this after prog.procs are established
    strcpy(obj_file,fName);
    read_functions(obj_file);

    // transform the CFGs into a global CFG called tcfg (transformed-cfg)
    prog_tran();

    // identify loop levels as well as block-loop mapping
    loop_process();

    /* vivy: infeasible path analysis */
    if( enable_infeas ) {
      strcpy(obj_file,fName);
      infeas_analysis(obj_file);
    }
}



static void
microarch_modeling()
{
    if (bpred_scheme != NO_BPRED)
	bpred_analysis();
    if (enable_icache)
	cache_analysis();
    pipe_analysis();
}


static void
do_ilp(char *obj_file)
{
    int	    exit_val;
    char    s[256];

    //printf("do_ilp...\n");
    sprintf(s, "%s.lp", obj_file);
    filp = fopen(s, "w");
    sprintf(s, "%s.cons", obj_file);
    fusr = fopen(s, "r");
    if ((filp == NULL) || (fusr == NULL)) {
	fprintf(stderr, "fail to open ILP/CONS files for writing/reading\n");
	exit(1);
    }
    constraints();

    fclose(filp);
    fclose(fusr);

    /* vivy: print a cplex version */
    sprintf( s, "%s.ilp", obj_file );
    filp = fopen( s, "w" );
    fprintf( filp, "enter Q\n" );
    fclose( filp );

    // same with lp_solve format but no comment supported
    // sprintf( s, "sed '/\\\\/d' %s.lp >> %s.ilp", obj_file, obj_file );
    // EDIT: cplex supports the same comment format as lp_solve
    sprintf( s, "cat %s.lp >> %s.ilp", obj_file, obj_file );
    system( s );

    sprintf( s, "%s.ilp", obj_file );
    filp = fopen( s, "a" );
    fprintf( filp, "optimize\n" );
    fprintf( filp, "set logfile %s.sol\n", obj_file );
    fprintf( filp, "display solution objective\n" );
    fprintf( filp, "display solution variables -\n" );
    fprintf( filp, "quit\n" );
    fclose( filp );

    /* Command:
     * rm -f %s.sol; cplex < %s.ilp >/dev/null 2>/dev/null; cat %s.sol | sed '/^/s/Obj/obj/'
     */
}



static void
run_est(char *obj_file)
{
    microarch_modeling();
    do_ilp(obj_file);
}



static void
run_cfg(char *obj_file)
{
    int	    i;
    char    s[128];
    FILE    *fcfg;

    sprintf(s, "%s.cfg", obj_file);
    //printf("dumping control flow graphs to file:%s\n", s);
    fcfg = fopen(s, "w");
    if (fcfg == NULL) {
	fprintf(stderr, "fail to create file: %s.cfg\n", s);
	exit(1);
    }

    for (i=0; i<prog.num_procs; i++) {
	dump_cfg(fcfg, &prog.procs[i]);
    }
    fclose(fcfg);

    //printf("done.\n");
}


extern int fetch_width;

/* modification to indirect jump */
/* liangyun */

typedef struct jptb{
        addr_t adr;
        int    ntarget;
        addr_t *ptarget;
        int    index;
}jptb;

jptb * pjptb;
int    bjptb = 0;
int    njp;

int lookup_jptable(addr_t adr){
    int i;
    int num = 0;
    for(i = 0; i < njp; i++){
        if(pjptb[i].adr == adr ){
           num = pjptb[i].ntarget;
        }
    }
    return num;        
}

void get_jptable(addr_t src,int index, addr_t * target){
     int i;
     for(i = 0; i < njp; i++){
         if(pjptb[i].adr == src){
             *target = pjptb[i].ptarget[index];
             break;
          }
     } 
     assert(i < njp);
}

int get_jptable_static(addr_t src, addr_t * target){
     int i,j;
     int tail = -1;
     for(i = 0; i < njp; i++){
         if(pjptb[i].adr == src){
             j = pjptb[i].index;
             *target = pjptb[i].ptarget[j];
             pjptb[i].index++;
             if(pjptb[i].index == pjptb[i].ntarget)
                   tail = 0;
             else  
                   tail = 1;
             break;
          }
     } 
     //assert(i < njp);
     return tail;
}


void read_injp(char * objfile){
     int  tablesize,i,j;
     char file[100];
     FILE *ftable;
     sprintf(file,"%s.jtable",objfile);
     ftable = fopen(file,"r");
     if(!ftable){
        // no indirect jump
        bjptb = 0;
     }else{
        bjptb = 1;
        fscanf(ftable,"%d",&tablesize);
        njp = tablesize;
        pjptb = (jptb *)calloc(tablesize, sizeof(jptb));
        for(i = 0; i < tablesize; i++){
            fscanf(ftable,"%08x",&pjptb[i].adr);
            fscanf(ftable,"%d",&pjptb[i].ntarget);
            pjptb[i].index = 0;
            pjptb[i].ptarget = (addr_t *)calloc(pjptb[i].ntarget, sizeof(addr_t));
            for(j = 0; j < pjptb[i].ntarget; j++)
               fscanf(ftable,"%08x",&pjptb[i].ptarget[j]);
        }
        fclose(ftable);
     }
     //printf("bool: %d\n",bjptb);
}

int    *pdepth;
int     bdepth = 0;

int  test_depth(int pid, int depth){
     if (depth < pdepth[pid])
         return 1;
     else
         return 0;
}

void read_recursive(char * objfile){
     char file[100];
     FILE *ftable;
     int size,i;
     sprintf(file,"%s.recursive",objfile);
     ftable = fopen(file,"r");
     if(!ftable){
         bdepth = 0;
     }else{
         bdepth = 1;
         fscanf(ftable,"%d",&size);
         pdepth = (int *)calloc(size,sizeof(pdepth));
         for(i = 0; i < size; i++)
            fscanf(ftable,"%d",&pdepth[i]);
         fclose(ftable);
    }
}

/* HBK: scope-aware data cache analysis */
int X,Y,B,l1,l2;
static void scp_aware_datacache_analysis(char *bin_fname) {
    printf("\nADDRESS ANALYSIS: %s\n",bin_fname);fflush(stdout);
    ticks a,b;

    X = 2; Y = 32; B = 32; l1 = 6; l2 = 0;//no L2 cache	  
    a = getticks();
    classified_address_analysis(bin_fname);
    b = getticks();
	printf("\n===================================================\n");
	printf("Address analysis time = %lf secs\n", (b - a)/((1.0) * CPU_MHZ));
	printf("===================================================\n");

    a = getticks();
    //set cache config

    //call Sudipta's instruction cache?
	enable_icache = 1;
    enable_abs_inst_cache = 0;

    enable_infeas = 0; //NOTE: need to repair Vivy's AB BB infeasible detection
    enable_dcache = 0;
    printf("\nCACHE ANALYSIS: %s\n",bin_fname);fflush(stdout);
    mpa_datacache();
    b = getticks();
	printf("\n===================================================\n");
	printf("Cache analysis time = %lf secs\n", (b - a)/((1.0) * CPU_MHZ));
	printf("===================================================\n");
}

/* sudiptac :::: analyze two level cache hierarchies */
static void analyze_cache_hierarchy()
{
	 ticks a,b;
		  
	 /* analyze memory addresses. This is needed for data cache analysis */
	 a = getticks();
	 analyze_address();
	 b = getticks();

	 printf("===================================================\n");
	 printf("Address computation time = %lf secs\n", (b - a)/((1.0) * CPU_MHZ));
	 printf("===================================================\n");
	 
	 a = getticks();	  
	 /* sudiptac : analyze the data cache */
	 if(enable_dcache)
		analyze_data_cache();

	 if(enable_ul2cache || enable_il2cache)
	 {
		  enable_icache = 0; 
		  enable_abs_inst_cache = 1;
		  /* sudiptac : analyze instruction cache (abstract interpretation approach) */
		  analyze_abs_instr_cache();
		  
		  if(enable_ul2cache)
			 /* analyze level 2 unified cache */
			 analyze_unified_cache(); 
	 }

	 b = getticks();	  
	 
	 printf("===================================================\n");
	 printf("Maximum cache analysis time = %lf secs\n", (b - a)/((1.0) * CPU_MHZ));
	 printf("===================================================\n");
}	  

int
main(int argc, char **argv){
    int dbg = 1;
    char fName[256],str[256];
    //fName = calloc(256,sizeof(char));str = calloc(256,sizeof(char));
    strcpy(fName,argv[argc-1]);
    if (dbg) {
        printf("\nFile name %s",fName);fflush(stdout);
        printf("\n***NOTICE: you need to manually inline all procedures for address analysis to work");
        printf("\n***NOTICE: for triangular loop, you need to create file <binary file name>.econ and set extra loop conditions to help Chronos recognize them"); 
        printf("\n Contrainst format: <type> L1_id L2_id k");
        printf("\n type = \"eql\" : L1's loop bound <= L2's iteration + k");
        printf("\n type = \"inv\" : L1's loop bound <= k - L2's iteration");
    }
    if (argc <= 1) {
		  fprintf(stderr, "Usage:\n");
		  fprintf(stderr, "%s <options> <benchmark>\n", argv[0]);
		  exit(1);
    }

    init_isa();
    
	 /* read options including (1) actions; (2) processor configuration */
    read_opt(argc, argv);
    
    /* liangyun: read jump table if necessary */   
    strcpy(str,fName);
    read_injp(str);

    /* liangyun: read depth table for recursive function */
    strcpy(str,fName);
    read_recursive(str);
 
    /* vivy: only these steps are needed to build CFG */
    if (strcmp(run_opt, "CFG") == 0) {
      strcpy(str,fName);
      read_code( str );
      build_cfgs();
      run_cfg( str );
      return 0;
    }

    enable_infeas = 1;

    strcpy(str,fName);
    path_analysis(str);

    strcpy(str,fName);
    if (enable_scp_dcache) {//scope-aware PS analysis for dcache
        scp_aware_datacache_analysis(fName);
    }
    else {//non-scope-aware unified cache analysis
	    /* sudiptac : a two level cache analysis */
	    analyze_cache_hierarchy();
    }

    run_est(argv[argc - 1]);
}
