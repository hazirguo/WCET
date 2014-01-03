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
#include "cfg.h"
#include "bpred.h"
#include "cache.h"

extern char *run_opt;
extern FILE *filp, *fusr;

int	    bpred_scheme;
int	    enable_icache;
prog_t	    prog;


// program flow analysis to construct control flow graphs from objective code
static void
path_analysis(char *obj_file)
{
    // read object code, decode it
    read_code(obj_file);
    // create procs and their CFGs from the decoded text
    
    build_cfgs();
    // transform the CFGs into a global CFG called tcfg (transformed-cfg)
   
    prog_tran();
    // identify loop levels as well as block-loop mapping
   
    loop_process();
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

    sprintf( s, "sed '/\\\\/d' %s.lp >> %s.ilp", obj_file, obj_file );

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

     * rm -f %s.sol; cplex < %s.ilp >/dev/null 2>/dev/null; cat %s.sol | sed
'/^/s/Obj/obj/'

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

int
main(int argc, char **argv)
{
    if (argc <= 1) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s <options> <benchmark>\n", argv[0]);
	exit(1);
    }

    init_isa();
    // read options including (1) actions; (2) processor configuration
    read_opt(argc, argv);
    
    /* liangyun: read jump table if necessary */   
    read_injp(argv[argc - 1]);

    /* liangyun: read depth table for recursive function */
    read_recursive(argv[argc - 1]);
 
    path_analysis(argv[argc-1]);
    
    if (strcmp(run_opt, "CFG") == 0)
	run_cfg(argv[argc - 1]);
    else
	run_est(argv[argc - 1]);
}
