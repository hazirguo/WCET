/* Memory address analysis */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include "tcfg.h"
#include "address.h"
#include "loops.h"
#include "common.h"
#include "infeasible.h"

/* Transformed CFG */
extern tcfg_node_t** tcfg;
extern int num_tcfg_nodes;

/* Loop structures */
loop_t** loop_map;

/* The instruction set */
extern isa_t* isa;
static void dumpAddress(tcfg_node_t* bb);
static void updateSuccessorAloc(tcfg_node_t* bbi, int* change);
static int checkEquality(ric_p arg1, ric_p arg2);
FILE* aFile;

/* Analysis functions */
static void analyze_top();
static void analyze(tcfg_node_t* bbi);
static slist_p analyze_loop_top(loop_t* loop, int loop_id);
static void analyze_loop(tcfg_node_t* bbi, int loop_id);
static void joinMemoryAloc(abs_mem_p mem1, abs_mem_p* mem2);

/* Static variables used for analysis */
static int* analyzed;
static int* analyzed_loop;

/* Analysis count */
int count = 0;

static int isStoreInst(char* isa_name)
{
	if(! strcmp(isa_name, "sw") || !strcmp(isa_name, "sh") 
	   || !strcmp(isa_name, "sb") || !strcmp(isa_name, "swl")
		|| !strcmp(isa_name, "swr") || !strcmp(isa_name, "s.s")
		|| !strcmp(isa_name, "s.d"))
	
		return 1;  
	
	return 0;	
}

static int isLoadInst(char* isa_name)
{
	if(! strcmp(isa_name, "lw") || !strcmp(isa_name, "lh") 
		|| !strcmp(isa_name, "lb") || !strcmp(isa_name, "lwl")
		|| !strcmp(isa_name, "lwr") || !strcmp(isa_name, "lhu") 
		|| !strcmp(isa_name, "l.s") || !strcmp(isa_name, "l.d"))
		
		return 1;

	return 0;	
}

/* For getting set of addresses from base offset addressing mode */
ric_p getAddrBaseOffset(de_inst_t* inst, int base, int offset, int opt)
{
	ric_p* abs_reg	= inst->abs_reg;
	ric_p nAddr;

#ifdef _DEBUG
	if(!abs_reg)
	{
		printf("Some problem of abstraction at instruction %x\n", inst->addr);  
	}
#endif
	
	assert(abs_reg);

	if(opt && inst->mod_addr)
	{
#ifdef _DEBUG
		 printf("modified address = (0x%x,0x%x,0x%x)\n", inst->mod_addr->lower_bound, 
					 inst->mod_addr->upper_bound, inst->mod_addr->stride);
#endif
		 return inst->mod_addr; 
	}	 

	if(abs_reg[base])
	{
		 nAddr = (ric_p)malloc(sizeof(ric_s));
#ifdef _DEBUG
		 printf("Base register (%d) value(%x) = %d\n", base, inst->addr,
				abs_reg[base]->lower_bound);
#endif
		 nAddr->lower_bound = abs_reg[base]->lower_bound + offset;
		 nAddr->upper_bound = abs_reg[base]->upper_bound + offset;
		 
		 /* FIXME: HACK HACK HACK */
		 if(base == REG_GLOBAL)
			nAddr->stride = 0;		 
		 else
		  	nAddr->stride = abs_reg[base]->stride;
		 
		 return nAddr;
	}

	return NULL;
}

/* For getting set of addresses from base index addressing mode */
ric_p getAddrBaseIndex(de_inst_t* inst, int base, int index, int opt)
{
	ric_p* abs_reg	= inst->abs_reg;
	ric_p nAddr;

#ifdef _DEBUG
	if(!abs_reg)
	{
		printf("Some problem of abstraction at instruction %x\n", inst->addr);  
	}
#endif

	assert(abs_reg);

	if(opt && inst->mod_addr)
	{
#ifdef _DEBUG
		 printf("modified address = (0x%x,0x%x,0x%x)\n", inst->mod_addr->lower_bound, 
					 inst->mod_addr->upper_bound, inst->mod_addr->stride);
#endif
		 return inst->mod_addr; 
	}

	if(abs_reg[base] && abs_reg[index])
	{
		 nAddr = (ric_p)malloc(sizeof(ric_s));
		 nAddr->lower_bound = abs_reg[base]->lower_bound +
				abs_reg[index]->lower_bound;
		 nAddr->upper_bound = abs_reg[base]->upper_bound + 
				abs_reg[index]->upper_bound;
		 nAddr->stride = gcd(abs_reg[base]->stride, abs_reg[index]->stride);
		 return nAddr;
	}

	return NULL;
}

/* Debugging related function */
#ifdef _DEBUG
static void dump(de_inst_t* inst, ric_p addr, FILE* fp)
{
	fprintf(fp, "==========================================================\n");
	fprintf(fp, "Instruction name = %s, Address = %x, Immediate = %d\n", 
		  isa[inst->op_enum].name, inst->addr, inst->imm);	  
	fprintf(fp, "==========================================================\n");
	if(addr)
		fprintf(fp, "Address = (0x%x,0x%x,0x%x)\n", addr->lower_bound, addr->upper_bound,
		  addr->stride);
	else
		fprintf(fp, "UNKNOWN\n");  
}

/* Dump set of all addresses accessed */

static void dump_data_address()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;
	FILE* fp;

	fp = fopen("address.dump", "w");
	if(!fp)
		fatal("Internal file opening failed");  

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  		isa_name =  isa[inst->op_enum].name;

			  /* For a memory load/store instruction find out the 
			   * memory hit/miss classification from the abstract 
			   * cache state */
				if(isStoreInst(isa_name))
				{
					if(inst->num_in <= 2)
					{
					 	imm = inst->imm;
						base = *(inst->in + 1);
					 	addr = getAddrBaseOffset(inst, base, imm, 0);	 
						dump(inst, addr, fp);
					}	
					else if(inst->num_in == 3)
					{
						base = *(inst->in + 1);
						index = *(inst->in + 2);
						addr = getAddrBaseIndex(inst, base, index, 0);
						dump(inst, addr, fp);
					}
			  }	 
			  else if(isLoadInst(isa_name))
			  {
					if(inst->num_in <= 1)
					{
						imm = inst->imm;
		  				base = *(inst->in);
					 	addr = getAddrBaseOffset(inst, base, imm, 0);	 
						dump(inst, addr, fp);
					}	
					else if(inst->num_in == 2)
					{
						base = *(inst->in);
					   index = *(inst->in + 1);
					 	addr = getAddrBaseIndex(inst, base, index, 0);
						dump(inst, addr, fp);
					}
			 }
			 inst++;
		}
	}
	
	fclose(fp);

}

void dump_mod_data_address()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;
	FILE* fp;

	fp = fopen("address.dump", "a");
	if(!fp)
		fatal("Internal file opening failed");  

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
			 if(inst->mod_addr)
			 {
				 fprintf(fp, "modified address at 0x%x = (0x%x,0x%x,0x%x)\n",
					 inst->r_addr,
					 inst->mod_addr->lower_bound, 
					 inst->mod_addr->upper_bound, 
					 inst->mod_addr->stride);
			 }
			 inst++;
		 }
	}
	
	fclose(fp);
}

