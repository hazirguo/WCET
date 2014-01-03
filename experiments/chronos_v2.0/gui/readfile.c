
/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Copyright (C) 2006 Xianfeng Li
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
 * $Id: readfile.c,v 1.1 2005/07/10 15:00:53 liangyun  Exp $
 *
 * This file contains functions reading the benchmark executable.
 *
 ******************************************************************************/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "ecoff.h"
#include "readfile.h"
#include "symbol.h"


/* amount of tail padding added to all loaded text segments */
#define TEXT_TAIL_PADDING 128

/* program text (code) segment base */
md_addr_t	ld_text_base = 0;
unsigned	text_entry;
unsigned	text_offset;
;
/* program text (code) size in bytes */
unsigned int	ld_text_size = 0;

int		code_size;

md_addr_t	start_addr, end_addr, main_addr;
md_addr_t	*procs_addr = NULL; // start addresses of procedures
int		num_procs = 0;
extern struct   sym_sym_t **sym_textsyms;
extern int	sym_ntextsyms;


FILE   *fr; // recursive function 
int    nfr = 0; // the number of recursive function
char   frname[128];     
void update_fr(char * procname){
      if(!nfr){
          fr = fopen(frname, "w");
      }
      nfr++;
      fprintf(fr,"%s\n",procname);
}

void save_fr(){
     if(nfr)
        fclose(fr);
}