/* Dump the memory status at each instruction */
static void dumpAlocStatus(tcfg_node_t* bbi, FILE* fp) 
{
	ric_p* reg = bbi->bb->in_abs_reg_value;
	int num_regs;
	abs_mem_p iter;
	char l[20],u[20];

	fprintf(fp,"\n****Printing Input information to the basic block****\n");	  
	fprintf(fp,"*****Printing Abstract Register Values*****\n\n");
	fprintf(fp, "Basic block start address = 0x%x, End address = 0x%x\n", 
		  bbi->bb->sa, bbi->bb->sa + bbi->bb->size);	  
	for(num_regs = 0; num_regs < MAX_NO_REGISTERS; num_regs++)
	{	
		if(! reg[num_regs])
		 continue;
		fprintf(fp, "Register %d::\n", num_regs);
		if(reg[num_regs]->lower_bound == -INFINITY)
		  sprintf(l, "-inf");
		else  
		  sprintf(l, "%d", reg[num_regs]->lower_bound);
		if(reg[num_regs]->upper_bound == INFINITY)
		  sprintf(u, "inf");
		else  
		  sprintf(u, "%d", reg[num_regs]->upper_bound);
		fprintf(fp, "(%s,%s,%d)\n", l, u, reg[num_regs]->stride);
	}	  
	
	fprintf(fp,"\n*****End Printing Abstract Register Values*****\n\n");
	fprintf(fp,"\n*****Printing Abstract Memory Locations*****\n\n");
	
	for(iter = bbi->bb->in_abs_mem_value; iter; iter = iter->next)
	{
		fprintf(fp, "Address = %x\n", iter->inst_addr);  
		if(!iter->valid)
		{
		  fprintf(fp,"Not valid\n");
		  continue;
		}  
		fprintf(fp, "Address Range::");
		if(iter->addr->lower_bound == -INFINITY)
		  sprintf(l, "-inf");
		else  
		  sprintf(l, "%d", iter->addr->lower_bound);
		if(iter->addr->upper_bound == INFINITY)
		  sprintf(u, "inf");
		else  
		  sprintf(u, "%d", iter->addr->upper_bound);
		fprintf(fp, "(%s,%s,%d)  ", l, u, iter->addr->stride);
		fprintf(fp, "Value Range::");
		if(!iter->value)
		{
		  fprintf(fp, "(UNKNOWN)\n");
		  continue;
		}
		if(iter->value->lower_bound == -INFINITY)
		  sprintf(l, "-inf");
		else  
		  sprintf(l, "%d", iter->value->lower_bound);
		if(iter->value->upper_bound == INFINITY)
		  sprintf(u, "inf");
		else  
		  sprintf(u, "%d", iter->value->upper_bound);
		fprintf(fp, "(%s,%s,%d)\n", l, u, iter->value->stride);
	}
	
	fprintf(fp,"\n*****End Printing Abstract Memory Locations*****\n\n");
	
	reg = bbi->bb->out_abs_reg_value;
	
	fprintf(fp,"\n****Priniting Output information to the basic block****\n");	  
	fprintf(fp,"*****Printing Abstract Register Values*****\n\n");
	fprintf(fp, "Basic block start address = 0x%x, End address = 0x%x\n", 
		  bbi->bb->sa, bbi->bb->sa + bbi->bb->size);	  
	for(num_regs = 0; num_regs < MAX_NO_REGISTERS; num_regs++)
	{	
		if(! reg[num_regs])
		 continue;
		fprintf(fp, "Register %d::\n", num_regs);
		if(reg[num_regs]->lower_bound == -INFINITY)
		  sprintf(l, "-inf");
		else  
		  sprintf(l, "%d", reg[num_regs]->lower_bound);
		if(reg[num_regs]->upper_bound == INFINITY)
		  sprintf(u, "inf");
		else  
		  sprintf(u, "%d", reg[num_regs]->upper_bound);
		fprintf(fp, "(%s,%s,%d)\n", l, u, reg[num_regs]->stride);
	}	  
	
	fprintf(fp,"\n*****End Printing Abstract Register Values*****\n\n");
	fprintf(fp,"\n*****Printing Abstract Memory Locations*****\n\n");
	
	for(iter = bbi->bb->out_abs_mem_value; iter; iter = iter->next)
	{
		fprintf(fp, "Address = %x\n", iter->inst_addr);  
		if(!iter->valid)
		{
		  fprintf(fp,"Not valid\n");
		  continue;
		}  
		fprintf(fp, "Address Range::");
		if(iter->addr->lower_bound == -INFINITY)
		  sprintf(l, "-inf");
		else  
		  sprintf(l, "%d", iter->addr->lower_bound);
		if(iter->addr->upper_bound == INFINITY)
		  sprintf(u, "inf");
		else  
		  sprintf(u, "%d", iter->addr->upper_bound);
		fprintf(fp, "(%s,%s,%d)  ", l, u, iter->addr->stride);
		fprintf(fp, "Value Range::");
		if(!iter->value)
		{
		  fprintf(fp, "(UNKNOWN)\n");
		  continue;
		}
		if(iter->value->lower_bound == -INFINITY)
		  sprintf(l, "-inf");
		else  
		  sprintf(l, "%d", iter->value->lower_bound);
		if(iter->value->upper_bound == INFINITY)
		  sprintf(u, "inf");
		else  
		  sprintf(u, "%d", iter->value->upper_bound);
		fprintf(fp, "(%s,%s,%d)\n", l, u, iter->value->stride);
	}
	
	fprintf(fp,"\n*****End Printing Abstract Memory Locations*****\n\n");
}

/* Dump the address range calculated for different memory accesses
 * in a basic block */
static void dumpAddress(tcfg_node_t* bbi) 
{
	de_inst_t* inst;
	int i;
	
	if(! aFile)
		aFile = fopen("aloc.dump", "w");
	if(!aFile)
		fatal("Internal file opening failed");
	fprintf(aFile, "Printing abstract value of registers and memory locations\n");	  

	dumpAlocStatus(bbi, aFile);
}
#endif

/****************** END DEBUGGING RELATED FUNCTION ***************/

/* Get the loop bound for a particular loop */
/* FIXME: This function is rather fuzzy....better to choose a different 
 * implementation */
static int getLoopBound(loop_t* loop)
{
  /* FIXME: Temporary */
  tcfg_node_t* tail;
  tcfg_node_t* head;
  de_inst_t* cmp_inst;

  assert(loop);
  
  /* This is the last node of the loop */
  tail = loop->tail;
  head = loop->head;
 
  assert(tail);
		
  if(head->exec_count != -1)
  {
	  loop_t* parent = loop->parent;
	  
	  if(parent && parent->head->exec_count != -1)
		  return (head->exec_count / parent->head->exec_count);
	  else
		  return head->exec_count;
  }		 
  {
		  extern inf_loop_t* inf_loops;
		  inf_loop_t* lp;

		  lp = &(inf_loops[loop->id]);
		  assert(lp);
				
		  if(lp && lp->bound >= 0)
		  {
				
				printf("returning %d for loop tail %d\n", lp->bound, tail->id);	 

				return lp->bound;	 
		  }	
  }
  {
		printf("Loop bound not constant... provide a constraint... exiting\n");
		exit(-1);
  }	
}

/* Computes Greatest common divisor of two arguments */
int gcd ( int a, int b )
{
  int c;

  if(! a) return b;
  if(! b) return a;
  
  while ( a != 0 ) {
     c = a; a = b%a;  b = c;
  }
  return b;
}

/* Compute maximum of all the arguments given */
int maxof(int n_args, ...){
   register int i;
   int max, a;
   va_list ap;

   va_start(ap, n_args);
   max = va_arg(ap, int);
   
	for(i = 2; i <= n_args; i++) {
       if((a = va_arg(ap, int)) > max)
          max = a;
       }

   va_end(ap);
   
	return max;
}

/* Compute minimum of all the arguments given */
int minof(int n_args, ...){
   register int i;
   int min, a;
   va_list ap;

   va_start(ap, n_args);
   min = va_arg(ap, int);
   
	for(i = 2; i <= n_args; i++) {
       if((a = va_arg(ap, int)) < min)
          min = a;
       }

   va_end(ap);
   
	return min;
}

/* Create a range */
ric_p makeRIC(ric_p arg)
{
	ric_p nRIC;

	if(! arg)
		return NULL;  
	nRIC = (ric_p)malloc(sizeof(ric_s));
	if(! nRIC)
		fatal("Out of memory");
	nRIC->lower_bound = arg->lower_bound;	
	nRIC->upper_bound = arg->upper_bound;	
	nRIC->stride = arg->stride;	

	return nRIC;
}

/* Make one ric per instruction (allocate memory) */
static void makeRICPerInstruction(de_inst_t* inst, ric_p* abs_reg)
{
	int i;

	if(! inst->abs_reg)
	{
		inst->abs_reg = (ric_p *)malloc(MAX_NO_REGISTERS * sizeof(ric_p));
		if(! inst->abs_reg)
		  fatal("Out of memory");
		memset(inst->abs_reg, 0, MAX_NO_REGISTERS * sizeof(ric_p));
	}
	for(i = 0; i < MAX_NO_REGISTERS; i++)
	{
		if(! abs_reg[i])
		  continue;
		if(! inst->abs_reg[i])
		{
		  inst->abs_reg[i] = (ric_p)malloc(sizeof(ric_s));
		  if(! inst->abs_reg[i])
			 fatal("Out of memory");
		}
		inst->abs_reg[i]->lower_bound = abs_reg[i]->lower_bound;
		inst->abs_reg[i]->upper_bound = abs_reg[i]->upper_bound;
		inst->abs_reg[i]->stride = abs_reg[i]->stride;
	}
}

/* update RIC for multiply instruction (register-register addressing mode) */
static void updateRICMul(ric_p* hi, ric_p* lo, ric_p src1, ric_p src2)
{
	long long lower_bound, upper_bound;

	if(! (*hi))  
	  *hi = (ric_p)malloc(sizeof(ric_s));  
	if(! hi)  
	  fatal("Out of memory");
	if(! (*lo))  
	  *lo = (ric_p)malloc(sizeof(ric_s));  
	if(! lo)  
	  fatal("Out of memory");

	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1 && src2->stride != -1)
		{
		  lower_bound = minof(4, src1->lower_bound * src2->lower_bound,
					 src1->lower_bound * src2->upper_bound, 
					 src1->upper_bound * src2->upper_bound,
					 src1->upper_bound * src2->lower_bound);
		  upper_bound = maxof(4, src1->lower_bound * src2->lower_bound,
					 src1->lower_bound * src2->upper_bound, 
					 src1->upper_bound * src2->upper_bound,
					 src1->upper_bound * src2->lower_bound);
		  (*hi)->lower_bound = (int)(lower_bound >> 32);
		  (*hi)->upper_bound = (int)(upper_bound >> 32);
		  (*hi)->stride = 1;

		  if(! ((*hi)->upper_bound)) {
				(*lo)->upper_bound = (int)upper_bound;
				(*lo)->lower_bound = (int)lower_bound;
				(*lo)->stride = (int)(gcd(gcd(src1->stride * src2->stride, 
					 src1->lower_bound * src2->stride),
					 src2->lower_bound * src1->stride));  
		  } else
		  {
				(*lo)->upper_bound = 0;
				(*lo)->lower_bound = 0;
				(*lo)->stride = -1;	 
		  }
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*hi)->lower_bound = (*hi)->upper_bound = 0;
		  (*lo)->lower_bound = (*lo)->upper_bound = 0;
		  (*hi)->stride = -1;
		  (*lo)->stride = -1;
		}
	} else
	{
		  (*hi)->lower_bound = (*hi)->upper_bound = 0;
		  (*lo)->lower_bound = (*lo)->upper_bound = 0;
		  (*hi)->stride = -1;
		  (*lo)->stride = -1;
	}
}

/* update RIC for subtract operation (register-register addressing mode) */
static void updateRICSub(ric_p* dest, ric_p src1, ric_p src2)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1 && src2->stride != -1)
		{
		  (*dest)->lower_bound = src1->lower_bound - src2->upper_bound;  
		  (*dest)->upper_bound = src1->upper_bound - src2->lower_bound;  
		  (*dest)->stride = gcd(src1->stride, src2->stride);  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* update RIC for boolean operation (register-register addressing mode) */
static void updateRICBool(ric_p* dest, ric_p src1, ric_p src2)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1 && src2->stride != -1)
		{
		  if(src1->upper_bound < src2->lower_bound) 
		  	  (*dest)->lower_bound = (*dest)->upper_bound  = 1; 
		  else	  
		  	  (*dest)->lower_bound = (*dest)->upper_bound  = 0;  
		  (*dest)->stride = 0;  
		} else
		{
		  (*dest)->lower_bound = 0;
		  (*dest)->upper_bound = 1;
		  (*dest)->stride = 1;
		}
	} else
	{
		  (*dest)->lower_bound = 0;
		  (*dest)->upper_bound = 1;
		  (*dest)->stride = 1;
	}
}

/* update RIC for boolean checking operation (immediate addressing 
 * mode - unsigned) */

static void updateRICBoolU(ric_p* dest)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	(*dest)->lower_bound = 0;
	(*dest)->upper_bound = 1;
	(*dest)->stride = 1;
}

/* update RIC for boolean checking operation (immediate addressing 
 * mode) */
static void updateRICBoolImm(ric_p* dest, ric_p src1, int imm)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  if(src1->upper_bound < imm) 
		  	  (*dest)->lower_bound = (*dest)->upper_bound  = 1; 
		  else	  
		  	  (*dest)->lower_bound = (*dest)->upper_bound  = 0;  
		  (*dest)->stride = 0;  
		} else
		{
		  (*dest)->lower_bound = 0;
		  (*dest)->upper_bound = 1;
		  (*dest)->stride = 1;
		}
	} else
	{
		  (*dest)->lower_bound = 0;
		  (*dest)->upper_bound = 1;
		  (*dest)->stride = 1;
	}
}

/* update RIC for add instruction (register-register addressing mode) */
static void updateRICAdd(ric_p* dest, ric_p src1, ric_p src2)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1 && src2->stride != -1)
		{
			(*dest)->lower_bound = src1->lower_bound + src2->lower_bound;  
			(*dest)->upper_bound = src1->upper_bound + src2->upper_bound;  
		  /* FIXME :: Assumption , stride cannot be -INFINITY or +INFINITY
			*/	 
		  (*dest)->stride = gcd(src1->stride, src2->stride);  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* Perform join operation of two RIC-s (CHECK this) */
static ric_p updateRICJoin(ric_p src1, ric_p src2)
{
	ric_p result;
   int diff1, diff2, diff3, diff4;

	result = (ric_p)malloc(sizeof(ric_s));  
	
	if(! result)  
	  fatal("Out of memory");

	memset(result, 0, sizeof(ric_s));  

	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1 && src2->stride != -1)
		{
		  if(!src1->lower_bound && !src1->upper_bound && src1->stride) {
				result->lower_bound = src2->lower_bound;
				result->upper_bound = src2->upper_bound;
				result->stride = src2->stride;
		  } 
		  else if(!src2->lower_bound && !src2->upper_bound && src2->stride) {
				result->lower_bound = src1->lower_bound;
				result->upper_bound = src1->upper_bound;
				result->stride = src1->stride;
		  }
		  else  
		  {
				result->lower_bound = minof(2, src1->lower_bound, src2->lower_bound);  
				result->upper_bound = maxof(2, src1->upper_bound, src2->upper_bound);  
				/* This condition means both RIC are constant */
				if(! src1->stride && !src2->stride && 
					 result->lower_bound != result->upper_bound)
				  result->stride = abs(src1->lower_bound - src2->lower_bound);		 
				else {	 
				  diff1 = abs(src1->lower_bound - src2->lower_bound);
				  diff2 = abs(src1->lower_bound - src2->upper_bound);
		        diff3 = abs(src1->upper_bound - src2->lower_bound);
		        diff4 = abs(src1->upper_bound - src2->upper_bound);
		        result->stride = gcd(gcd(gcd(gcd(gcd(diff1, diff2),
							diff3), diff4), src1->stride), src2->stride);  
			  }
		  }
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  result->lower_bound = result->upper_bound = 0;
		  result->stride = -1;
		}
	} else if(src1 || src2)
	{
		  result->lower_bound = result->upper_bound = 0;
		  result->stride = -1;
	} else
		  return NULL;

	return result;
}