void read_symbol_table(char *fname, md_inst_t *code)
{
     FILE *fobj;
     FILE *fdis;
         
     struct ecoff_filehdr  fhdr;
     struct ecoff_aouthdr  ahdr;
     struct ecoff_symhdr_t symhdr;
     struct ecoff_fdr      *pfdr;       
     struct ecoff_fdr        fdr;
     struct ecoff_pdr      *ppdr;
     struct ecoff_pdr        pdr;
     struct ecoff_SYMR     *symb;
     char   **             files;     
                   
     int    nfile,nproc;
     int    i,j,k;
     int    ifd,ipd;
     int    len;
     char   *strtab;
     char   *strline;     
     char   disname[128];       
                                                  
     fobj = fopen(fname,"r");
     if (!fobj)
        fatal("cannot open executable `%s'", fname);
     
     sprintf(disname,"%s.dis",fname);
     fdis = fopen(disname,"w");
     if(!fdis)
        fatal("cannot open dis file"); 
      
     sprintf(frname,"%s.rec",fname);
    
         
    /* locate to symbol table */
     if (fread(&fhdr, sizeof(struct ecoff_filehdr), 1, fobj) < 1)
        fatal("cannot read header from executable `%s'", fname);

     /* record endian of target */
     if (fhdr.f_magic != ECOFF_EB_MAGIC && fhdr.f_magic != ECOFF_EL_MAGIC)
        fatal("bad magic number in executable `%s'", fname);
          
     if (fread(&ahdr, sizeof(struct ecoff_aouthdr), 1, fobj) < 1)
        fatal("cannot read AOUT header from executable `%s'", fname);

     /* seek to the beginning of the symbolic header */
     fseek(fobj, fhdr.f_symptr, 0);

     if (fread(&symhdr, sizeof(struct ecoff_symhdr_t), 1, fobj) < 1)
        fatal("could not read symbolic header from executable `%s'", fname);

     if (symhdr.magic != ECOFF_magicSym)
        fatal("bad magic number (0x%x) in symbolic header", symhdr.magic);
      
     nfile = symhdr.ifdMax;
     //printf("%d files \n",nfile);
     pfdr = (struct ecoff_fdr *) calloc(nfile,sizeof(struct ecoff_fdr));
     if(!pfdr)
        fatal("out of virtual memory");
            
     /*seek to the beginning of the file descriptor */
     fseek(fobj,symhdr.cbFdOffset,0);
     for(i = 0; i < nfile; i++){
        if(fread(&pfdr[i], sizeof(struct ecoff_fdr), 1, fobj) < 0)
          fatal("could not read file discriptor from executable '%s'",fname);
     }

     /* allocate space for the string table */
     len = symhdr.issMax + symhdr.issExtMax;
     strtab = (char *) calloc(len, sizeof(char));
     if(!strtab)
        fatal("out of virtual memory");

     /* read all the symbol names into memory */
     fseek(fobj, symhdr.cbSsOffset, 0);
     if(fread(strtab, len, 1, fobj) < 0)
        fatal("error while reading symbol table names");
     files = (char **)calloc(nfile,sizeof(char *));
     for(i = 0; i < nfile; i++){
         int str_offset;
         str_offset = pfdr[i].issBase + pfdr[i].rss;
         files[i] = mystrdup(&strtab[str_offset]);
         //printf("[%d] %s  adr %08x \n",i,files[i],pfdr[i].adr);
     }
     
     /* allocate space for the line numbers */
     len = symhdr.cbLine;
     strline = (char *) calloc(len,sizeof(char));
     if(!strline)
        fatal("out of virtual memory");
     /* read all the line numbers into memory */
     fseek(fobj,symhdr.cbLineOffset,0);
     if(fread(strline, len, 1, fobj) < 0 )
        fatal("error while reading line numbers");
     
     /* allocate space for the procedure discriptor */
     nproc = symhdr.ipdMax;
     //printf("Proc %d\n",nproc);
     ppdr = (struct ecoff_pdr *) calloc(nproc, sizeof(struct ecoff_pdr));
     if(!ppdr)
         fatal("out of virtual memory!");
     /* seek to the beginning of the procedure descriptor */
     fseek(fobj,symhdr.cbPdOffset,0);
     for(i = 0; i < nproc; i++){
        if(fread(&ppdr[i], sizeof(struct ecoff_pdr), 1, fobj) < 0)
          fatal("could not read proc discriptor from executable '%s'",fname);
     }
      
     /* allocate space for the local symbol */
     symb = (struct ecoff_SYMR *) calloc(symhdr.isymMax,sizeof(struct ecoff_SYMR));
     if(!symb)
        fatal("out of virtual memory ");  

     fseek(fobj,symhdr.cbSymOffset,0); 
     if(fread(symb,sizeof(struct ecoff_SYMR),symhdr.isymMax,fobj) < 0)
        fatal("error reading local symbol entries");
     
     /* 
       dump local symbol 
         for(i = 0; i < symhdr.isymMax; i++){
         int str_offset;
         char *sname;
         str_offset = symb[i].iss;
         switch(symb[i].st){
                case ECOFF_stProc:
                case ECOFF_stStaticProc:
                case ECOFF_stLabel:
                // from text segment 
                //sname = mystrdup(&strtab[str_offset]);
                printf("name %s offset \n",mystrdup(&strtab[str_offset]));
                break;
         }
     }*/

     /* 
         dump binary code 
         address: [start_addr,end_addr)
         end_addr: the starting address of library call
     */
     md_addr_t pc = start_addr;
     int       icode = 0; //code index;
     //fprintf(fdis,"start_addr: %08x end_addr: %08x\n",start_addr,end_addr);
     fprintf(fdis,"start_addr\n");
     for(i = 0; i < nfile; i++){
         ifd = i;
         /* initilization function */
         if(pfdr[ifd].adr < start_addr){
            continue;
         }
         
         /* library function */
         if(pfdr[ifd].adr >= end_addr)
            break;
         //fprintf(fdis,"start adr: %08x\n",pfdr[ifd].adr);
         ipd = pfdr[ifd].ipdFirst;
         
         /* 
           no line information, dump all the instructions 
         */
         if(pfdr[ifd].cbLine == 0 || ppdr[ipd].iline == -1){
                //printf("no procedure in file %d\n",ifd);
                md_addr_t addr = pfdr[ifd + 1].adr;
                md_addr_t offset = 0;
                while( pc < addr){
                      fprintf(fdis,"%08x ", pc);
                      fprintf(fdis,"<__start");
                      if(offset!=0)
                        fprintf(fdis,"+0x%x",offset);
                      fprintf(fdis,"> ");
                      md_print_insn(code[icode],pc,fdis);
                      decode_function(fdis,code[icode],pc," ");
                      fprintf(fdis,"\n");
                      pc += sizeof(md_inst_t);
                      offset += sizeof(md_inst_t);
                      icode++;
                }
                fprintf(fdis,"    ...\n");
                continue;
         }

        
         int START_FILE_OFFSET;
         int END_FILE_OFFSET;
         int START_PROC_OFFSET;
         int END_PROC_OFFSET;
         md_addr_t END_ADDR;
         
         START_FILE_OFFSET = pfdr[ifd].cbLineOffset;
         END_FILE_OFFSET   = START_FILE_OFFSET + pfdr[ifd].cbLine;

         for(j = 0; j < pfdr[ifd].cpd; j++){

            int sym_offset;
            char *procname;
            ipd = pfdr[ifd].ipdFirst + j;

            /* proc offset in local symbol table */
            sym_offset = pfdr[ifd].isymBase + ppdr[ipd].isym;
            /* get  proc name including static function */
            assert(symb[sym_offset].st == ECOFF_stStaticProc || symb[sym_offset].st == ECOFF_stProc);      
            procname = mystrdup(&strtab[symb[sym_offset].iss]);
            fprintf(fdis,"%s():\n",procname);
            
            
            START_PROC_OFFSET = START_FILE_OFFSET + ppdr[ipd].cbLineOffset;
            if(j != pfdr[ifd].cpd - 1){
               END_PROC_OFFSET   = START_FILE_OFFSET + ppdr[ipd + 1].cbLineOffset;
               END_ADDR = ppdr[ipd + 1].adr;
            }
            else{
               END_PROC_OFFSET   = END_FILE_OFFSET;
               END_ADDR = pfdr[ifd + 1].adr;
            }
   
            int count;
            unsigned int delta;
            int cur_line = ppdr[ipd].lnLow;
            int pre_line = -1;
            int brec;

            md_addr_t offset = 0;
            //printf("lowest source line %d\n",cur_line);
            for(k = START_PROC_OFFSET; k < END_PROC_OFFSET; k++){
                 count = ((strline[k] & 0x0F) + 1) / 2;
                 delta = (signed int)((strline[k] & 0xF0) >> 4);
                 //printf(" %08x ",strline[k]);
                 if(delta > 8)
                    delta = delta - 16;
                 if(delta == 8){
                     delta = ((strline[k + 1] << 8)|(0xFF & strline[k + 2]));
                     //printf("line %d:, %d %d %d\n",cur_line,strline[k+2],strline[k+1],delta);
                 
                     //if(delta >= 1 << 15)
                       // delta = delta - (1 << 16)
                     k += 2;
                }
                cur_line += delta;
                //printf(" delta %d count %d \n", delta,count);
                if(cur_line != pre_line){
                    fprintf(fdis,"%s:%d\n",files[ifd],cur_line);
                    pre_line = cur_line;
                }
                while(count > 0){
                      fprintf(fdis,"%08x ", pc);
                      fprintf(fdis,"<%s",procname);
                      if(offset!=0)
                        fprintf(fdis,"+0x%x",offset);
                      fprintf(fdis,"> ");
                      md_print_insn(code[icode],pc,fdis);
                      brec = decode_function(fdis,code[icode],pc,procname);
                      if(brec){
                         update_fr(procname);
                      }
                      fprintf(fdis,"\n");
                      pc += sizeof(md_inst_t);
                      offset += sizeof(md_inst_t);
                      count--;
                      icode++;
                }
            }
            /* print instruction of the last line */
            while(pc < END_ADDR){
                  fprintf(fdis,"%08x ", pc);
                  fprintf(fdis,"<%s",procname);
                  if(offset!=0)
                        fprintf(fdis,"+0x%x",offset);
                  fprintf(fdis,"> ");
                  md_print_insn(code[icode],pc,fdis);
                  brec = decode_function(fdis,code[icode],pc,procname);
                  if(brec){
                     update_fr(procname);
                  }
                  fprintf(fdis,"\n");
                  pc += sizeof(md_inst_t);
                  offset += sizeof(md_inst_t);
                  icode++;
            }
         }
     }
     fprintf(fdis,"end_addr\n");
     fclose(fdis);
}