/* Load the value in memory */
ric_p returnMemoryAloc(ric_s mem, abs_mem_p abs_mem_st)
{
	abs_mem_p iter;
	ric_p result;
   int found = 0;
	ric_s t_addr;
	ric_p p_addr;

	result = (ric_p)malloc(sizeof(ric_s));  
	
	if(! result)  
	  fatal("Out of memory");

	t_addr.lower_bound = t_addr.upper_bound = 0;
	t_addr.stride = 1;	  
	p_addr = &t_addr;

	memset(result, 0, sizeof(ric_s)); 
	result->stride = 1;

	for(iter = abs_mem_st; iter; iter = iter->next) {
		if(!iter->valid)
		  continue;
		if(mem.lower_bound >= iter->addr->lower_bound && 
			  mem.upper_bound <= iter->addr->upper_bound  &&
				 mem.stride == iter->addr->stride && 
					 abs(mem.lower_bound - iter->addr->lower_bound) % mem.stride == 0) {
			  found = 1;  	 	
			  result = updateRICJoin(result, iter->value);		 
		}
		else if(mem.lower_bound <= iter->addr->lower_bound && 
		  mem.upper_bound >= iter->addr->upper_bound  &&
		  mem.stride == iter->addr->stride && 
		  abs(mem.lower_bound - iter->addr->lower_bound) % mem.stride == 0) {
			  
			  p_addr = updateRICJoin(p_addr, iter->addr);
			  result = updateRICJoin(result, iter->value);		 
		}
	}

	if(!found && !checkEquality(p_addr, &mem)) {
		
		/* Return top element (i.e., unknown) if memory store is not found */
		result->lower_bound = result->upper_bound = 0;
		result->stride = -1;
	}

	return result;
}

/* update RIC for a load instruction (base index addressing mode) */
static void updateRICLoadIndex(ric_p* dest, ric_p src1, ric_p src2,
		  abs_mem_p abs_mem_st, int size)
{
	ric_p mem_aloc;

	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");

	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  ric_s temp;
		  free(*dest);
		  temp.lower_bound = src1->lower_bound + src2->lower_bound;
		  temp.upper_bound = src1->upper_bound + src2->upper_bound;
		  temp.stride = size;
		  *dest = returnMemoryAloc(temp, abs_mem_st);
		} else 
		{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else 
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	} 
}

/* update RIC for a load instruction */
static void updateRICLoad(ric_p* dest, ric_p src1, int imm, abs_mem_p abs_mem_st,
		  int size)
{
	ric_p mem_aloc;

	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");

	if(src1)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  ric_s temp;
		  free(*dest);
		  temp.lower_bound = src1->lower_bound + imm;
		  temp.upper_bound = src1->upper_bound + imm;
		  temp.stride = size;
		  *dest = returnMemoryAloc(temp, abs_mem_st);
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* Allocate memory for aloc abstraction of a memory component 
 * (register or memory location) */
static abs_mem_p createMemoryAloc(ric_s addr, ric_p value)
{
		abs_mem_p nEntry = (abs_mem_p)malloc(sizeof(abs_mem_s));
		if(!nEntry)
		  fatal("Out of Memory");
		memset(nEntry, 0, sizeof(abs_mem_s));  
		nEntry->addr = (ric_p)malloc(sizeof(ric_s));  
		if(! nEntry->addr)
		  fatal("Out of Memory");
		nEntry->value = (ric_p)malloc(sizeof(ric_s));  
		if(! nEntry->value)
		  fatal("Out of Memory");
		nEntry->addr->lower_bound = addr.lower_bound;
		nEntry->addr->upper_bound = addr.upper_bound;
		nEntry->addr->stride = addr.stride;
		if(value)
		{
		  nEntry->value->lower_bound = value->lower_bound;
		  nEntry->value->upper_bound = value->upper_bound;
		  nEntry->value->stride = value->stride;
		}
		else
		{
		  nEntry->value->lower_bound = 0;
		  nEntry->value->upper_bound = 0;
		  nEntry->value->stride = -1;
		}
		nEntry->next = NULL;
		  
		return nEntry;
}

/* invalidate aloc abstraction of a memory operation as the 
 * values cannot be tracked anymore */
static void invalidateMemoryAloc(abs_mem_p abs_mem_st)
{
	abs_mem_p iter;
	
	/* Assign all memory location value as top */
	for(iter = abs_mem_st; iter; iter = iter->next) {
		if(!iter->valid || !iter->value)
		  continue;
		iter->value->lower_bound = iter->value->upper_bound = 0;
		iter->value->stride = -1;
	}
}

/* update aloc abstraction for a memory location */
static void updateMemoryAloc(ric_s mem, abs_mem_p* abs_mem_st, ric_p value, 
		  int addr)
{
	abs_mem_p iter;

	for(iter = *abs_mem_st; iter; iter = iter->next) {
		if(!iter->valid && !(iter->inst_addr == addr))  
		  continue;
		else if(iter->inst_addr == addr)
		{
			if(!iter->valid)		 
			{
				iter->valid = 1;
				iter->addr = makeRIC(&mem);
				iter->value = makeRIC(value);
			}
			else
			{
				iter->addr->lower_bound = mem.lower_bound;	 
				iter->addr->upper_bound = mem.upper_bound;	 
				iter->addr->stride = mem.stride;
				iter->value = updateRICJoin(value, iter->value);
			}
		}
		else if(mem.lower_bound >= iter->addr->lower_bound && 
			  mem.upper_bound <= iter->addr->upper_bound  &&
				 mem.stride == iter->addr->stride && 
					 abs(mem.lower_bound - iter->addr->lower_bound) % mem.stride == 0) {
		
					 iter->value = updateRICJoin(value, iter->value);		 
		} 
		else if((! iter->addr->lower_bound >= mem.upper_bound + mem.stride &&
				!mem.lower_bound >= iter->addr->upper_bound + iter->addr->stride)){
				
				iter->value->lower_bound = iter->value->upper_bound = 0;
				iter->value->stride = -1;
		} 	
		count++;
	}
}	

/* update RIC for store (base-indexed addressing mode) */
static void updateRICStoreIndex(ric_p value, ric_p base, ric_p index, 
		  abs_mem_p* abs_mem_st, int size, int addr)
{
	ric_p mem_aloc;

	if(base)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(base->stride != -1 && gcd(gcd(base->stride, index->stride), 
				size) == size)
		{
		  ric_s temp;
		  temp.lower_bound = base->lower_bound + index->lower_bound;
		  temp.upper_bound = base->upper_bound + index->upper_bound;
		  temp.stride = size;
		  updateMemoryAloc(temp, abs_mem_st, value, addr);
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  invalidateMemoryAloc(*abs_mem_st);
		}
	} else
	{
		  invalidateMemoryAloc(*abs_mem_st);
	}
}

/* update RIC for store instruction */
static void updateRICStore(ric_p value, ric_p base, int imm, abs_mem_p* abs_mem_st,
		  int size, int addr)
{
	ric_p mem_aloc;

	if(base)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(base->stride != -1 && gcd(base->stride, size) == size)
		{
		  ric_s temp;
		  temp.lower_bound = base->lower_bound + imm;
		  temp.upper_bound = base->upper_bound + imm;
		  temp.stride = size;
		  updateMemoryAloc(temp, abs_mem_st, value, addr);
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  invalidateMemoryAloc(*abs_mem_st);
		}
	} else
	{
		  invalidateMemoryAloc(*abs_mem_st);
	}
}

/* update RIC for right shift instruction (operand provided in register) */
static void updateRICRightShiftV(ric_p* dest, ric_p src1, ric_p src2)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1 && src2)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1 && src2->stride != -1)
		{
		  if(src1->upper_bound <= 0) {
				(*dest)->lower_bound = src1->lower_bound >> src2->lower_bound;  
				(*dest)->upper_bound = src1->upper_bound >> src2->upper_bound;  
		  } else if(src1->lower_bound <= 0 && src1->upper_bound > 0) {
				(*dest)->lower_bound = src1->lower_bound >> src2->lower_bound;  
				(*dest)->upper_bound = src1->upper_bound >> src2->lower_bound;  
		  } else if(src1->lower_bound > 0) {
				(*dest)->lower_bound = src1->lower_bound >> src2->upper_bound;  
				(*dest)->upper_bound = src1->upper_bound >> src2->lower_bound;  
		  }		
		  (*dest)->stride = 1;  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* update RIC for left shift instruction (immediate operand) */
static void updateRICLeftShift(ric_p* dest, ric_p src1, int imm)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  (*dest)->lower_bound = src1->lower_bound << imm;  
		  (*dest)->upper_bound = src1->upper_bound << imm;  
		  if(src1->stride != 0)
				(*dest)->stride = src1->stride << imm;  
		  else
				(*dest)->stride = src1->stride;  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* update RIC for right shift instruction (immediate operand) */
static void updateRICRightShift(ric_p* dest, ric_p src1, int imm)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  (*dest)->lower_bound = src1->lower_bound >> imm;  
		  (*dest)->upper_bound = src1->upper_bound >> imm;  
		  (*dest)->stride = src1->stride >> imm;  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* Update RIC for add instruction with immediate addressing mode */
static void updateRICAddImm(ric_p* dest, ric_p src1, int imm)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");
	if(src1)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  (*dest)->lower_bound = src1->lower_bound + imm;  
		  (*dest)->upper_bound = src1->upper_bound + imm;  
		  (*dest)->stride = src1->stride;  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* Update RIC for immediate addressing mode */
static void updateRICImm(ric_p* dest, int imm)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! dest)  
	  fatal("Out of memory");

	(*dest)->lower_bound = imm;  
	(*dest)->upper_bound = imm;  
	(*dest)->stride = 0;  
}

/* update the RIC */
static void updateRIC(ric_p* dest, ric_p src1)
{	
	if(! (*dest))  
	  *dest = (ric_p)malloc(sizeof(ric_s));  
	if(! *(dest))
	  fatal("Out of memory");

	if(src1)
	{ 
		/* If none of the sources are top element in the lattice */  
		if(src1->stride != -1)
		{
		  (*dest)->lower_bound = src1->lower_bound;  
		  (*dest)->upper_bound = src1->upper_bound;  
		  (*dest)->stride = src1->stride;  
		} else
		{
		  /* Otherwise make the conservative approximation that the 
			* destination is the top element. We represent [0,0,-1] as
			* the top element for implementation purpose as in all other
			* cases stride must be nonnegative*/
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
		}
	} 
	else
	{
		  (*dest)->lower_bound = (*dest)->upper_bound = 0;
		  (*dest)->stride = -1;
	}
}

/* This is the transfer function for the abstract location. This is 
 * carried out for basic instructions in the PISA */