/*   compare addr to symbol's addrs, 
      return <0 if less; =0 if equal; >0 if great
* /
static int cmp_adr(const void *m1, const void *m2)
{
    struct sym_sym_t *mi1 = (struct sym_sym_t *) m1;
    struct sym_sym_t *mi2 = (struct sym_sym_t *) m2;
    return (mi1->addr - mi2->addr);
}

/*
     jal "function"
     decode the function name from symbol table 
*/


int decode_function(FILE * fdis, md_inst_t inst, md_addr_t pc, char * procname){
     int i;
     char * jal = "jal";
     enum md_opcode  op; 
     MD_SET_OPCODE(op, inst);
     if(strcmp(jal,MD_OP_NAME(op)) == 0){
        /* jal */
        //struct sym_sym_t key, *res;
        md_addr_t addr;
        addr = ((pc & 036000000000) | (TARG << 2));
        /*
            qsort(sym_textsyms,sym_ntextsyms,sizeof(sym_textsyms[0]),cmp_adr);
            res = (struct sym_sym_t *) bsearch(&key, sym_textsyms, sym_ntextsyms, sizeof(sym_textsyms[0]),cmp_adr);
            assert(res != NULL);
        */
        for(i = 0; i < sym_ntextsyms; i++){
             if(sym_textsyms[i]->addr == addr)
                 break;
        }
        assert(i < sym_ntextsyms);
        fprintf(fdis, " <%s> ", sym_textsyms[i]->name);
        if(strcmp(sym_textsyms[i]->name, procname) == 0){
             return 1;
        }
     }
     return 0;
}
static void
read_text_head(char *fname)
{
    FILE		    *pf;
    long		    pos;
    struct ecoff_filehdr    fhdr;
    struct ecoff_aouthdr    ahdr;
    struct ecoff_scnhdr	    shdr;
    unsigned		    text_size;
    int			    i;
    enum md_opcode  op;

    pf = fopen(fname, "r");    
    if (pf == NULL) {
	fprintf(stderr, "%s:%d fail to open file: %s\n", __FILE__, __LINE__, fname);
	exit (1);
    }

    md_init_decoder();

    // locate the text section
    fseek(pf, 0, SEEK_SET);
    fread(&fhdr, sizeof fhdr, 1, pf);
    fread(&ahdr, sizeof ahdr, 1, pf);
    //printf("a.out text_start %08x\n",ahdr.text_start); 00400000

    for (i=0; i<fhdr.f_nscns; i++) {
	fread(&shdr, sizeof shdr, 1, pf);
	if (shdr.s_flags != ECOFF_STYP_TEXT)
	    continue;
	text_size = shdr.s_size;
	text_offset = shdr.s_scnptr;
	text_entry = shdr.s_vaddr;
        //printf("section %d size %08x, offset %08x entry %08x\n",i, text_size,text_offset,text_entry);
	ld_text_size = ((shdr.s_vaddr + shdr.s_size) - MD_TEXT_BASE) 
	    + TEXT_TAIL_PADDING;
    }

    ld_text_base = MD_TEXT_BASE;
}