static void transformAloc(tcfg_node_t* bbi, int* change)
{		  
	de_inst_t* inst;
	char* isa_name;
	int rs,rt,rd,imm;
	int n_inst;
	ric_p* abs_reg_value = NULL;
	abs_mem_p abs_mem_value = NULL;
	abs_mem_p iter;
	int i;

	assert(bbi);
	assert(bbi->bb);
	inst = bbi->bb->code;

	/* Check if any input is present. Otherwise allocate an initial
	 * memory */
	
	if(! bbi->bb->in_abs_reg_value) 
	{
		bbi->bb->in_abs_reg_value = (ric_p *)malloc(MAX_NO_REGISTERS 
		  * sizeof(ric_p));  
		if(! bbi->bb->in_abs_reg_value)
		  fatal("Out of memory");
		memset(bbi->bb->in_abs_reg_value, 0, MAX_NO_REGISTERS * 
		  sizeof(ric_p));	
		/* Initialize $zero register Value */
		bbi->bb->in_abs_reg_value[0] = (ric_p)malloc(sizeof(ric_s));  
		memset(bbi->bb->in_abs_reg_value[0], 0, sizeof(ric_s));	
		bbi->bb->in_abs_reg_value[28] = (ric_p)malloc(sizeof(ric_s));  
		CHECK_MEM(bbi->bb->in_abs_reg_value[28]);
		bbi->bb->in_abs_reg_value[28]->lower_bound = GLOBAL_START;
		bbi->bb->in_abs_reg_value[28]->upper_bound = GLOBAL_START;
		bbi->bb->in_abs_reg_value[28]->stride = 0;
	}
	
	if(! bbi->bb->out_abs_reg_value) 
	{
		bbi->bb->out_abs_reg_value = (ric_p *)malloc(MAX_NO_REGISTERS 
		  * sizeof(ric_p));  
	
		if(! bbi->bb->out_abs_reg_value)
		  fatal("Out of memory");
		memset(bbi->bb->out_abs_reg_value, 0, MAX_NO_REGISTERS * 
		  sizeof(ric_p));	
		/* Initialize $zero register Value */
		bbi->bb->out_abs_reg_value[0] = (ric_p)malloc(sizeof(ric_s));  
		memset(bbi->bb->out_abs_reg_value[0], 0, sizeof(ric_s));	
		bbi->bb->in_abs_reg_value[28] = (ric_p)malloc(sizeof(ric_s));  
		CHECK_MEM(bbi->bb->in_abs_reg_value[28]);
		bbi->bb->in_abs_reg_value[28]->lower_bound = GLOBAL_START;
		bbi->bb->in_abs_reg_value[28]->upper_bound = GLOBAL_START;
		bbi->bb->in_abs_reg_value[28]->stride = 0;
	}
	
	/* First copy input information to output */
	for(i = 0; i < MAX_NO_REGISTERS; i++)
	{
		 if(bbi->bb->in_abs_reg_value[i]) 
		   bbi->bb->out_abs_reg_value[i] =  makeRIC(bbi->bb->in_abs_reg_value[i]);
	}
	
	bbi->bb->out_abs_mem_value = NULL;
	
	for(iter = bbi->bb->in_abs_mem_value; iter; iter = iter->next)
	{
		 abs_mem_p nEntry = (abs_mem_p)malloc(sizeof(abs_mem_s));
		 memset(nEntry, 0, sizeof(abs_mem_s));
		 nEntry->inst_addr = iter->inst_addr;
		 nEntry->valid = iter->valid;
		 nEntry->addr = makeRIC(iter->addr);
		 nEntry->value = makeRIC(iter->value);
		 nEntry->next = bbi->bb->out_abs_mem_value;
		 bbi->bb->out_abs_mem_value = nEntry;
	}

	/* Get the register store of the basic block */	  
	abs_reg_value = bbi->bb->out_abs_reg_value;	  
	/* Get the memory store of the basic block */
	abs_mem_value = bbi->bb->out_abs_mem_value;

	for(n_inst = 0; n_inst < bbi->bb->num_inst; n_inst++)
	{
		isa_name = isa[inst->op_enum].name;	  

		/* make copy of abstract registers per instruction basis.
		 * This will be needed during data cache analysis */
		makeRICPerInstruction(inst, abs_reg_value);

		if(! strcmp(isa_name, "add") || ! strcmp(isa_name, "addu"))
		{
		  /* Get two input and one output register operands */
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		  rd = *(inst->out);
		  updateRICAdd(&abs_reg_value[rd], abs_reg_value[rs], 
					 abs_reg_value[rt]);
		} else if(! strcmp(isa_name, "addi") || ! strcmp(isa_name, "addiu"))
		{
		  /* Get two input and one output register operands */
		  rs = *(inst->in);
		  imm = inst->imm;
		  rd = *(inst->out);
		  updateRICAddImm(&abs_reg_value[rd], abs_reg_value[rs],
					 imm);
		} else if(! strcmp(isa_name, "sub") || ! strcmp(isa_name, "subu"))
		{
		  /* Get two input and one output register operands */
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		  rd = *(inst->out);
		  updateRICSub(&abs_reg_value[rd], abs_reg_value[rs],
					 abs_reg_value[rt]);
		} else if(! strcmp(isa_name, "mult")) 
		{
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		  updateRICMul(&abs_reg_value[HI], &abs_reg_value[LO], 
					 abs_reg_value[rs], abs_reg_value[rt]);
		} else if(! strcmp(isa_name, "multu")) {
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		} else if(! strcmp(isa_name, "div")) 
		{
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		} else if(! strcmp(isa_name, "divu")) {
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		} else if(! strcmp(isa_name, "mfhi")) { /* Move operations */
		  rd = *(inst->out);
		  updateRIC(&(abs_reg_value[rd]), abs_reg_value[HI]);
		} else if(! strcmp(isa_name, "mflo")) {
		  rd = *(inst->out);
		  updateRIC(&(abs_reg_value[rd]), abs_reg_value[LO]);
		} else if(! strcmp(isa_name, "mthi")) {
		  rs = *(inst->in);
		  updateRIC(&(abs_reg_value[HI]), abs_reg_value[rs]);
		} else if(! strcmp(isa_name, "mtlo")) {
		  rs = *(inst->in);
		  updateRIC(&(abs_reg_value[LO]), abs_reg_value[rs]);
		} else if(! strcmp(isa_name, "lui")) { /* Load upper immediate */
		  rt = *(inst->out);
		  imm = inst->imm;
		  updateRICImm(&(abs_reg_value[rt]), imm << 16);
		} else if (! strcmp(isa_name, "slt")) { /* Implicit set/reset operation */
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		  rd = *(inst->out);
		  updateRICBool(& (abs_reg_value[rd]), abs_reg_value[rs],
					 abs_reg_value[rt]);
		} else if (! strcmp(isa_name, "slti")) {
		  rs = *(inst->in);
		  imm = inst->imm;
		  rd = *(inst->out);
		  updateRICBoolImm(& (abs_reg_value[rd]), abs_reg_value[rs],
					 imm);
		} else if (! strcmp(isa_name, "sltu") || ! strcmp(isa_name, "sltiu")) {
		  rd = *(inst->out);
		  updateRICBoolU(& (abs_reg_value[rd]));
		} else if (! strcmp(isa_name, "sra") || !strcmp(isa_name, "srl")) { /* Shift operation */
		  rs = *(inst->in);
		  imm = inst->imm;
		  rd = *(inst->out);
		  updateRICRightShift(& (abs_reg_value[rd]), abs_reg_value[rs],
					 imm);
		} else if (! strcmp(isa_name, "srav")) { /* Shift operation */
		  rs = *(inst->in);
		  rt = *(inst->in + 1);
		  rd = *(inst->out);
		  updateRICRightShiftV(& (abs_reg_value[rd]), abs_reg_value[rs],
					 abs_reg_value[rt]);
		} else if(! strcmp(isa_name, "sll")) { /* Shift operation */
			 rs = *(inst->in);
			 rd = *(inst->out);
			 imm = inst->imm;
			 assert(imm >= 0);
			 updateRICLeftShift(&abs_reg_value[rd], abs_reg_value[rs], imm);
		} else if(! strcmp(isa_name, "lb"))	{ /* Load operations */
		  if(inst->num_in <= 1) {
			 rs = *(inst->in);
			 imm = inst->imm;
			 rd = *(inst->out);
			 updateRICLoad(& (abs_reg_value[rd]), abs_reg_value[rs],
					 imm, abs_mem_value, SIZE_OF_BYTE);			 
			} else if(inst->num_in == 2) {
			 rs = *(inst->in);
			 rt = *(inst->in + 1);
			 rd = *(inst->out);
			 updateRICLoadIndex(& (abs_reg_value[rd]), 
					 abs_reg_value[rs], abs_reg_value[rt],
					 abs_mem_value, SIZE_OF_BYTE);			 
			}
		} else if(! strcmp(isa_name, "lh"))	{ /* Load operations */
		  if(inst->num_in <= 1) {
			 rs = *(inst->in);
			 imm = inst->imm;
			 rd = *(inst->out);
			 updateRICLoad(& (abs_reg_value[rd]), abs_reg_value[rs],
					 imm, abs_mem_value, SIZE_OF_HALF_WORD);			 
			} else if(inst->num_in == 2) {
			 rs = *(inst->in);
			 rt = *(inst->in + 1);
			 rd = *(inst->out);
			 updateRICLoadIndex(& (abs_reg_value[rd]), 
					 abs_reg_value[rs], abs_reg_value[rt],
					 abs_mem_value, SIZE_OF_HALF_WORD);			 
			}
		} else if(! strcmp(isa_name, "lw")) 	{  /* Load operations */
		   if(inst->num_in <= 1) {
			 rs = *(inst->in);
			 imm = inst->imm;
			 rd = *(inst->out);
			 updateRICLoad(& (abs_reg_value[rd]), abs_reg_value[rs],
					 imm, abs_mem_value, SIZE_OF_WORD);			 
			} else if(inst->num_in == 2) {
			 rs = *(inst->in);
			 rt = *(inst->in + 1);
			 rd = *(inst->out);
			 updateRICLoadIndex(& (abs_reg_value[rd]), 
					 abs_reg_value[rs], abs_reg_value[rt],
					 abs_mem_value, SIZE_OF_WORD);			 
			}
		} else if(!strcmp(isa_name, "lwl") || !strcmp(isa_name, "lwr")
		  || !strcmp(isa_name, "swl") || !strcmp(isa_name, "swr") ||
		  !strcmp(isa_name, "lhu")) {
		    /* FIXME: Currently not implemented */
			 rs = *(inst->in);
			 imm = inst->imm;
			 rd = *(inst->out);
		    if(! abs_reg_value[rd])  
		  	  abs_reg_value[rd] = (ric_p)malloc(sizeof(ric_s));  
			 CHECK_MEM(abs_reg_value[rd]); 
			 /* Make destination register unknown element */
			 abs_reg_value[rd]->lower_bound = abs_reg_value[rd]->upper_bound = 0;
			 abs_reg_value[rd]->stride = -1;
		}
		else if(! strcmp(isa_name, "sb")) { /* Store operations */
		  if(inst->num_in <= 2) {
			 rs = *(inst->in);
			 imm = inst->imm;
			 rt = *(inst->in + 1);
			 updateRICStore(abs_reg_value[rs], abs_reg_value[rt],
					 imm, &abs_mem_value, SIZE_OF_BYTE, inst->addr);			 
			} else if(inst->num_in == 3) {
			 rs = *(inst->in);
			 rd = *(inst->in + 1);
			 rt = *(inst->in + 2);
			 updateRICStoreIndex(abs_reg_value[rs], abs_reg_value[rt],
					 abs_reg_value[rd], &abs_mem_value, SIZE_OF_BYTE, 
					 inst->addr);			 
			} 		 
		} else if(! strcmp(isa_name, "sh")) { /* Store operations */
		  if(inst->num_in <= 2) {
			 rs = *(inst->in);
			 imm = inst->imm;
			 rt = *(inst->in + 1);
			 updateRICStore(abs_reg_value[rs], abs_reg_value[rt],
					 imm, &abs_mem_value, SIZE_OF_HALF_WORD, inst->addr);			 
			} else if(inst->num_in == 3) {
			 rs = *(inst->in);
			 rd = *(inst->in + 1);
			 rt = *(inst->in + 2);
			 updateRICStoreIndex(abs_reg_value[rs], abs_reg_value[rt],
					 abs_reg_value[rd], &abs_mem_value, SIZE_OF_HALF_WORD,
					 inst->addr);			 
			} 		 
		} else if(! strcmp(isa_name, "sw"))  {  /* Store operations */
			 if(inst->num_in <= 2) {
				rs = *(inst->in);
				imm = inst->imm;
			   rt = *(inst->in + 1);
				updateRICStore(abs_reg_value[rs], abs_reg_value[rt],
					 imm, &abs_mem_value, SIZE_OF_WORD, inst->addr);			 
			 } else if(inst->num_in == 3) {
				rs = *(inst->in);
			   rd = *(inst->in + 1);
			   rt = *(inst->in + 2);
				updateRICStoreIndex(abs_reg_value[rs], abs_reg_value[rt],
					 abs_reg_value[rd], &abs_mem_value, SIZE_OF_WORD, 
					 inst->addr);			 
			} 		 
		}
		/* Double word load/store */
		else if(! strcmp(isa_name, "l.d") || ! strcmp(isa_name, "s.d") ||
					 !strcmp(isa_name, "l.s") || !strcmp(isa_name, "s.s")) {
		    /* FIXME: Currently not implemented */
			 rs = *(inst->in);
			 imm = inst->imm;
			 rd = *(inst->out);
		    if(! abs_reg_value[rd])  
		  	  abs_reg_value[rd] = (ric_p)malloc(sizeof(ric_s));  
			 CHECK_MEM(abs_reg_value[rd]); 
			 /* Make destination register unknown element */
			 /* abs_reg_value[rd]->lower_bound = abs_reg_value[rd]->upper_bound = 0;
			 abs_reg_value[rd]->stride = -1;*/
		}
		else if(! strcmp(isa_name, "beq")) { /* Control operations */
			rs = *(inst->in);
			rt = *(inst->in + 1);
			/* Perform filtering operation here */
		} else if(! strcmp(isa_name, "bne")) {
			rs = *(inst->in);
			rt = *(inst->in + 1);
			/* Perform filtering operation here */
		}
		else {
		  /* printf("Instruction %s not implemented returning top\n",
					 isa_name); */
		}
		inst++;
	}
	
	/* Update output memory information */
	bbi->bb->out_abs_mem_value = abs_mem_value;

#ifdef _DEBUG
	dumpAddress(bbi);  
#endif

	updateSuccessorAloc(bbi, change);
	bbi->anal_count++;
}