// tell from the name of the function whether it is a library function, if so, it
// means no subsequent user functions
static int
is_lib_func(char *func_name)
{
    if ((strcmp(func_name, "__do_global_dtors") == 0)
	|| (strcmp(func_name, "__libc_init") == 0))
	return 1;
    else
	return 0;
}

static void
lookup_addr(char *fname)
{
    int			i, size = 0, max_num_procs = 16;
    struct sym_sym_t	*sym;

    sym_loadsyms(fname, 1);

    /*
         sym_textsyms[0]->addr is the starting address of the first 
         non static function
         start_addr = text section entry address including initilization codes
    */
    //start_addr = sym_textsyms[0]->addr;
    start_addr = text_entry; 

    text_offset += (start_addr - text_entry);
    /*
       get the last ending address (until to library call)
    */
    for (i=0; i < sym_ntextsyms; i++) {
	sym = sym_textsyms[i];
	if (is_lib_func(sym->name)){
	    end_addr = sym->addr;
        }
	if (strcmp(sym->name, "main") == 0)
	    main_addr = sym->addr;
    }
    code_size = end_addr - start_addr;
    //printf("start_addr %08x end_addr %08x \n",start_addr, end_addr);
}

int   nij = 0; //number of indirect jump
FILE *fij;
char obj[100];

void detect_ij(md_addr_t pc, md_inst_t inst){
     enum md_opcode op;
     /* decode the instruction */
     MD_SET_OPCODE(op,inst);
  
     if(MD_OP_FLAGS(op) & F_INDIRJMP){ //test if "jr"
         MD_OP_FORMAT(op);
         if(BS != 31){ //not the jr $31, 
            if(!nij){
                nij++;
                fij = fopen(obj,"w");
                fprintf(fij,"%08x ", pc);
                md_print_insn(inst,op,fij);
                fprintf(fij,"\n");
            }else{
                nij++;
                fprintf(fij,"%08x ", pc);
                md_print_insn(inst,op,fij);
                fprintf(fij,"\n");    
            }
         }
        
      }
}

void save_ij(){
     if(nij){
        //rewind(fij);
        //fprintf(fij,"%d\n",nij);
        fclose(fij);
     }
}

md_inst_t *
readcode(char *fname)
{   
    FILE	    *pf;
    md_inst_t	    *code, inst;
    enum md_opcode  op;
    int		    i, j;
    md_addr_t       pc;

    sprintf(obj,"%s.ir",fname);

    read_text_head(fname);
    lookup_addr(fname);

    pf = fopen(fname, "r");
    
    // alloc mem for the text to be read
    code = (md_inst_t *) malloc(code_size);
    if (code == NULL) {
	fprintf(stderr, "fail malloc!\n");
	exit(1);
    }

    // read the text
    pc = start_addr;
    fseek(pf, text_offset, SEEK_SET);
    for (i=0, j=0; i<code_size; i += sizeof(md_inst_t)) {
	fread(&inst, sizeof inst, 1, pf);
	MD_SET_OPCODE(op, inst);
	inst.a = (inst.a & ~0xff) | (unsigned int)MD_OP_ENUM(op);
	code[j++] = inst;
        /* dump instruction */
        /* printf("%08x ", pc);
           md_print_insn(inst,pc,stdout);
           printf("\n");
           pc += sizeof(md_inst_t);
        */  
        detect_ij(pc,inst);
        pc += sizeof(md_inst_t);
    }
    fclose(pf);
    read_symbol_table(fname,code);
    //sym_dumpsyms(stdout);
    //printf("text\n");
    //sym_dumptextsyms(stdout);
    
    save_ij();
    save_fr();
    return code;
}