/* Check equality of two RIC */
static int checkEquality(ric_p arg1, ric_p arg2)
{
/*#define EQUAL(x,y) (((x)==(y)) || (((x) <= -INFINITY) && ((y) <= -INFINITY)) \
	|| (((x) >= INFINITY) && ((y) >= INFINITY)))*/
#define EQUAL(x,y) ((x)==(y))

	return !((arg1 && arg2 && EQUAL(arg1->lower_bound, arg2->lower_bound)
					 && EQUAL(arg1->upper_bound, arg2->upper_bound) 
					 && arg1->stride == arg2->stride) 
					 || (! arg1 && !arg2) 
					 );	  
}

/* Update information of abstract registers and memory locations 
 * from predecessors */
static void JoinPredAloc(tcfg_node_t* succ, tcfg_node_t* bbi, int* change_flag)
{
	int i;
	de_inst_t* l_inst, *prev_inst;

	/* Update Register Values. For loops perform widening also */	  
	if(! succ->bb->in_abs_reg_value) {
		*change_flag = 1;  
		l_inst = bbi->bb->code + bbi->bb->num_inst - 1;
		prev_inst = succ->bb->code - 1;
		
		succ->bb->in_abs_reg_value = (ric_p *)malloc(MAX_NO_REGISTERS * sizeof(ric_p));  
		if(! succ->bb->in_abs_reg_value)
		 fatal("Out of memory");
		
		memset(succ->bb->in_abs_reg_value, 0, MAX_NO_REGISTERS * sizeof(ric_p));	

		/* Initialize $zero register Value */
		succ->bb->in_abs_reg_value[0] = (ric_p)malloc(MAX_NO_REGISTERS * sizeof(ric_s));  
		memset(succ->bb->in_abs_reg_value[0], 0, sizeof(ric_s));	
		
		for(i = 1; i < MAX_NO_REGISTERS; i++)
		{
			if(!strcmp(isa[l_inst->op_enum].name, "jr") && 
				 !strcmp(isa[prev_inst->op_enum].name, "jal") && 
				 ((i >= REG_START && i <= REG_END) || (i == REG_STACK)))
			{
				 succ->bb->in_abs_reg_value[i] = makeRIC(prev_inst->abs_reg[i]);
			}
		   else if(bbi->bb->out_abs_reg_value[i])
		  		succ->bb->in_abs_reg_value[i] = makeRIC(bbi->bb->out_abs_reg_value[i]);  
		}
	} 
	else 
	{
		for(i = 1; i < MAX_NO_REGISTERS; i++)
		{
		  ric_p temp;

		  /* If it was a procedure call instruction then restore all 
			* callee saved registers and the stack pointer */
		  {
				l_inst = bbi->bb->code + bbi->bb->num_inst - 1;
				prev_inst = succ->bb->code - 1;
				if(!strcmp(isa[l_inst->op_enum].name, "jr") && 
					 !strcmp(isa[prev_inst->op_enum].name, "jal") && 
					 ((i >= REG_START && i <= REG_END) || (i == REG_STACK)))
				{
					 temp = makeRIC(prev_inst->abs_reg[i]);
				}
				else
				{
					temp = updateRICJoin(succ->bb->in_abs_reg_value[i],
						bbi->bb->out_abs_reg_value[i]);  
				}		
		  }
		  succ->bb->in_abs_reg_value[i] = temp;
		}
	}	
	/* Update Memory Store */
	if(! succ->bb->in_abs_mem_value)
	{
		 abs_mem_p iter;

		 for(iter = bbi->bb->out_abs_mem_value; iter; iter = iter->next)
		 {
			 abs_mem_p nEntry = createMemoryAloc(*(iter->addr), iter->value);	 
			 nEntry->next = succ->bb->in_abs_mem_value;
		    succ->bb->in_abs_mem_value = nEntry;
		 }
	}
	else
	{
		joinMemoryAloc(bbi->bb->out_abs_mem_value, &(succ->bb->in_abs_mem_value));
	}
}

static void updateSuccessorAloc(tcfg_node_t* bbi, int* change_flag)
{
	tcfg_edge_t* iter;
	int found = 0;

	for(iter = bbi->out; iter; iter = iter->next_out)
	{
		JoinPredAloc(iter->dst, bbi, change_flag);  
	}
}

/* Get all loop nodes from a loop id */
static slist_p getAllLoopNodes(int loop_id)
{
	slist_p head = NULL;
	loop_t* lparent;
	int i;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		if(!loop_map[i])
		  continue;
		
		if(loop_map[i]->id == loop_id) 
		{
			slist_p temp = (slist_p)malloc(sizeof(slist_s));
			temp->dst = (tcfg_node_t *)tcfg[i];
			temp->next = head;
			head = temp;
		}
		
		else
		{
		   lparent = loop_map[i]->parent;
			
			while(lparent && lparent->id != 0)
			{
				if(lparent->id == loop_id)
				{	 
					slist_p temp = (slist_p) malloc(sizeof(slist_s));
					temp->dst = (tcfg_node_t *)tcfg[i];
					temp->next = head;
					head = temp;
					break;
				}	
				
				lparent = lparent->parent;
			}	
		}
	}

	return head;
}

/* Perform a symbolic execution on transformed program control
 * flow graph */
static void analyze(tcfg_node_t* bbi)
{
	int i, j, n;
	slist_p l_nodes, iter;
	int change;

	/* Return if already analyzed */	
	if (analyzed[bbi->id] || !loop_map[bbi->id]) 
		return;

	i = bbi->id;	
		  
	/* If the current node is a node inside a loop */
	/* Since the whole program is considered to be a
	 * loop, we need to check the identity of the loop
	 * to be non-zero */
	if(loop_map[i]->id != 0)
	{
		/* get the loop bound */		 
		n = getLoopBound(loop_map[i]);	 
		
		for (j = 0; j < n; j++)
		{
			analyze_loop_top(loop_map[i], loop_map[i]->id);	 		 	 
		}
		
		l_nodes = getAllLoopNodes(loop_map[i]->id);
		
		/* Analysis of the loop is done. Mark all resident 
		 * nodes in the loop */
		for(iter = l_nodes; iter; iter = iter->next)
		{
			analyzed[((tcfg_node_t *)iter->dst)->id] = 1;		 
		}
	}	
	else 
	{
		 tcfg_edge_t* edge; 
		
		/* First analyze all the predecessors of the 
		 * nodes */ 		 
		 for(edge = bbi->in; edge; edge = edge->next_in)
		 {
			analyze(edge->src);	 
		 }
		 
		 /* Apply transfer functions to this basic block */
		 transformAloc(bbi, &change);
		 /* Mark the node to say that it has been analyzed */
		 analyzed[bbi->id] = 1;		 
	}
}

/* Analyze one loop */
static void analyze_loop(tcfg_node_t* iter, int loop_id)
{
	int n, i, change;

	/* If loop node is already analyzed then 
	 * return */	  
	if(analyzed_loop[iter->id])
		return;  

	if(loop_map[iter->id]->id != loop_id)
	{
		/* Get the loop bound */  
		n = getLoopBound(loop_map[iter->id]);
		
		/* Analyze the inner loop till its 
		 * loop iteration limit */
		for(i = 0; i < n; i++)
		{  
		  analyze_loop_top(loop_map[iter->id], 
					 loop_map[iter->id]->id);
		} 
	}
	else 
	{
		tcfg_edge_t* pred;

		/* Traverse all the predecessor nodes */ 
		for(pred = iter->in; pred; pred = pred->next_in)
		{
		  /* No need to analyze itself before it */
		   if(pred->src->id == iter->id || bbi_backedge(pred) || 
					 !loop_map[pred->src->id])
			{		 
		      if(!loop_map[pred->src->id])
				{
					analyzed[pred->src->id] = 1;
					analyzed_loop[pred->src->id] = 1;
				}	
				continue;	 
			}	
		  /* This means that the predecessor is outside 
			* the loop. Analyze with parent analyze 
			* function */
			if(loop_map[pred->src->id]->id == 0)
				analyze(pred->src);	 
			else
			{
				analyze_loop(pred->src, loop_map[pred->src->id]->id);	 
			}	
		}
	}	

	/* Apply transfer functions for the basic block node
	 * inside this loop */
	transformAloc(iter, &change);

	/* Mark that the node inside loop has been analyzed 
	 * so that it won't be analyzed second time */
	analyzed_loop[iter->id] = 1;
}

/* Top level function for analyzing a loop */
static slist_p analyze_loop_top(loop_t* loop, int loop_id)
{
	slist_p iter;
	slist_p loop_nodes;

	loop_nodes = getAllLoopNodes(loop_id);

	for(iter = loop_nodes; iter; iter = iter->next)
	{
		  analyzed_loop[((tcfg_node_t *)iter->dst)->id] = 0;
	}
	for(iter = loop_nodes; iter; iter = iter->next)
	{
		  analyze_loop(((tcfg_node_t *)iter->dst), loop_id);
	}

	return loop_nodes;
}

/* Top level function for iterative computation */
static void analyze_top()
{
	int i;
	
	/* Allocate memory for the symbolic execution */
	analyzed = (int *) malloc(num_tcfg_nodes * sizeof(int));
	analyzed_loop = (int *) malloc(num_tcfg_nodes * sizeof(int));
	memset(analyzed, 0, num_tcfg_nodes * sizeof(int));
	memset(analyzed_loop, 0, num_tcfg_nodes * sizeof(int));

	for (i = 0; i < num_tcfg_nodes; i++)
	{
		analyze(tcfg[i]);
	}
}

static void joinMemoryAloc(abs_mem_p mem1, abs_mem_p* mem2)
{
	abs_mem_p iter1;

	for(iter1 = mem1; iter1; iter1 = iter1->next)
	{
		if(!iter1->valid)
		  continue;
		updateMemoryAloc(*(iter1->addr), mem2, iter1->value, iter1->inst_addr); 
	}
}

static void create_aloc()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j, k;
	int rs, rt, d, imm, base, offset, index;
	FILE* fp;
	int x = 0;
	int inst_addr[4096];

	for (i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  		isa_name =  isa[inst->op_enum].name;

			  /* For a memory load/store instruction find out the 
			   * memory hit/miss classification from the abstract 
			   * cache state */
				if(isStoreInst(isa_name))
				{
					 inst_addr[x++] = inst->addr; 
			   }	 
			 inst++;
		}
	}
	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 for(k = 0; k < x; k++)		 
		 {
			abs_mem_p temp = (abs_mem_p)malloc(sizeof(abs_mem_s));
			memset(temp, 0, sizeof(abs_mem_s));
			temp->inst_addr = inst_addr[k];
			temp->valid = 0;
			temp->next = tcfg[i]->bb->in_abs_mem_value;
			tcfg[i]->bb->in_abs_mem_value = temp;
		 }		 
	}
}

/* Top level function for address analysis */

void analyze_address()
{
	/* FIXME: Assumed that the first basic block in the transformed 
	 * CFG is always the start block */
	tcfg_node_t* bbi = tcfg[0];
	tcfg_edge_t* edge;
   worklist_p Wlist = NULL;
	int change_flag = 0, glob_change_flag = 1;
   int i;

	/* Allocate memory */	  
	bbi->bb->in_abs_reg_value = (ric_p *)malloc(MAX_NO_REGISTERS 
	  * sizeof(ric_p));  
	CHECK_MEM(bbi->bb->in_abs_reg_value);
	memset(bbi->bb->in_abs_reg_value, 0, MAX_NO_REGISTERS * 
	  sizeof(ric_p));	
	
	/* Initialize $zero register Value */
	bbi->bb->in_abs_reg_value[0] = (ric_p)malloc(sizeof(ric_s));  
	CHECK_MEM(bbi->bb->in_abs_reg_value[0]);
	memset(bbi->bb->in_abs_reg_value[0], 0, sizeof(ric_s));	
	
	/* Initialize stack and global pointer */
	bbi->bb->in_abs_reg_value[28] = (ric_p)malloc(sizeof(ric_s));  
	CHECK_MEM(bbi->bb->in_abs_reg_value[28]);
	bbi->bb->in_abs_reg_value[28]->lower_bound = GLOBAL_START;
	bbi->bb->in_abs_reg_value[28]->upper_bound = GLOBAL_START;
	bbi->bb->in_abs_reg_value[28]->stride = 0;
	
	/* Stack pointer */
	bbi->bb->in_abs_reg_value[29] = (ric_p)malloc(sizeof(ric_s));  
	CHECK_MEM(bbi->bb->in_abs_reg_value[29]);
	bbi->bb->in_abs_reg_value[29]->lower_bound = STACK_START;
	bbi->bb->in_abs_reg_value[29]->upper_bound = STACK_START;
	bbi->bb->in_abs_reg_value[29]->stride = 0;

	/* Return address */
	bbi->bb->in_abs_reg_value[31] = (ric_p)malloc(sizeof(ric_s));  
	CHECK_MEM(bbi->bb->in_abs_reg_value[31]);
	bbi->bb->in_abs_reg_value[31]->lower_bound = RETURN_ADDRESS;
	bbi->bb->in_abs_reg_value[31]->upper_bound = RETURN_ADDRESS;
	bbi->bb->in_abs_reg_value[31]->stride = 0;

	/* Create memory A-locs for all store locations */
	create_aloc();

	/* Do a symbolic execution */
	analyze_top();

#ifdef _DEBUG
	printf("Analysis count = %d\n for %d BB nodes\n", count, num_tcfg_nodes);
	dump_data_address();
	for(i = 0; i < num_tcfg_nodes; i++)
	 dumpAddress(tcfg[i]);
#endif
}
