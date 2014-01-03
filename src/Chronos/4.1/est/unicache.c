#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cache.h"
#include "tcfg.h"
#include "address.h"
#include "common.h"

extern tcfg_node_t** tcfg;
extern int num_tcfg_nodes;
extern prog_t	prog;

/* The instruction set */
extern isa_t* isa;
extern int gcd(int a, int b);

extern int nsets, bsize, assoc;
/* sudiptac ::: adding options for level 1 data cache */
extern int nsets_dl1, bsize_dl1, assoc_dl1, cache_dl1_lat;

/* sudiptac ::: adding options for level 2 cache 
 * (it could be unified or separate instruction cache) */
extern int nsets_l2, bsize_l2, assoc_l2, cache_dl2_lat, cache_il2_lat;
extern int mem_lat[2];
extern int enable_il2cache;
extern int enable_ul2cache;

static acs_p Difference(acs_p acs, mem_blk_set_t* mem_blk);
static void build_graph(proc_t* proc);
static void change_layout();
static void change_address();
static int count;
static int getShiftedBlock(unsigned int tblock);
static int allocated(mem_blk_set_t* arg);
static int addMapping(mem_blk_set_t* arg, unsigned int naddr);

#ifdef _DEBUG
static void dumpCacheBB(acs_p** acs, FILE* fp);
int analysis = 0;
int unified = 0;	 
#endif
int l1_d1_ps = 0;
int l1_i1_ps = 0;
int i1_u1_ps = 0;
int u1_d1_ps = 0;
int opt = 0;
extern ric_p getAddrBaseOffset(de_inst_t* inst, int base, int offset, int opt);
extern ric_p getAddrBaseIndex(de_inst_t* inst, int base, int index, int opt);
int X,Y,B,l1,l2;

/****************************************/
/******* For debugging purposes *********/
/****************************************/

#ifdef _DEBUG
/* Print classification of data accesses */
static void print_classification_data()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;

			 if(inst->data_access == ALL_HIT)
			 	printf("Data Access AH at %x\n", inst->addr);
			 else if(inst->data_access == ALL_MISS)
				printf("Data Access AM at %x\n", inst->addr);
			 else if(inst->data_access == PS)
				printf("Data Access PS at %x\n", inst->addr);
			 else if(inst->data_access == NOT_CLASSIFIED)
				printf("Data Access NC at %x\n", inst->addr);
			 else
				printf("Should not come here %x\n", inst->addr);

			 inst++;
		}
	}
}

/* print hit-miss classification of all iinstructions */
static void print_classification()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;

			 if(inst->inst_access == ALL_HIT)
			 	printf("Instruction all hit at %x\n", inst->addr);
			 else if(inst->inst_access == ALL_MISS)
				printf("Instruction all miss at %x\n", inst->addr);
			 else if(inst->inst_access == PS)
				printf("Instruction persistence at %x\n", inst->addr);
			 else if(inst->inst_access == NOT_CLASSIFIED)
				printf("Instruction not classified at %x\n", inst->addr);
			 else
				printf("Should not come here %x\n", inst->addr);

			 inst++;
		}

		printf("tcfg node %d delay = (I=%d,D=%d)\n", tcfg[i]->id, 
					 tcfg[i]->inst_cache_delay + L1_MISS_PENALTY * tcfg[i]->n_persistence,
					 tcfg[i]->dcache_delay);
	}
}

/* Dump the cache state */
/* All for debugging purposes */

/* Dump cache states of a single basic block (at 
 * exit point) */
static void dumpCacheBB(acs_p** acs, FILE* fp)
{
	int i,k;
	mem_blk_set_t* iter;

	if(!acs)
		return;
		 
	for(k = 0; k < MAX_CACHE_SET; k++)
	{
		for(i = 0; i <= CACHE_SET_SIZE; i++)
		{
		  fprintf(fp, "*************************************\n");  
		  fprintf(fp, "Printing cache (set,block) (%d,%d)\n", k,i);  
		  fprintf(fp, "*************************************\n");  
		  if(!acs[k][i])
		   fprintf(fp, "{empty}\n");
		  else if(!acs[k][i]->mem_blk_h)  
		   fprintf(fp, "{empty}\n");
		  else
		  {
			 for(iter = acs[k][i]->mem_blk_h; iter; iter = iter->next)
			 {
				fprintf(fp, "Memory Block %x,", iter->block);	 
			 }
			 fprintf(fp, "\n");
		  }
		}
	}	
}

/* Dump cache states of all basic blocks (at exit
 * point) */
static void dumpCache()
{
	int i;
	FILE* fp;

	fp = fopen("cache.dump", "w");
	if(!fp)
		printf("Internal file opening failed\n");  

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		de_inst_t* l_inst;
		/* Go to last instruction */
		l_inst = tcfg[i]->bb->code + tcfg[i]->bb->num_inst - 1;
		fprintf(fp, "=====================================\n");
		fprintf(fp, "At tcfg block %d\n", i);
		fprintf(fp, "=====================================\n");
		dumpCacheBB(l_inst->acs_out, fp);
	}

	fclose(fp);
}

/* Dump instruction cache states of all basic blocks 
 * (at exit point) */
static void dumpInstCache()
{
	int i;
	FILE* fp;

	fp = fopen("inst_cache.dump", "w");
	if(!fp)
		printf("Internal file opening failed\n");  

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		de_inst_t* l_inst;
		/* Go to last instruction */
		l_inst = tcfg[i]->bb->code + tcfg[i]->bb->num_inst - 1;
		/* l_inst = tcfg[i]->bb->code ; */
		fprintf(fp, "=====================================\n");
		fprintf(fp, "At tcfg block %d\n", i);
		fprintf(fp, "=====================================\n");
		dumpCacheBB(l_inst->acs_in, fp);
	}

	fclose(fp);
}
#endif

/* Error message routine */
void prerr(char* msg)
{
	printf("PANIC ***** %s. Exiting now.......\n", msg);
	exit(-1);
}

/*************************************************************/
/* 					END DEBUG RELATED ROUTINES						 */
/*************************************************************/ 		

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

/* Create a cache set */
static acs_p* makeCacheSet()
{
	acs_p* ret;

	/* To accomodate the victim cache block, cache-set size is set
	 * one more than the given parameter */
	ret = (acs_p *)malloc((CACHE_SET_SIZE + 1) * sizeof(acs_p));
	CHECK_MEM(ret);
	memset(ret, 0, (CACHE_SET_SIZE + 1) * sizeof(acs_p));

	return ret;
}

/* Free a set of linked memory blocks */
static void freeMemBlock(mem_blk_set_t* head)
{
	mem_blk_set_t* iter;

	if(!head)
		return; 

	freeMemBlock(head->next);
	head->next = NULL;

#ifdef _DELETE
	printf("Freeing mem block = %x\n", head);
#endif
	
	free(head);
}

/* Free an abstract cache line */
static void freeCacheLine(acs_p acl)
{
	if(!acl)
		return;

	freeMemBlock(acl->mem_blk_h);	  
	acl->mem_blk_h = NULL;

	free(acl);
}

/* Free an abstract cache set */
static void freeCacheSet(acs_p* acs)
{
	int i;

	if (!acs)
	 return;	   

	for(i = 0; i <= CACHE_SET_SIZE; i++)
	{
		 freeCacheLine(acs[i]);
		 acs[i] = NULL;
	}	 

	free(acs);
}

/* Free an abstract cache state */
static void freeCacheState(acs_p** acs)
{
	int i;

	if(!acs)
		return;  

	for(i = 0; i < MAX_CACHE_SET; i++)
	{
		freeCacheSet(acs[i]);  
		acs[i] = NULL;
	}
	
	free(acs);
}

/* Get all memory referenced by this address range */
static mem_blk_set_t* getMemoryBlocks(ric_p addr)
{
	mem_blk_set_t* mem_set = NULL;
	mem_blk_set_t* temp;
	int i;
	int prev = -1;
	int count = 0;

	for(i = addr->lower_bound; i <= addr->upper_bound; i += addr->stride)
	{
		if(prev == GET_MEM(i))
		  continue;
		prev = GET_MEM(i);  
		temp = (mem_blk_set_t *) malloc(sizeof(mem_blk_set_t));
		CHECK_MEM(temp);
		/* Assume that all addresses are aligned */
		temp->block = prev;
		
		/* FIXME : what is this code meant for ? */
		/*if(opt)
		{
		  int newb;

		  if(newb = getShiftedBlock(prev));
		  {
#ifdef _DEBUG
				printf("Returning shifted block\n");
#endif
				temp->block = newb;	 
		  }  			 
		}*/  
		
		temp->next = mem_set;
		mem_set = temp;
		count++;
		/* if number of memory blocks for this address set is more than 
		 * the cache size then the cache is flushed */
		if(count > MAX_CACHE_SET * CACHE_SET_SIZE)
		  break;
		if(!addr->stride)	
		  break;
	}

	return mem_set;	  
}

/* Make an empty cache block */
static acs_p makeEmpty()
{
	acs_p ret;

	ret = (acs_p)malloc(sizeof(acs_s));
	CHECK_MEM(ret);
	/* Nothing in the cache block */
	ret->mem_blk_h = NULL;

	return ret;
}

/* Returns 1 if a particular memory block is present in a given
 * Cache block */
static int isResident(mem_blk_set_t* mem_blk_h, mem_blk_set_t* item)
{
	mem_blk_set_t* iter;
	
	for(iter = mem_blk_h; iter; iter = iter->next)	  
	{
		if(iter->block == item->block)
		  return 1;	 
	}

	return 0;
}

/* Check for a given set of memory blocks whether its
 * superset (may not be proper) is present in any of 
 * the cache block */
static int checkForInclusionSingle(acs_p* acs_in, mem_blk_set_t* mem_blk_set)
{
	mem_blk_set_t* iter;
	int i;

    for(i = 0; i <= CACHE_SET_SIZE; i++)
    {
        for(iter = mem_blk_set; iter; iter = iter->next)
        {
            if(!acs_in[i]) continue;
            else if( isResident(acs_in[i]->mem_blk_h, iter))
                    break;	 
        }
        if( iter) {
            return i;
        }
    }

    /* oops .. Not found memory block in the cache */	  
    return -1;
}

/* Partition the memory block with respect what is 
 * already present in the cache and what is not */
static void partition(acs_p* acs_in, mem_blk_set_t* mem_blk_h,
		  mem_blk_set_t** mem_blk_resident, mem_blk_set_t** mem_blk_absent)
{
	int i;
	int found;
	mem_blk_set_t* iter;
	mem_blk_set_t* temp;

	*mem_blk_resident = *mem_blk_absent = NULL;

	for(iter = mem_blk_h; iter; iter = iter->next)	  
	{
		found = 0;

		for(i = 0; i <= CACHE_SET_SIZE; i++)
		{
			if(acs_in && acs_in[i] && isResident(acs_in[i]->mem_blk_h, iter))
			{
				temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
				CHECK_MEM(temp);
				temp->block = iter->block;
				temp->next = *mem_blk_resident;
				*mem_blk_resident = temp;	 	
				found = 1;
				break;
			}
		}
		if(!found)
		{
			temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
			CHECK_MEM(temp);
			temp->block = iter->block;
			temp->next = *mem_blk_absent;
			*mem_blk_absent = temp;	 	
		}
	}	
}

/* Count the number of memory blocks present */
static int getCardinality(mem_blk_set_t* mem_blk_set)
{
	mem_blk_set_t* iter;
	int count = 0;
	
	for(iter = mem_blk_set; iter; iter = iter->next)
		 count++; 

	return count;	 
}

/* Make a copy of the cache block */
static acs_p makeCopy(acs_p acs_in)
{
	acs_p ret;
	int i;
	mem_blk_set_t* iter;

	if(! acs_in)
		return NULL;  

	ret = (acs_p)malloc(sizeof(acs_s));
	CHECK_MEM(ret);
	memset(ret, 0, sizeof(acs_s));
	ret->mem_blk_h = NULL;

	for(iter = acs_in->mem_blk_h; iter; iter = iter->next)
	{
		mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
		CHECK_MEM(temp);
		temp->block = iter->block;
		temp->next = ret->mem_blk_h;
		ret->mem_blk_h = temp;
	}

	return ret;
}

/* Make a cache block from a set of memory blocks */
static acs_p makeCacheBlock(mem_blk_set_t* mem_blk_set)
{
	acs_p ret;
	int i;
	mem_blk_set_t* iter;

	if(! mem_blk_set)
		return NULL;  

	ret = (acs_p)malloc(sizeof(acs_s));
	CHECK_MEM(ret);
	memset(ret, 0, sizeof(acs_s));
	ret->mem_blk_h = NULL;

	for(iter = mem_blk_set; iter; iter = iter->next)
	{
		mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
		CHECK_MEM(temp);
		temp->block = iter->block;
		temp->next = ret->mem_blk_h;
		ret->mem_blk_h = temp;
	}

	return ret;
}

/* Set intersection of the contents of two cache blocks */
/* and return the result */
static acs_p Intersect(acs_p acs1, acs_p acs2)
{
	acs_p ret;
	int i;
	mem_blk_set_t* iter;

	if(!acs1 || !acs2)
		return NULL;  

	ret = makeEmpty();	

	for(iter = acs2->mem_blk_h; iter; iter = iter->next)
	{
		/* If the cache block is present in both of the 
		 * argument cache blocks, then it is present in 
		 * the return cache block. Useful for must 
		 * analysis */
		if(isResident(acs1->mem_blk_h, iter))  
		{
		  mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
		  CHECK_MEM(temp);
		  temp->block = iter->block;
		  temp->next = ret->mem_blk_h;
		  ret->mem_blk_h = temp;
		}
	}

	return ret;
}

/* Set union of the contents of two cache blocks */
/* and return the result */
static acs_p Union(acs_p acs1, acs_p acs2)
{
	acs_p ret;
	int i;
	mem_blk_set_t* iter;

	if(! acs1 && !acs2)
		return NULL;  

	/* Copy the first cache block */
	ret = makeCopy(acs1);

	if(!acs2)
	   return ret;  

	for(iter = acs2->mem_blk_h; iter; iter = iter->next)
	{
		if(!ret)
		{
		  ret = (acs_p)malloc(sizeof(acs_s));
		  CHECK_MEM(ret);	 		 
		  ret->mem_blk_h = NULL;
		}
		if(!isResident(ret->mem_blk_h, iter))  
		{
		  mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
		  CHECK_MEM(temp);
		  temp->block = iter->block;
		  temp->next = ret->mem_blk_h;
		  ret->mem_blk_h = temp;
		}
	}

	return ret;
}

/* Set union of one cache block ans a set of memory blocks
 */
static acs_p UnionCacheMem(acs_p acs1, mem_blk_set_t* mem_blk_set)
{
	acs_p ret = NULL;
	int i;
	mem_blk_set_t* iter;

	if(! mem_blk_set)
		return acs1;  

	/* Copy the first cache block */
	ret = makeCopy(acs1);

	for(iter = mem_blk_set; iter; iter = iter->next)
	{
		if(! ret)
		{
		  ret = (acs_p)malloc(sizeof(acs_s));
		  memset(ret, 0, sizeof(acs_s));
		}  
		if(!isResident(ret->mem_blk_h, iter))  
		{
		  mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
		  CHECK_MEM(temp);
		  temp->block = iter->block;
		  temp->next = ret->mem_blk_h;
		  ret->mem_blk_h = temp;
		}
	}

	return ret;
}

/* Check for a given set of memory blocks whether its
 * superset (may not be proper) is present in the entire 
 * cache */
static int checkForOnePresence(acs_p** acs_in, mem_blk_set_t* mem_blk_set)
{
	mem_blk_set_t* iter;
	int i,k;

	for(iter = mem_blk_set; iter; iter = iter->next)
	{
		k = GET_SET(iter->block);  
		for(i = 0; i < CACHE_SET_SIZE; i++)
		{
		  	if(acs_in[k][i] && isResident(acs_in[k][i]->mem_blk_h, iter))
				return 1;	 
		}
	}

	return 0;
}

/* Check for a given set of memory blocks whether its
 * superset (may not be proper) is present in the entire 
 * cache */
static int checkForPresence(acs_p** acs_in, mem_blk_set_t* mem_blk_set)
{
	mem_blk_set_t* iter;
	int i,k;

	for(iter = mem_blk_set; iter; iter = iter->next)
	{ 
		k = GET_SET(iter->block);  
		
		for(i = 0; i < CACHE_SET_SIZE; i++)
		{
		  	if(acs_in[k][i] && isResident(acs_in[k][i]->mem_blk_h, iter))
				break;	 
		}
		
		if(i == CACHE_SET_SIZE)
		  return 0;
	}

	return 1;
}

/* Check for a given set of memory blocks whether its
 * superset (may not be proper) is present in the entire 
 * cache */
static int checkForVictim(acs_p** acs_in, mem_blk_set_t* mem_blk_set)
{
	mem_blk_set_t* iter;
	int i,k;

	for(iter = mem_blk_set; iter; iter = iter->next)
	{ 
		for(i = 0; i < MAX_CACHE_SET; i++)  
		{
		  if(acs_in[i][PSEUDO] && isResident(acs_in[i][PSEUDO]->mem_blk_h, iter))
			return 1;	 
		}	
	}

	return 0;
}

/* Check for a given set of memory blocks whether its
 * superset (may not be proper) is present in any of 
 * the cache block */
static int checkForInclusion(acs_p* acs_in, mem_blk_set_t* mem_blk_set)
{
	mem_blk_set_t* iter;
	int i;

	for(i = 0; i < CACHE_SET_SIZE; i++)
	{
		for(iter = mem_blk_set; iter; iter = iter->next)
		{
		  	if(acs_in[i] && ! isResident(acs_in[i]->mem_blk_h, iter))
				break;	 
		}
		if(! iter)
		  return i;
	}

	/* oops .. Not found memory block in the cache */	  
	return -1;
}

/* Get the memory blocks mapping to the same set */
static mem_blk_set_t* getMemoryBlockOfSet(mem_blk_set_t* mem_blk, int set)
{
	mem_blk_set_t* head = NULL;
	mem_blk_set_t* iter;
	int st;

	if(!mem_blk)
		return NULL;  

	for(iter = mem_blk; iter; iter = iter->next)
	{
		 st = GET_SET(iter->block);
		 if(st == set)
		 {
			mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
			CHECK_MEM(temp);
			temp->block = iter->block;
			temp->next = head;
			head = temp;
		 }
	}

	return head;
}

/* Cache update function for must analysis */
acs_p** update_must(acs_p** acs_in, ric_p addr, ANALYSIS_T type)
{
    mem_blk_set_t* mem_blk = NULL, *mem_blk_resident = NULL, *mem_blk_absent = NULL, *iter;
    acs_p l_top;
    int n_elems, h, i;
    int k,j;
    mem_blk_set_t* all_blk;
    mem_blk_set_t temp;
    acs_p** acs_out;	  
    acs_p** ret_acs_out;	  
    all_blk = getMemoryBlocks(addr);	  
	 int hold;

	 /* Allocate intial memory blocks for the update function */	  
    acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
    CHECK_MEM(acs_out);
    memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));

    ret_acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
    CHECK_MEM(ret_acs_out);
    memset(ret_acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));

	 hold = getCardinality(all_blk);

    for(k = 0; k < MAX_CACHE_SET; k++)
    {
		  /* Flush the cache if number of memory blocks in the address set
			* is more than the cache size */
		  if(hold > MAX_CACHE_SET * CACHE_SET_SIZE)
		  {
				ret_acs_out[k] = makeCacheSet();
            for(i = 0; i <= CACHE_SET_SIZE; i++)
              ret_acs_out[k][i] = makeEmpty();
				continue;
		  }

        mem_blk = getMemoryBlockOfSet(all_blk, k); 
        acs_out[k] = makeCacheSet();
        ret_acs_out[k] = makeCacheSet();

        if(!mem_blk)
		  {
            for(i = 0; i <= CACHE_SET_SIZE; i++)
              ret_acs_out[k][i] = makeCopy(acs_in[k][i]);
            continue;
		  }
        
		  if (getCardinality(all_blk) == 1) {
            h = checkForInclusionSingle(acs_in[k], mem_blk);
            
				/* Do nothing. No change in cache state if the cache block is 
				 * found in the 0-th position */	  
            if(h == 0)
            {
                for(i = 0; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_in[k][i]);
            }	  
            else if(h != -1)
            {
                ret_acs_out[k][0] = makeCacheBlock(mem_blk);
                for(i = 1; i < h - 1; i++)
                    ret_acs_out[k][i] = Difference(acs_in[k][i - 1],mem_blk);
                ret_acs_out[k][h] = Union(acs_out[k][h], acs_in[k][h - 1]);
                for(i = h + 1; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = Difference(acs_in[k][i],mem_blk);
            }
            else
            {
                ret_acs_out[k][0] = makeCacheBlock(mem_blk);
                for(i = 1; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_in[k][i - 1]);
            }
            continue;
        }
        
		  /* Partition the memory blocks depending on which are present and 
         * absent in the abstract cache state */
        partition(acs_in[k], mem_blk, &mem_blk_resident, &mem_blk_absent);	  
        n_elems = getCardinality(mem_blk_absent);

        if(n_elems != 0 && n_elems <= CACHE_SET_SIZE)
        {
            for(i = n_elems; i < CACHE_SET_SIZE; i++)
            {
                acs_out[k][i] = makeCopy(acs_in[k][i - n_elems]);	 
            }
            for(i = 0; i < n_elems; i++)
                acs_out[k][i] = makeEmpty();		 
        }
        else if(n_elems != 0)
        {
            for(i = 0; i < CACHE_SET_SIZE; i++)
                acs_out[k][i] = makeEmpty();		 
        }
		  
		  for(i = 0; i <= CACHE_SET_SIZE; i++)
            ret_acs_out[k][i] = makeCopy(acs_out[k][i]);

        for(iter = mem_blk_resident; iter; iter = iter->next)
        {
         temp.block = iter->block;
         temp.next = NULL;
         h = checkForInclusion(acs_in[k], &temp);

		  /* Do nothing. No change in cache state */	  
		  if(h == 0)
		  {
				for(i = 0; i <= CACHE_SET_SIZE; i++)
				{
					ret_acs_out[k][i] = makeCopy(acs_out[k][i]);
				}	
		  }	  
		  else if(h != -1)
		  {
		      ret_acs_out[k][0] = makeEmpty();
		      for(i = 1; i < h - 1; i++)
				{
					 ret_acs_out[k][i] = makeCopy(acs_out[k][i - 1]);
				}	 
		      ret_acs_out[k][h] = Union(acs_out[k][h], acs_out[k][h - 1]);
		      for(i = h + 1; i < CACHE_SET_SIZE; i++)
				{
					 ret_acs_out[k][i] = makeCopy(acs_out[k][i]);
				}	 
		  }
		  else
		  {
		      ret_acs_out[k][0] = makeEmpty();
				for(i = 1; i < CACHE_SET_SIZE; i++)
					ret_acs_out[k][i] = makeCopy(acs_out[k][i - 1]);
		  }
		  
		  for(i = 0; i <= CACHE_SET_SIZE; i++) {
#ifdef MEM_FREE
			 freeCacheLine(acs_out[k][i]);
			 acs_out[k][i] = NULL;		 
#endif
		    acs_out[k][i] = makeCopy(ret_acs_out[k][i]);
		  }	  
	   }
	 } 

#ifdef MEM_FREE
	 freeMemBlock(mem_blk_resident);
	 freeMemBlock(mem_blk_absent);
	 freeMemBlock(mem_blk);
	 freeMemBlock(all_blk);
	 mem_blk = all_blk = mem_blk_resident = mem_blk_absent = NULL;
#endif	 

    return ret_acs_out;
}

/* cache update function for persistence data cache analysis */
#define DBG_PS_UPDATE 0
static acs_p** update_Reinherd(acs_p** acs_in, ric_p addr)
{
	mem_blk_set_t* mem_blk = NULL, *mem_blk_resident = NULL, *mem_blk_absent = NULL, *iter;
	acs_p l_top;
	int n_elems, h, i;
	int k,j;
	mem_blk_set_t* all_blk;
	mem_blk_set_t temp;
	acs_p** acs_out;	  
	acs_p** ret_acs_out;	  

	all_blk = getMemoryBlocks(addr);	  
   
	/* allocate initial memory for update stage */
	acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
	CHECK_MEM(acs_out);
	memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
	
	ret_acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
	CHECK_MEM(ret_acs_out);
	memset(ret_acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
	
	for(k = 0; k < MAX_CACHE_SET; k++)
   {
        mem_blk = getMemoryBlockOfSet(all_blk, k); 
        acs_out[k] = makeCacheSet();
        ret_acs_out[k] = makeCacheSet();

		  /* If there is no memory block in the set, abstract cache set 
			* is unchanged */
        if(!mem_blk) {
				for(i = 0; i <= CACHE_SET_SIZE; i++)
					ret_acs_out[k][i] = makeCopy(acs_in[k][i]);
				continue;
        }

		  /* Check whether it is a singleton memory block update, in that 
			* case update function can be optimized to reduce over 
			* estimation */
			if (getCardinality(all_blk)==1) {
				h = checkForInclusionSingle(acs_in[k], mem_blk);
				/* Do nothing. No change in cache state */	  
            if(h == 0)
            {
                for(i = 0; i <= CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_in[k][i]);
            }	  
            else if(h != -1)
            {
                ret_acs_out[k][0] = makeCacheBlock(mem_blk);
                for(i = 1; i < h - 1; i++)
                    ret_acs_out[k][i] = Difference(acs_in[k][i - 1],mem_blk);
                /*remove from victim block*/
                ret_acs_out[k][h] = Union(acs_out[k][h], acs_in[k][h - 1]);
                for(i = h + 1; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = Difference(acs_in[k][i],mem_blk);
                ret_acs_out[k][PSEUDO] = Difference(acs_in[k][PSEUDO],mem_blk);  
            }
            else
            {
                ret_acs_out[k][0] = makeCacheBlock(mem_blk);
                for(i = 1; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_in[k][i - 1]);
                ret_acs_out[k][PSEUDO] = Union(acs_in[k][PSEUDO], 
                        acs_in[k][CACHE_SET_SIZE - 1]);
            }
            continue;
        }


        /* Partition the memory blocks depending on which are present and 
         * absent in the abstract cache state */
        partition(acs_in[k], mem_blk, &mem_blk_resident, &mem_blk_absent);	  
        n_elems = getCardinality(mem_blk_absent);

		  /* Check whether any memory blocks are not present in the old abstract 
			* cache state */
        if(n_elems != 0 && n_elems <= CACHE_SET_SIZE)
        {
				acs_p cur;

            for(i = n_elems; i < CACHE_SET_SIZE; i++)
            {
                acs_out[k][i] = makeCopy(acs_in[k][i - n_elems]);	 
            }
            for(i = 0; i < n_elems - 1; i++)
                acs_out[k][i] = makeEmpty();		 
            acs_out[k][n_elems  - 1] = makeCacheBlock(mem_blk_absent); 	 
            l_top = makeCopy(acs_in[k][PSEUDO]);
            for(i = CACHE_SET_SIZE - n_elems; i <= CACHE_SET_SIZE; i++) {
					 cur = l_top;
                l_top = Union(l_top, acs_in[k][i]);		 
#ifdef MEM_FREE
					 freeCacheLine(cur);
					 cur = NULL;
#endif
				}	 
            acs_out[k][PSEUDO] = makeCopy(l_top);	 
#ifdef MEM_FREE
				freeCacheLine(l_top);
				l_top = NULL;
#endif
        }
		  /* There is something not present in the previous or old 
			* abstract cache */
        else if(n_elems != 0)
        {
				acs_p cur;

            for(i = 0; i < CACHE_SET_SIZE; i++)
                acs_out[k][i] = makeEmpty();		 
            l_top = makeCopy(acs_in[k][PSEUDO]);
            for(i = 0; i < CACHE_SET_SIZE; i++)
            {
					 cur = l_top;
                l_top = Union(l_top, acs_in[k][i]);		 
#ifdef MEM_FREE
					 freeCacheLine(cur);
					 cur = NULL;
#endif
            }
				cur = l_top;
            l_top = UnionCacheMem(l_top, mem_blk_absent);
#ifdef MEM_FREE
				freeCacheLine(cur);
				cur = NULL;
#endif
            acs_out[k][PSEUDO] = makeCopy(l_top);	 
#ifdef MEM_FREE
				freeCacheLine(l_top);
				l_top = NULL;
#endif
        }
        else
        {
         for(i = 0; i <= CACHE_SET_SIZE; i++)
            acs_out[k][i] = makeCopy(acs_in[k][i]);
        }

		  for(i = 0; i <= CACHE_SET_SIZE; i++)
            ret_acs_out[k][i] = makeCopy(acs_out[k][i]);

        for(iter = mem_blk_resident; iter; iter = iter->next)
        {
            temp.block = iter->block;
            temp.next = NULL;
            h = checkForInclusion(acs_in[k], &temp);

            /* Do nothing. No change in cache state */	  
            if(h == 0)
            {
                for(i = 0; i <= CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_out[k][i]);
            }	  
            else if(h != -1)
            {
                ret_acs_out[k][0] = makeEmpty();
                for(i = 1; i < h - 1; i++)
                    ret_acs_out[k][i] = makeCopy(acs_out[k][i - 1]);
                ret_acs_out[k][h] = Union(acs_out[k][h], acs_out[k][h - 1]);
                for(i = h + 1; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_out[k][i]);
                ret_acs_out[k][PSEUDO] = makeCopy(acs_out[k][PSEUDO]);  
            }
            else
            {
                ret_acs_out[k][0] = makeEmpty();
                for(i = 1; i < CACHE_SET_SIZE; i++)
                    ret_acs_out[k][i] = makeCopy(acs_out[k][i - 1]);
                ret_acs_out[k][PSEUDO] = Union(acs_out[k][PSEUDO], 
                        acs_out[k][CACHE_SET_SIZE - 1]);
            }
            
				for(i = 0; i <= CACHE_SET_SIZE; i++)
				{
#ifdef MEM_FREE
					 freeCacheLine(acs_out[k][i]);
					 acs_out[k][i] = NULL;
#endif
                acs_out[k][i] = makeCopy(ret_acs_out[k][i]);
				}	 
        }
    }  

#ifdef MEM_FREE
	 freeMemBlock(mem_blk_resident);
	 freeMemBlock(mem_blk_absent);
	 freeMemBlock(mem_blk);
	 freeMemBlock(all_blk);
	 mem_blk = all_blk = mem_blk_resident = mem_blk_absent = NULL;
#endif	 

    return ret_acs_out;
}

/* Take the set difference of a cache line and a memory block */
static acs_p Difference(acs_p acs, mem_blk_set_t* mem_blk)
{
	mem_blk_set_t* iter;
	acs_p ret;

	if(!acs)
		return NULL;  

	ret = (acs_p)malloc(sizeof(acs_s));
	CHECK_MEM(ret);
	ret->mem_blk_h = NULL;

	for(iter = acs->mem_blk_h; iter; iter = iter->next)
	{
		if(iter->block != mem_blk->block)
		{
			mem_blk_set_t* temp = (mem_blk_set_t *)malloc(sizeof(
					 mem_blk_set_t));		 
			CHECK_MEM(temp);
			temp->block = iter->block;
			temp->next = ret->mem_blk_h;
			ret->mem_blk_h = temp;
		}	
	}	

	return ret;
}

/* Copy an abstract cache state */
static acs_p* copy_cache(acs_p* acs)
{
	int i;
	acs_p* dest = NULL;

	if(! acs)
		return NULL;  

	/* Allocate memory for the copying destination */
	dest = makeCacheSet();
	
	for(i = 0; i <= CACHE_SET_SIZE; i++)
	{
		 dest[i] = makeCopy(acs[i]); 
	}

	return dest;
}

/* abstract cache set update for a singleton memory block */
static acs_p* update_singleton(acs_p* acs, mem_blk_set_t* mem_blk_set)
{
	int line = 0;
	acs_p* ret;
	acs_p cur;
	int i, j;
	mem_blk_set_t* temp = NULL;

	temp = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));
	CHECK_MEM(temp);
	temp->block = mem_blk_set->block;
	temp->next = NULL;

	ret = makeCacheSet();
	
	for(line = 0; line < CACHE_SET_SIZE; line++)
	{
		/* block is not present in the cache line */  
		if(!acs)
		{
		 line = CACHE_SET_SIZE; 
		 break;		 
		} 
		/* block is present in the cache line */
		if(acs[line] && isResident(acs[line]->mem_blk_h, temp))
		 break;
	}

	/* The memory block is present in the cache */
	if(line < CACHE_SET_SIZE)
	{
		for(i = 1; i < line; i++)
		  ret[i] = makeCopy(acs[i - 1]);
		if(line > 0)  {
		  cur = Difference(acs[line], temp);
		  ret[line] = Union(cur, acs[line - 1]);   
#ifdef MEM_FREE
		  freeCacheLine(cur);
		  cur = NULL;
#endif
		}  
		for(i = line + 1; i < CACHE_SET_SIZE; i++)
		  ret[i] = makeCopy(acs[i]);
		ret[0] = makeCacheBlock(temp);  
		
		/* For May analysis */
		if(line == 0 && acs[0])
		{
		  cur = Difference(acs[0], temp);
		  ret[1] = Union(cur, ret[1]); 
#ifdef MEM_FREE
		  freeCacheLine(cur);
		  cur = NULL;
#endif
		}  
	}
	
	/* The memory block is not present in the cache */
	else
	{
		for(i = 1; i < CACHE_SET_SIZE; i++)
		  ret[i] = makeCopy(acs[i - 1]);
		ret[0] = makeCacheBlock(temp); 
		/* For persistence analysis collect the victim cache 
		 * blocks */
		ret[PSEUDO] = Union(acs[PSEUDO], acs[CACHE_SET_SIZE - 1]);  
	}

#ifdef MEM_FREE
	free(temp);	  
#endif

	return ret;
}

/* Must join of cache analysis */
static acs_p* joinCacheMust(acs_p* acs1, acs_p* arg)
{				
	acs_p temp = NULL;
	acs_p val = NULL;
	int i, j;
	acs_p* acs;
	mem_blk_set_t* iter;
	
	if(!acs1)
		return copy_cache(arg);  

	acs = makeCacheSet();

	/* Do cache join for all the abstract cache sets of a cache */
	for(i = 0; i < CACHE_SET_SIZE; i++)
	{
		temp = NULL;  
		val = NULL;

		/* If one memory block is present in more than one cache
		 * block take the cache block which is older in age. 
		 * Following code take care of this in case of must 
		 * analysis. */
		 for(j = i; j >= 0; j--)
		 {
			val = temp;
		  	temp = Union(temp, Intersect(acs1[i], arg[j])); 
#ifdef MEM_FREE
			freeCacheLine(val);
			val = NULL;
#endif
		 }

		 for(j = i; j >= 0; j--)
		 {
			val = temp;
			temp = Union(temp, Intersect(arg[i], acs1[j])); 
#ifdef MEM_FREE
		   freeCacheLine(val);
			val = NULL;
#endif
		 }
		 
		 acs[i] = temp;
	}

	return acs;
}	

/* May join of cache analysis */
static acs_p* joinCacheMay(acs_p* acs1, acs_p* arg)
{				
	acs_p temp = NULL;
	acs_p val = NULL;
	int i, j;
	acs_p* acs;
	mem_blk_set_t* iter;
	
	if(!acs1)
		return copy_cache(arg);  

	acs = makeCacheSet();

	for(i = 0; i < CACHE_SET_SIZE; i++)
	{
		temp = NULL;
		val = NULL;

		/* PRESENT in ACS-0 but not in ACS-1 */
		if(acs1[i])
		{
			for(iter = acs1[i]->mem_blk_h; iter; iter = iter->next)
			{
				for(j = i - 1; j >=0; j--)
				{
					if(arg[j] && isResident(arg[j]->mem_blk_h, iter))
					{
						break;	
					}	
				} 
				if(j < 0)
				{	
					mem_blk_set_t* cur = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));	
					cur->block = iter->block;
					cur->next = NULL;
					val = temp;
					temp = UnionCacheMem(temp, cur);		
#ifdef MEM_FREE
					freeCacheLine(val);		
					free(cur);
					cur = NULL; val = NULL;
#endif
				}
			}
		}	  
		/* PRESENT in ACS-1 but not in ACS-0 */
		if(arg[i])
		{
			for(iter = arg[i]->mem_blk_h; iter; iter = iter->next)
			{
				for(j = i - 1; j >=0; j--)
				{
					if(acs1[j] && isResident(acs1[j]->mem_blk_h, iter))
					{
						break;	
					}	
				} 
				if(j < 0)
				{
					mem_blk_set_t* cur = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));	
					cur->block = iter->block;
					cur->next = NULL;
					val = temp;
					temp = UnionCacheMem(temp, cur);		
#ifdef MEM_FREE
					freeCacheLine(val);
					free(cur);
					val = NULL; cur = NULL;
#endif
				}
			}
		}
		acs[i] = temp;
	}

	return acs;
}	

/* Persistence join of cache analysis */
static acs_p* joinCachePS(acs_p* acs1, acs_p* arg)
{
	acs_p temp = NULL;
	acs_p val = NULL;
	int i, j;
	acs_p* acs;
	mem_blk_set_t* iter;
	
	if(!acs1)
		return copy_cache(arg);  

	acs = makeCacheSet();
	
	for(i = 0; i <= CACHE_SET_SIZE; i++)
	{
		temp = NULL;
		val = NULL;

		if(acs1[i])
		{
			for(iter = acs1[i]->mem_blk_h; iter; iter = iter->next)
			{
				for(j = i + 1; j <= CACHE_SET_SIZE; j++)
				{
					if(arg[j] && isResident(arg[j]->mem_blk_h, iter))
					{
						break;	
					}	
				} 
				if(j > CACHE_SET_SIZE)
				{	
					mem_blk_set_t* cur = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));	
					cur->block = iter->block;
					cur->next = NULL;
					val = temp;
					temp = UnionCacheMem(temp, cur);		
#ifdef MEM_FREE
					freeCacheLine(val);
					free(cur);
					val = NULL; cur = NULL;
#endif
				}
			}
		}	  
		if(arg[i])
		{
			for(iter = arg[i]->mem_blk_h; iter; iter = iter->next)
			{
				for(j = i + 1; j <= CACHE_SET_SIZE; j++)
				{
					if(acs1[j] && isResident(acs1[j]->mem_blk_h, iter))
					{
						break;	
					}	
				} 
				if(j > CACHE_SET_SIZE)
				{
					mem_blk_set_t* cur = (mem_blk_set_t *)malloc(sizeof(mem_blk_set_t));	
					cur->block = iter->block;
					cur->next = NULL;
					val = temp;
					temp = UnionCacheMem(temp, cur);		
#ifdef MEM_FREE
					freeCacheLine(val);
					free(cur);
					val = NULL; cur = NULL;
#endif
				}
			}
		}
		acs[i] = temp;
	}

	return acs;
}

/* Join function during data cache update. Depends on the
 * direction of analysis (Must, May and Persistence) */
static acs_p* joinCache(acs_p* acs1, acs_p* arg, ANALYSIS_T type)
{
	acs_p temp = NULL;
	acs_p val = NULL;
	int i, j;
	acs_p* acs;
	mem_blk_set_t* iter;
	
	/* Join operation for for must cache analysis */
 	if(type == MUST)
	{
		return joinCacheMust(acs1, arg);  
	}	  
	
	/* Join function for MAY analysis */
	else if(type == MAY)
	{
		return joinCacheMay(acs1, arg);
	}

	/* Join function for persistence analysis */
	else if(type == PERSISTENCE)
	{
		return joinCachePS(acs1, arg);  
	}

	return NULL;
}

/* Instruction cache update function */
static acs_p** update_abs_inst(acs_p** acs_in, unsigned addr)
{
	mem_blk_set_t mem_blk, *iter;
	acs_p* temp;
	acs_p* cur;
	acs_p** acs_out;
   int set,i;

	mem_blk.block = GET_MEM(addr);
	mem_blk.next = NULL;
	
	acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
	memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));

	for(i = 0; i < MAX_CACHE_SET; i++)
		acs_out[i] = copy_cache(acs_in[i]);

	/* Each instruction corresponds to two addresses. Update
	 * the instruction cache state accordingly */
	set = GET_SET(mem_blk.block);
	temp = update_singleton(acs_in[set], &mem_blk);
	mem_blk.block = GET_MEM(addr + SIZE_OF_WORD);
	set = GET_SET(mem_blk.block);
	cur = acs_out[set];
	acs_out[set] = update_singleton(temp, &mem_blk);

	/* free up memory */	  
#ifdef MEM_FREE
	freeCacheSet(temp);
	freeCacheSet(cur);
	temp = cur = NULL;
#endif

	return acs_out;
}

/* Data cache update function --- for may analysis only 
 * FIXME: Have to remove the type argument */
static acs_p** update_A(acs_p** acs_in, ric_p addr, ANALYSIS_T type)
{
	mem_blk_set_t* mem_blk, *iter;
	acs_p** acs_out;
	int set;
	acs_p* temp, *cur;
	int i;

	/* Get corresponding memory blocks for set of
	 * addresses */
	 mem_blk = getMemoryBlocks(addr);	  
    set = GET_SET(mem_blk->block);
	
	 acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
	 memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));

	 for(i = 0; i < MAX_CACHE_SET; i++)
		  acs_out[i] = copy_cache(acs_in[i]);

	/* Join over all possible range of memory blocks
	 * referenced. We should keep some parameter here
	 * which would determine the depth of update 
	 * operation for a particular memory access */
	 cur = acs_out[set];
	 acs_out[set] = update_singleton(acs_in[set], mem_blk);
#ifdef MEM_FREE
	 freeCacheSet(cur);	  
	 cur = NULL;
#endif

	 if(getCardinality(mem_blk) > 1)
	 {
		for(iter = mem_blk->next; iter; iter = iter->next)
		{
		  set = GET_SET(iter->block);
		  temp = update_singleton(acs_in[set], iter);
		  cur = acs_out[set];
		  acs_out[set] = joinCache(acs_out[set], temp, type); 

#ifdef MEM_FREE
		  freeCacheSet(cur);
		  freeCacheSet(temp);
		  temp = cur = NULL;
#endif
		}
	}

#ifdef MEM_FREE
	freeMemBlock(mem_blk);
	mem_blk = NULL;
#endif

	return acs_out;
}

/* Check whether two abstract cache blocks are same or not */
static int is_same_cache_block(acs_p acs1, acs_p acs2)
{
	mem_blk_set_t* iter;

	if(!acs1 && !acs2)
		return 1;
	if(!acs1 || !acs2)
		return 0;
	
	for(iter = acs1->mem_blk_h; iter; iter = iter->next)
	{
		if(!isResident(acs2->mem_blk_h, iter))
		  return 0;
	}

	if(getCardinality(acs1->mem_blk_h) == 
		  getCardinality(acs2->mem_blk_h))
		return 1;

	return 0;	
}

/* Check whether two abstract cache states are identical */
static int checkEquality(acs_p* acs1, acs_p* acs2)
{
	int i;

	if(!acs1 && !acs2)
		return 1;
	if(!acs1 || !acs2)
		return 0;  
	for(i = 0; i <= CACHE_SET_SIZE; i++)
	{
		if(! acs1[i] && !acs2[i])
		  continue;
		else if(!acs1[i] || !acs2[i])
		  return 0;
		else if(!is_same_cache_block(acs1[i], acs2[i]))
		  return 0;
	}

	return 1;
}

/* Transfer/update function for l2 instruction abstract cache */
static void transforml2InstCacheState(tcfg_node_t* bbi, int* change_flag, ANALYSIS_T type)
{
	de_inst_t* inst;
	int n_inst;
	int base, imm, index;
	char* isa_name;
	ric_p addr;
	acs_p** acs_out = NULL;
	acs_p** cur_acs;
	acs_p* cur_acs_set;
   int k;

	assert(bbi);
	assert(bbi->bb);
	inst = bbi->bb->code;

	for(n_inst = 0; n_inst < bbi->bb->num_inst; n_inst++)
	{
		 isa_name = isa[inst->op_enum].name;
		 
		 /* Save a copy to check the equality between the 
		  * previous state and the updated state. This is 
		  * important for the iterative computaion */
		  acs_out = (acs_p **) malloc(MAX_CACHE_SET * sizeof(acs_p *));
		  memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		 
		  if(!inst->acs_out)
		  {
		    inst->acs_out = (acs_p **) malloc(MAX_CACHE_SET * sizeof(acs_p *));
		    memset(inst->acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		  }
		  
		  for(k = 0; k < MAX_CACHE_SET; k++)
		  {
		    acs_out[k] = copy_cache(inst->acs_out[k]);

		    if(!inst->acs_out[k])
		    {
			   inst->acs_out[k] = makeCacheSet();
		    }
		  }
		 
		 /* Use cache access classification method */
		 if(inst->inst_access == ALL_MISS)
		 {
			cur_acs = inst->acs_out;		 
		  	inst->acs_out = update_abs_inst(inst->acs_in, inst->addr);

#ifdef MEM_FREE
		   freeCacheState(cur_acs);
			cur_acs = NULL;
#endif
		 }	
		 else if(inst->inst_access == NOT_CLASSIFIED || 
					 inst->inst_access == PS)
		 {
			cur_acs = inst->acs_out;		 
		  	inst->acs_out = update_abs_inst(inst->acs_in, inst->addr);
		   
			for(k = 0; k < MAX_CACHE_SET; k++) {
			  cur_acs_set = inst->acs_out[k];		 
		     inst->acs_out[k] = joinCache(inst->acs_in[k], inst->acs_out[k], type);
#ifdef MEM_FREE
			  freeCacheSet(cur_acs_set);
			  cur_acs_set = NULL;
#endif
			}

#ifdef MEM_FREE
		   freeCacheState(cur_acs);
			cur_acs = NULL;
#endif
		 }
		 /* L2 cache is not accessed at all. So no change in 
		  * abstract cache state */
		 else
		 {
		   for(k = 0; k < MAX_CACHE_SET; k++) {
			  cur_acs_set = inst->acs_out[k];		 
			  inst->acs_out[k] = copy_cache(inst->acs_in[k]);		 
#ifdef MEM_FREE
			  freeCacheSet(cur_acs_set);
			  cur_acs_set = NULL;
#endif
		   }
       }

		 /* check whether the abstract cache states change or not */
		 for(k = 0; k < MAX_CACHE_SET; k++)
			*change_flag |= !checkEquality(inst->acs_out[k], acs_out[k]);


		 /* Linking incoming and outgoing abstract cache states of two 
		  * consecutive basic blocks */
		 if(n_inst < bbi->bb->num_inst - 1)
		 {
			 de_inst_t* next_inst = inst + 1;
		    for(k = 0; k < MAX_CACHE_SET; k++)
			   next_inst->acs_in[k] = inst->acs_out[k];
			 inst++;
		 }	 
	}
}

/* Transfer/update function for instruction abstract cache */
static void transformInstCacheState(tcfg_node_t* bbi, int* change_flag)
{
	de_inst_t* inst;
	int n_inst;
	int base, imm, index;
	char* isa_name;
	ric_p addr;
	acs_p** acs_out;
	acs_p** cur_acs;
   int k;

	assert(bbi);
	assert(bbi->bb);
	inst = bbi->bb->code;

	for(n_inst = 0; n_inst < bbi->bb->num_inst; n_inst++)
	{
		 isa_name = isa[inst->op_enum].name;
		 
		/* Save a copy to check the equality between the 
		 * previous state and the updated state. This is 
		 * important for the iterative computaion */
		 acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
		 memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		 
		 if(!inst->acs_out)
		 {
		    inst->acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
		    memset(inst->acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		 }
		 
		 for(k = 0; k < MAX_CACHE_SET; k++) {
		   acs_out[k] = copy_cache(inst->acs_out[k]);

		   if(!inst->acs_out[k])
		   {
			  inst->acs_out[k] = makeCacheSet();
		   }
		 }
		 
		 cur_acs = inst->acs_out;
		 inst->acs_out = update_abs_inst(inst->acs_in, inst->addr);

#ifdef MEM_FREE
		 freeCacheState(cur_acs);
       cur_acs = NULL;
#endif
		
		 /* checking equality of two abstract cache states */
		 for(k = 0; k < MAX_CACHE_SET; k++)
		   *change_flag |= !checkEquality(inst->acs_out[k], acs_out[k]);
		 
		 /* linking incoming and outgoing abstract cache states of two 
		  * consecutive basic blocks */ 
		 if(n_inst < bbi->bb->num_inst - 1)
		 {
			 de_inst_t* next_inst = inst + 1;
			 
			 for(k = 0; k < MAX_CACHE_SET; k++)
			   next_inst->acs_in[k] = inst->acs_out[k];
			 
			 inst++;
		 }	 
	}
}

/* Transfer/update functions for unified caches */
static void transformUnifiedCacheState(tcfg_node_t* bbi, int* change_flag, ANALYSIS_T type)
{
	de_inst_t* inst;
	int n_inst;
	int base, imm, index;
	char* isa_name;
	ric_p addr;
	acs_p** acs_out;
	acs_p** temp;
	acs_p* cur;
	int k;

	assert(bbi);
	assert(bbi->bb);
	inst = bbi->bb->code;

	/* Traverse through all the instructions of this basic block */	  
	for(n_inst = 0; n_inst < bbi->bb->num_inst; n_inst++)
	{
		 isa_name = isa[inst->op_enum].name;
		 
		 /* Save a copy to check the equality between the previous state and the 
		  * updated state. This is important for the iterative computaion */
		 acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
		 memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		 
		 if(!inst->acs_out)
		 {
		    inst->acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
		    memset(inst->acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		 }
		 
		 /* copy all cache sets */
		 for(k = 0; k < MAX_CACHE_SET; k++)
		 {
		   acs_out[k] = copy_cache(inst->acs_out[k]);

		   if(!inst->acs_out[k])
		   {
			  inst->acs_out[k] = makeCacheSet();
		   }
		 }	
		 
		 /* Use cache access classification method and update unified
		  * cache state for instruction reference */
		 if(inst->inst_access == ALL_MISS)
		 {
		  	temp = update_abs_inst(inst->acs_in, inst->addr);
		 }	
		 else if(inst->inst_access == NOT_CLASSIFIED || inst->inst_access == PS)
		 {
		  	temp = update_abs_inst(inst->acs_in, inst->addr);
		   
			for(k = 0; k < MAX_CACHE_SET; k++) {
				cur = temp[k];		 
		  		temp[k] = joinCache(inst->acs_in[k], temp[k], type);
#ifdef MEM_FREE
		      freeCacheSet(cur);
				cur = NULL;
#endif
		   }	
		 }	
		 else
		 {
			temp = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
			memset(temp, 0, MAX_CACHE_SET * sizeof(acs_p *));
		   
			for(k = 0; k < MAX_CACHE_SET; k++) {
				temp[k] = copy_cache(inst->acs_in[k]);		 
			}
		 }	
		 
		 /* Update unified cache state for memory store operation */
		 if(isStoreInst(isa_name))
		 {
			 /* for base register addressing mode */		 
			 if(inst->num_in <= 2)
			 {
				imm = inst->imm;
				base = *(inst->in + 1);
				addr = getAddrBaseOffset(inst, base, imm, opt);	 
			 }	
			 
			 /* for base indexing addressing mode */
			 else if(inst->num_in == 3)
			 {
				base = *(inst->in + 1);
				index = *(inst->in + 2);
				addr = getAddrBaseIndex(inst, base, index, opt);
			 }
			 
			 /* Unified cache is definitely accessed for all miss l1 
			  * cache accesses */
		    if(inst->data_access == ALL_MISS)
			 {
				acs_p** cur_acs = inst->acs_out;

				if(type == PERSISTENCE)	 
					inst->acs_out = update_Reinherd(temp, addr);
				else if(type == MUST)	
					inst->acs_out = update_must(temp, addr, type);
				else if(type == MAY)	
					inst->acs_out = update_A(temp, addr, MAY);

#ifdef MEM_FREE
				freeCacheState(cur_acs);	 		 
				cur_acs = NULL;
#endif		 	 
			 }	
			 else if(inst->data_access == NOT_CLASSIFIED || inst->data_access == PS)
			 {
				acs_p** cur_acs = inst->acs_out;
				acs_p* cur_acs_set;
				
				if(type == PERSISTENCE)	 
					inst->acs_out = update_Reinherd(temp, addr);
				else if(type == MUST)	
					inst->acs_out = update_must(temp, addr, MUST);
				else if(type == MAY)	
					inst->acs_out = update_A(temp, addr, MAY);

#ifdef MEM_FREE
				freeCacheState(cur_acs);
				cur_acs = NULL;
#endif

				for(k = 0; k < MAX_CACHE_SET; k++)
				{
				  cur_acs_set = inst->acs_out[k];	 	 
				  inst->acs_out[k] = joinCache(inst->acs_out[k], temp[k], type);
#ifdef MEM_FREE
				  freeCacheSet(cur_acs_set);
				  cur_acs_set = NULL;
#endif
				}  
			 }
			 else
			 {
				acs_p* cur_acs_set;

				for(k = 0; k < MAX_CACHE_SET; k++)
				{
				  cur_acs_set = inst->acs_out[k];	 	 
				  inst->acs_out[k] = copy_cache(temp[k]); 
#ifdef MEM_FREE
				  freeCacheSet(cur_acs_set);
				  cur_acs_set = NULL;
#endif
				}  
			 }	
		 } 
		 
		 /* Update unified cache state for memory load operation */
		 else if(isLoadInst(isa_name))
		 {
			 if(inst->num_in <= 1)
			 {
				imm = inst->imm;
				base = *(inst->in);
				addr = getAddrBaseOffset(inst, base, imm, opt);	 
			 }	
			 else if(inst->num_in == 2)
			 {
				base = *(inst->in);
				index = *(inst->in + 1);
				addr = getAddrBaseIndex(inst, base, index, opt);
			 }
			 if(inst->data_access == ALL_MISS)
			 {
				acs_p** cur_acs;

				cur_acs = inst->acs_out;

				if(type == PERSISTENCE)	 
					inst->acs_out = update_Reinherd(temp, addr);
				else if(type == MUST)	
					inst->acs_out = update_must(temp, addr, MUST);
				else if(type == MAY)	
					inst->acs_out = update_A(temp, addr, MAY);

#ifdef MEM_FREE
				freeCacheState(cur_acs);
				cur_acs = NULL;
#endif
			 }	
			 else if(inst->data_access == NOT_CLASSIFIED || inst->data_access == PS)
			 {
				acs_p** cur_acs;
				acs_p* cur_acs_set;

				cur_acs = inst->acs_out;

				if(type == PERSISTENCE)	 
					inst->acs_out = update_Reinherd(temp, addr);
				else if(type == MUST)	
					inst->acs_out = update_must(temp, addr, MUST);
				else if(type == MAY)	
					inst->acs_out = update_A(temp, addr, MAY);

#ifdef MEM_FREE
				freeCacheState(cur_acs);
				cur_acs = NULL;
#endif

				for(k = 0; k < MAX_CACHE_SET; k++)
				{
				  cur_acs_set = inst->acs_out[k];
				  inst->acs_out[k] = joinCache(inst->acs_out[k], temp[k], type);

#ifdef MEM_FREE
				  freeCacheSet(cur_acs_set);
				  cur_acs_set = NULL;
#endif
				}  
			 }
			 else
			 {
				acs_p* cur_acs_set;
				
				for(k = 0; k < MAX_CACHE_SET; k++) {
				  cur_acs_set = inst->acs_out[k];
				  inst->acs_out[k] = copy_cache(temp[k]); 
#ifdef MEM_FREE
				  freeCacheSet(cur_acs_set);
				  cur_acs_set = NULL;
#endif
				}  
			 }	
		 } 
		 
		 else
		 {
			acs_p* cur_acs_set;
			
			/* Transparent to cache update. No load 
			 * and store instructions, just copy the 
			 * incoming cache state */
			for(k = 0; k < MAX_CACHE_SET; k++) {
			  cur_acs_set = inst->acs_out[k];
			  inst->acs_out[k] = copy_cache(temp[k]);		 
#ifdef MEM_FREE
			  freeCacheSet(cur_acs_set);
			  cur_acs_set = NULL;
#endif
			}  
		 }
	
		 /* Free up the memory required for intermediary abstract cache state */	  
		 #ifdef MEM_FREE
		  freeCacheState(temp);
		  temp = NULL;
		 #endif
		 
		 /* Check whether any abstract cache set has been changed or not */
		 for(k = 0; k < MAX_CACHE_SET; k++)
		   *change_flag |= !checkEquality(inst->acs_out[k], acs_out[k]);
		
		 /* Link the incoming and outgoing abstract cache states of two consecutive 
		  * instructions in a single basic block */
		 if(n_inst < bbi->bb->num_inst - 1)
		 {
			 de_inst_t* next_inst = inst + 1;
		    
			 next_inst->acs_in = inst->acs_out;
			 /* for(k = 0; k < MAX_CACHE_SET; k++)
			   next_inst->acs_in[k] = inst->acs_out[k]; */
			 
			 inst++;
		 }	 
	}
}

/* Abstract transfer/update functions for load/store instruction */
/* Argument 3 (type ANALYSIS_T) specifies the type of the analysis 
 * applied --- Must, May or Persistence */
static void transformDataCacheState(tcfg_node_t* bbi, int* change_flag, ANALYSIS_T type)
{
	de_inst_t* inst;
	int n_inst;
	int base, imm, index;
	char* isa_name;
	ric_p addr;
	acs_p** acs_out;
	acs_p** temp;
	int k;

	assert(bbi);
	assert(bbi->bb);
	inst = bbi->bb->code;

#ifdef _DEBUG
	printf("Transfer starts for bbi %d\n", bbi->id);
#endif

	for(n_inst = 0; n_inst < bbi->bb->num_inst; n_inst++)
	{
		 isa_name = isa[inst->op_enum].name;
		 
		 /* Save a copy to check the equality between the previous state and the 
		  * updated state. This is important for the fix-point iterative 
		  * computation */
		 acs_out = (acs_p **) malloc(MAX_CACHE_SET * sizeof(acs_p *));
		 memset(acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));

		 if(!inst->acs_out)
		 {
		    inst->acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
		    memset(inst->acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		 }
		 
		 for(k = 0; k < MAX_CACHE_SET; k++)
		 {
		   acs_out[k] = copy_cache(inst->acs_out[k]);
		   if(!inst->acs_out[k])
		   {
			  inst->acs_out[k] = makeCacheSet();
		   }
		 }

		 /* Update data cache state for memory store operation */
		 if(isStoreInst(isa_name))
		 {
			 /* save the old content of abstract cache state */		 
			 temp = inst->acs_out;
			 
			 /* If it is base register addressing mode */
			 if(inst->num_in <= 2)
			 {
				imm = inst->imm;
				base = *(inst->in + 1);
				addr = getAddrBaseOffset(inst, base, imm, opt);	 
				if(type == PERSISTENCE)
		  			inst->acs_out = update_Reinherd(inst->acs_in, addr);
				else if(type == MUST)
		  			inst->acs_out = update_must(inst->acs_in, addr, MUST);
				else if(type == MAY)	
		  			inst->acs_out = update_A(inst->acs_in, addr, MAY);
				else 
					prerr("a fatal error occured - undefined analysis type"); 
			 }
			 /* if it is a base index addressing mode */
			 else if(inst->num_in == 3)
			 {
				base = *(inst->in + 1);
				index = *(inst->in + 2);
				addr = getAddrBaseIndex(inst, base, index, opt);
				if(type == PERSISTENCE)
		  			inst->acs_out = update_Reinherd(inst->acs_in, addr);
				else if(type == MUST)
		  			inst->acs_out = update_must(inst->acs_in, addr, MUST);
				else if(type == MAY)
		  			inst->acs_out = update_A(inst->acs_in, addr, MAY);
				else
					prerr("a fatal error occured - undefined analysis type"); 
			 }
#ifdef MEM_FREE
		    freeCacheState(temp);
			 temp = NULL;
#endif
		 } 
		 
		 /* Update data cache state for memory load operation */
		 else if(isLoadInst(isa_name))
		 {
			 temp = inst->acs_out;
		
			 /* For base register addressing mode */		 
			 if(inst->num_in <= 1)
			 {
				imm = inst->imm;
				base = *(inst->in);
				addr = getAddrBaseOffset(inst, base, imm, opt);	 
				if(type == PERSISTENCE)
		  			inst->acs_out = update_Reinherd(inst->acs_in, addr);
				else if(type == MUST)
		  			inst->acs_out = update_must(inst->acs_in, addr, MUST);
				else if(type == MAY)
		  			inst->acs_out = update_A(inst->acs_in, addr, MAY);
				else
					prerr("a fatal error occured - undefined analysis type"); 
			 }
			 
			 /* For base index addressing mode */
			 else if(inst->num_in == 2)
			 {
				base = *(inst->in);
				index = *(inst->in + 1);
				addr = getAddrBaseIndex(inst, base, index, opt);
				if(type == PERSISTENCE)
		  			inst->acs_out = update_Reinherd(inst->acs_in, addr);
				else if(type == MUST)
		  			inst->acs_out = update_must(inst->acs_in, addr, MUST);
				else if(type == MAY)
		  			inst->acs_out = update_A(inst->acs_in, addr, MAY);
				else
					prerr("a fatal error occured - undefined analysis type"); 
			 }
#ifdef MEM_FREE
		  freeCacheState(temp);
		  temp = NULL;
#endif
		 }
		 else 
		 {
			 /* Transparent to cache update. No load and store instructions */
		    for(k = 0; k < MAX_CACHE_SET; k++) {
				acs_p* cur_acs_set;

				cur_acs_set = inst->acs_out[k];
			   inst->acs_out[k] = copy_cache(inst->acs_in[k]);		 
#ifdef MEM_FREE
				freeCacheSet(cur_acs_set);
#endif
			 }	
		 }
		 
		 /* Check the equality between the old and current abstract cache state 
		  */ 
		 for(k = 0; k < MAX_CACHE_SET; k++)
		    *change_flag |= !checkEquality(inst->acs_out[k], acs_out[k]);
	
		 /* Link the outgoing and incoming cache states of two consecutive 
		  * instructions in a basic block */  
		 if(n_inst < bbi->bb->num_inst - 1)
		 {
			 de_inst_t* next_inst = inst + 1;
			 
			 next_inst->acs_in = inst->acs_out;

			 /* for(k = 0; k < MAX_CACHE_SET; k++)
			   next_inst->acs_in[k] = inst->acs_out[k]; */
			 
			 inst++;
		 }	 

		 /* free up the memory for old abstract cache state */
#ifdef MEM_FREE
		 freeCacheState(acs_out);	  
		 acs_out = NULL;
#endif
	}

#ifdef _DEBUG
	printf("Transfer ends for bbi %d\n", bbi->id);
#endif
}

/* Perform join operation on two abstract cache states */
/* Join operation depends on the direction of cache analysis 
 * (may, must, persistence). This direction of cache analysis 
 * is provided through the argument "type" */
static void JoinCacheState(tcfg_node_t* pred, tcfg_node_t* bbi, int type)
{
	de_inst_t* f_inst, *l_inst;	
	acs_p* acs[MAX_CACHE_SET];
	acs_p* pred_acs[MAX_CACHE_SET];
	acs_p* ret[MAX_CACHE_SET];
	acs_p temp = NULL;
	mem_blk_set_t* iter;
	acs_p* free_p;
	int i,j,k;

	if(!pred)
		return;
	
	/* First instruction of this basic block */
	f_inst = bbi->bb->code;
	
	/* Last instruction of predecessor basic block */	  
	l_inst = pred->bb->code + pred->bb->num_inst - 1;

	/* Get the abstract cache state at the exit point of the last
	 * instruction of the predecessor basic block */
	for(i = 0; i < MAX_CACHE_SET; i++)
	{
		/* no cache state out of the last instruction of an input basic 
		 * block */
		if(!l_inst->acs_out[i])
		  continue;
		free_p = f_inst->acs_in[i];  
		f_inst->acs_in[i] = joinCache(f_inst->acs_in[i], l_inst->acs_out[i], type);
		
		/* Free the old abstract cache state */
#ifdef MEM_FREE
		freeCacheSet(free_p);  
		free_p = NULL;
#endif
	}	
}

/* This procedure is called before doing each cache analysis. Cache 
 * analysis is initialized with allocating memory for abstract cache 
 * states. After finishing one analysis and before starting a next 
 * one, this allocated memory should be reclaimed for better memory 
 * management. "cleanupCache" procedure is used for this purpose 
 */ 
static void initializeCache(tcfg_node_t* bbi)
{
	de_inst_t* inst = bbi->bb->code;
	int i, j;

	assert(inst);
	
   for(j = 0; j < bbi->bb->num_inst; j++)
	{
		inst->acs_in = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
	   memset(inst->acs_in, 0, MAX_CACHE_SET * sizeof(acs_p *));
		inst->acs_out = (acs_p **)malloc(MAX_CACHE_SET * sizeof(acs_p *));
	   memset(inst->acs_out, 0, MAX_CACHE_SET * sizeof(acs_p *));
		inst++;
	}
   inst = bbi->bb->code;
	for(i = 0; i < MAX_CACHE_SET; i++)
	{
		inst->acs_in[i] = makeCacheSet();
	}
}

/* For a given instruction address classify it as all miss
 * or non-classified */
static void categorize_inst_all_miss(de_inst_t* inst)
{
	mem_blk_set_t temp;
	int h1, h2;	  

	temp.block = GET_MEM(inst->addr);
   temp.next = NULL;

	h1 = checkForPresence(inst->acs_in, &temp);
	temp.block = GET_MEM(inst->addr + SIZE_OF_WORD);
	h2 = checkForPresence(inst->acs_in, &temp);
	
	if(!h1 || !h2)
	{
		inst->inst_access = ALL_MISS;
#ifdef _DEBUG
		printf("Instruction all miss at %x (block %d)\n", inst->addr, GET_MEM(inst->addr));
#endif
	}
	else if(inst->inst_access != ALL_HIT)
	{
		inst->inst_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Instruction NOT all miss at %x (block %d)\n", inst->addr, GET_MEM(inst->addr));
#endif
	}	
}

/* For a given instruction addresse classify it as all hit
 * or non-classified */
static void categorize_l2_inst_ps(de_inst_t* inst)
{
	mem_blk_set_t temp;
	int h1, h2;	  

	temp.block = GET_MEM(inst->addr);
   temp.next = NULL;

	h1 = checkForVictim(inst->acs_in, &temp);
	temp.block = GET_MEM(inst->addr + SIZE_OF_WORD);
	h2 = checkForVictim(inst->acs_in, &temp);
	
	if(inst->inst_access == ALL_HIT)
	{
		inst->l2_inst_access = ALL_X;
#ifdef _DEBUG
		printf("Instruction all-hit/PS in L1\n");
#endif
	}
	else if(!h1 && !h2)
	{
		if(inst->l2_inst_access != ALL_HIT)  
		{
		  inst->l2_inst_access = PS;
#ifdef _DEBUG
		  printf("Instruction(%x) l2 PS recognized\n", inst->addr);
#endif
		  i1_u1_ps += 1;  
		}
	}	  
	else
	{
		inst->l2_inst_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown instruction access in l2 recognized\n");
#endif
	}
}

/* For a given instruction addresse classify it as all hit
 * or non-classified */
static void categorize_l2_inst_hit_miss(de_inst_t* inst)
{
	mem_blk_set_t temp;
	int h1, h2;	  

	temp.block = GET_MEM(inst->addr);
   temp.next = NULL;

	h1 = checkForPresence(inst->acs_in, &temp);
	temp.block = GET_MEM(inst->addr + SIZE_OF_WORD);
	h2 = checkForPresence(inst->acs_in, &temp);
	
	if(inst->inst_access == ALL_HIT)
	{
		inst->l2_inst_access = ALL_X;
#ifdef _DEBUG
		printf("Instruction all-hit/PS in L1\n");
#endif
	}
	else if(h1 && h2)
	{
		inst->l2_inst_access = ALL_HIT;
#ifdef _DEBUG
		printf("Instruction(%x) l2 all hit recognized\n", inst->addr);
#endif
	}	  
	else
	{
		inst->l2_inst_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown instruction access in l2 recognized\n");
#endif
	}
}

/* For a given instruction addresse classify it as all hit
 * or non-classified */
static void categorize_inst_hit_miss(de_inst_t* inst)
{
	mem_blk_set_t temp;
	int h1, h2;	  

	temp.block = GET_MEM(inst->addr);
   temp.next = NULL;

	h1 = checkForPresence(inst->acs_in, &temp);
	temp.block = GET_MEM(inst->addr + SIZE_OF_WORD);
	h2 = checkForPresence(inst->acs_in, &temp);
	
	if(h1 && h2)
	{
		inst->inst_access = ALL_HIT;
#ifdef _DEBUG
		printf("Instruction all hit recognized\n");
#endif
	}	  
	else
	{
		inst->inst_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown instruction access recognized\n");
#endif
	}
}

/* For a given range of addresses and a given load/store instruction
 * classify the load/store as all-miss or non-classified */
static void categorize_all_miss(de_inst_t* inst, ric_p addr)
{
	mem_blk_set_t* temp = getMemoryBlocks(addr);
	int h;	  
	
	h = checkForOnePresence(inst->acs_in, temp);
	
	if(h == 0)
	{
		inst->data_access = ALL_MISS;
#ifdef _DEBUG
		printf("All miss data access recognized at %x\n", inst->addr);
#endif
	}

	free(temp);
}

/* For a given range of addresses and a given load/store instruction
 * classify the load/store as all-hit or non-classified */
static void categorize_u1_ps(de_inst_t* inst, ric_p addr)
{
	mem_blk_set_t* temp = getMemoryBlocks(addr);
	int h;	  
	
	h = checkForVictim(inst->acs_in, temp);
	
	if(inst->data_access == ALL_HIT || inst->data_access == PS)
	{
		inst->u1_data_access = ALL_X;
#ifdef _DEBUG
		printf("All-hit/PS data in L1 recognized\n");
#endif
	}
	else if(!h)
	{
		if(inst->u1_data_access != ALL_HIT)  
		  inst->u1_data_access = PS;
#ifdef _DEBUG
		printf("PS data in unified cache recognized\n");
#endif
		u1_d1_ps += 1;
	}	  
	else
	{
		inst->u1_data_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown data access recognized in unified access\n");
#endif
	}

	free(temp);
}

/* For a given range of addresses and a given load/store instruction
 * classify the load/store as all-hit or non-classified */
static void categorize_u1_hit_miss(de_inst_t* inst, ric_p addr)
{
	mem_blk_set_t* temp = getMemoryBlocks(addr);
	int h;	  
	
	h = checkForPresence(inst->acs_in, temp);
	
	if(inst->data_access == ALL_HIT || inst->data_access == PS)
	{
		inst->u1_data_access = ALL_X;
#ifdef _DEBUG
		printf("All-hit/PS data in L1 recognized\n");
#endif
	}
	else if (h != 0)
	{
		inst->u1_data_access = ALL_HIT;
#ifdef _DEBUG
		printf("All hit data in unified cache recognized\n");
#endif
	}	  
	else
	{
		inst->u1_data_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown data access recognized in unified access\n");
#endif
	}

	free(temp);
}

/* For a given instruction addresse classify it as all hit
 * or non-classified */
static void categorize_inst_ps_nc(de_inst_t* inst)
{
	mem_blk_set_t temp;
	int h1, h2;	  

	temp.block = GET_MEM(inst->addr);
   temp.next = NULL;

	h1 = checkForVictim(inst->acs_in, &temp);
	temp.block = GET_MEM(inst->addr + SIZE_OF_WORD);
	h2 = checkForVictim(inst->acs_in, &temp);
	
	if(inst->inst_access != ALL_HIT && inst->inst_access != ALL_MISS && !h1 && !h2)
	{
		inst->inst_access = PS;
#ifdef _DEBUG
		printf("Instruction(%x) is persistence in cache\n", inst->addr);
#endif
		l1_i1_ps += 1;
	}
	else if((h1 || h2) && inst->inst_access != ALL_MISS)
	{
		inst->inst_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Instruction Not Persistence\n");
#endif
	}	  
}

/* For a given range of addresses and aa given load/store instruction
 * classify the load/store as all-hit or non-classified */
static void categorize_hit_miss(de_inst_t* inst, ric_p addr, int bb)
{
	mem_blk_set_t* temp = getMemoryBlocks(addr);
	int h;	  
	
	h = checkForPresence(inst->acs_in, temp);
	
	if(h != 0)
	{
		inst->data_access = ALL_HIT;
#ifdef _DEBUG
		printf("All hit at instruction %x(BB=%d) recognized\n", inst->addr, bb);
#endif
	}	  
	else
	{
		inst->data_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown data access at %x(BB=%d) recognized\n", inst->addr, bb);
#endif
	}

	free(temp);
}

/* For a given range of addresses and a given load/store instruction
 * classify the load/store as all-hit or non-classified */
static void categorize_ps_data(de_inst_t* inst, ric_p addr)
{
	mem_blk_set_t* temp = getMemoryBlocks(addr);
	int h;	  
	
	h = checkForVictim(inst->acs_in, temp);
	
	if(h != 0)
	{
		inst->data_access = NOT_CLASSIFIED;
#ifdef _DEBUG
		printf("Unknown data access recognized\n");
#endif
	}	  
	else if(inst->data_access != ALL_HIT && inst->data_access != ALL_MISS)
	{
		inst->data_access = PS;
#ifdef _DEBUG
		printf("Persistence data access(%x) recognized\n", inst->addr);
#endif
		l1_d1_ps += 1;  
	}

	free(temp);
}

/* Categorize instruction access patterns. For must analysis :: all hits 
 * or non-classified */
static void categorize_l2_inst_ps_access()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;
			 categorize_l2_inst_ps(inst);
			 
			 /* set the latency for instruction cache miss */
			 if (inst->l2_inst_access == NOT_CLASSIFIED && 
					 (inst->inst_access == NOT_CLASSIFIED ||
					 inst->inst_access == ALL_MISS))
				 tcfg[i]->inst_cache_delay += L2_MISS_PENALTY; 	 
			 
			 /* CHECK */	 
			 else if (inst->l2_inst_access == PS || inst->inst_access == PS)
				 tcfg[i]->n_l2_persistence += 1;	 
			 
			 inst++;
		 }
	}
}

/* Categorize instruction access patterns. For must analysis :: all hits 
 * or non-classified */
static void categorize_l2_inst_access()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;
			 categorize_l2_inst_hit_miss(inst);
			 /* set the latency for instruction cache miss */
			 /* Do this after persistence analysis */
			 /* if(inst->l2_inst_access == NOT_CLASSIFIED)
				 tcfg[i]->inst_cache_delay += L2_MISS_PENALTY; */	 
			 inst++;
		}
	}
}

/* Categorize instruction access patterns. For must analysis :: all hits 
 * or non-classified */
static void categorize_inst_access()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;
			 categorize_inst_hit_miss(inst);
			 /* set the latency for instruction cache miss */
			 /* Set the latency later after persistence analysis */
			 /* if(inst->inst_access == NOT_CLASSIFIED)
				 tcfg[i]->inst_cache_delay += L1_MISS_PENALTY; */	 
			 inst++;
		}
	}
}


/* Categorize instruction access patterns. For persistence analysis 
 * :: persistence or non-classified */
static void categorize_inst_access_ps()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;
			 categorize_inst_ps_nc(inst);
			 
			 /* set the latency for instruction cache miss */
			 if(inst->inst_access == NOT_CLASSIFIED)
				 tcfg[i]->inst_cache_delay += L1_MISS_PENALTY;	 
			 /* Increase the no. of persistence instructions by one */	 
			 else if(inst->inst_access == PS)
				 tcfg[i]->n_persistence += 1;  	 
			 
			 inst++;
		}
	}
}

/* Categorize instruction access patterns. For may analysis :: all miss 
 * or non-classified */
static void categorize_inst_access_miss()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  	 isa_name =  isa[inst->op_enum].name;

			 /* categorize all miss instructions */
			 categorize_inst_all_miss(inst);
			 
			 if(inst->inst_access == ALL_MISS)
				tcfg[i]->inst_cache_delay += L1_MISS_PENALTY;	 
			 
			 inst++;
		}
	}
}

/* Categorize data access patterns. For must analysis :: all hits 
 * or non-classified */
static void categorize_data_access_miss()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	/* Traverse through the tcfg of all instructions */	  
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
					/* check type of store instruction --- with 2 
					 * operand or 3 operands */
					if(inst->num_in <= 2)
					{
					 	imm = inst->imm;
						base = *(inst->in + 1);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_all_miss(inst, addr);
					}	
					else if(inst->num_in == 3)
					{
						base = *(inst->in + 1);
						index = *(inst->in + 2);
						addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_all_miss(inst, addr);
					}
			  }	 
			  else if(isLoadInst(isa_name))
			  {
					/* check type of load instruction --- with 1 
					 * operand or 2 operands */
					if(inst->num_in <= 1)
					{
						imm = inst->imm;
		  				base = *(inst->in);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_all_miss(inst, addr);
					}	
					else if(inst->num_in == 2)
					{
						base = *(inst->in);
					   index = *(inst->in + 1);
					 	addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_all_miss(inst, addr);
					}
			 }
			 else
			 {
					inst->data_access = ALL_HIT; 
			 }
			 /* Add the delay */
			 if(inst->data_access == ALL_MISS)
		  		tcfg[i]->dcache_delay += L1_MISS_PENALTY;
			 inst++;
		}
	}
}

/* Categorize data access patterns. For persistence analysis :: 
 * persistence or non-classified */
static void categorize_data_access_ps()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

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
					/* check type of store instruction --- with 2 
					 * operands or 3 operands */
					if(inst->num_in <= 2)
					{
					 	imm = inst->imm;
						base = *(inst->in + 1);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_ps_data(inst, addr);
					}	
					else if(inst->num_in == 3)
					{
						base = *(inst->in + 1);
						index = *(inst->in + 2);
						addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_ps_data(inst, addr);
					}
			  }	 
			  else if(isLoadInst(isa_name))
			  {
					/* check type of load instruction --- with 1 
					 * operand or 2 operands */
					if(inst->num_in <= 1)
					{
						imm = inst->imm;
		  				base = *(inst->in);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_ps_data(inst, addr);
					}	
					else if(inst->num_in == 2)
					{
						base = *(inst->in);
					   index = *(inst->in + 1);
					 	addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_ps_data(inst, addr);
					}
			 }
			 else
			 {
					inst->data_access = ALL_HIT; 
			 }
			 /* set the latency for data cache miss */
			 if(inst->data_access == NOT_CLASSIFIED)
				 tcfg[i]->dcache_delay += L1_MISS_PENALTY;	 
			 /* set number of persistence data accesses 
			  * in the basic block */
			 else if(inst->data_access == PS)
				 tcfg[i]->n_data_persistence += getCardinality(
					 getMemoryBlocks(addr));	 
			 inst++;
		}
	}
}

/* Categorize data access patterns. For must analysis :: all hits 
 * or non-classified */
static void categorize_data_access()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

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
					/* check type of store instruction --- with 2 
					 * operands or 3 operands */
					if(inst->num_in <= 2)
					{
					 	imm = inst->imm;
						base = *(inst->in + 1);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_hit_miss(inst, addr, i);
					}	
					else if(inst->num_in == 3)
					{
						base = *(inst->in + 1);
						index = *(inst->in + 2);
						addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_hit_miss(inst, addr, i);
					}
			  }	 
			  else if(isLoadInst(isa_name))
			  {
					/* check type of load instruction --- with 1 
					 * operand or 2 operands */
					if(inst->num_in <= 1)
					{
						imm = inst->imm;
		  				base = *(inst->in);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_hit_miss(inst, addr, i);
					}	
					else if(inst->num_in == 2)
					{
						base = *(inst->in);
					   index = *(inst->in + 1);
					 	addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_hit_miss(inst, addr, i);
					}
			 }
			 else
			 {
					inst->data_access = ALL_HIT; 
			 }
			 /* set the latency for data cache miss */
			 /* The delay will be set later after persistence 
			  * analysis */
			 /* if(inst->data_access == NOT_CLASSIFIED)
				 tcfg[i]->dcache_delay += L1_MISS_PENALTY; */ 	 
			 inst++;
		}
	}
}

/* Categorize unified cache access patterns. For must analysis ::
 * all hits or non-classified */
static void categorize_unified_ps_cache_access()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  		isa_name =  isa[inst->op_enum].name;

				/* First categorize the instruction hit/miss 
				 * categorization */
				categorize_l2_inst_ps(inst);

			  /* For a memory load/store instruction find out the 
			   * memory hit/miss classification from the abstract 
			   * cache state */
				if(isStoreInst(isa_name))
				{
					/* Check type of store instruction --- with 2 operands 
					 * or 3 operands */
					if(inst->num_in <= 2)
					{
					 	imm = inst->imm;
						base = *(inst->in + 1);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_u1_ps(inst, addr);
					}	
					else if(inst->num_in == 3)
					{
						base = *(inst->in + 1);
						index = *(inst->in + 2);
						addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_u1_ps(inst, addr);
					}
			  }	 
			  else if(isLoadInst(isa_name))
			  {
					/* Check type of load instruction --- with 1 operand or 2 
					 * operands */
					if(inst->num_in <= 1)
					{
						imm = inst->imm;
		  				base = *(inst->in);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_u1_ps(inst, addr);
					}	
					else if(inst->num_in == 2)
					{
						base = *(inst->in);
					   index = *(inst->in + 1);
					 	addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_u1_ps(inst, addr);
					}
			 }
			 else
			 {
					inst->u1_data_access = ALL_X; 
			 }
			 /* Set the latency for instruction miss */
			 /* Set the latency later -- after persistence analysis */
			 if(inst->l2_inst_access == NOT_CLASSIFIED)
				 tcfg[i]->inst_cache_delay += L2_MISS_PENALTY;	 
			 else if(inst->l2_inst_access == PS)
				 tcfg[i]->n_u1_persistence += 1;	 
			 /* set the latency for data miss */
			 /* For persistence data access number of memory blocks in the
			  * set must be considered */
			 if(inst->u1_data_access == NOT_CLASSIFIED)
				 tcfg[i]->dcache_delay += L2_MISS_PENALTY; 	 
			 else if(inst->u1_data_access == PS)
				 tcfg[i]->n_u1_data_persistence += getCardinality(
					 getMemoryBlocks(addr)); 	 

			 inst++;
		}
	}
}

/* Categorize unified cache access patterns. For must analysis ::
 * all hits or non-classified */
static void categorize_unified_cache_access()
{
	tcfg_node_t* bbi;
	de_inst_t* inst;
	char* isa_name;
	ric_p addr;
	int i, j;
	int rs, rt, d, imm, base, offset, index;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		 inst = tcfg[i]->bb->code;
		 
		 for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		 {
		  		isa_name =  isa[inst->op_enum].name;

				/* First categorize the instruction hit/miss 
				 * categorization */
				categorize_l2_inst_hit_miss(inst);

			  /* For a memory load/store instruction find out the 
			   * memory hit/miss classification from the abstract 
			   * cache state */
				if(isStoreInst(isa_name))
				{
					/* check which type of store instruction...with 2 operands 
					 * or with 3 operands */
					if(inst->num_in <= 2)
					{
					 	imm = inst->imm;
						base = *(inst->in + 1);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_u1_hit_miss(inst, addr);
					}	
					else if(inst->num_in == 3)
					{
						base = *(inst->in + 1);
						index = *(inst->in + 2);
						addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_u1_hit_miss(inst, addr);
					}
			  }	 
			  else if(isLoadInst(isa_name))
			  {
					/* check which type of load instruction...with 1 operand
					 * or with 2 operands */
					if(inst->num_in <= 1)
					{
						imm = inst->imm;
		  				base = *(inst->in);
					 	addr = getAddrBaseOffset(inst, base, imm, opt);	 
						categorize_u1_hit_miss(inst, addr);
					}	
					else if(inst->num_in == 2)
					{
						base = *(inst->in);
					   index = *(inst->in + 1);
					 	addr = getAddrBaseIndex(inst, base, index, opt);
						categorize_u1_hit_miss(inst, addr);
					}
			 }
			 else
			 {
					inst->u1_data_access = ALL_X; 
			 }
			 /* Set the latency for instruction miss */
			 /* Set the latency later -- after persistence analysis */
			 /* if(inst->l2_inst_access == NOT_CLASSIFIED)
				 tcfg[i]->inst_cache_delay += L2_MISS_PENALTY;*/	 
			 /* set the latency for data miss */
			 /* if(inst->u1_data_access == NOT_CLASSIFIED)
				 tcfg[i]->dcache_delay += L2_MISS_PENALTY; */	 
			 inst++;
		}
	}
}

/* clean up the cache, this section is called before starting 
 * a new cache analysis */
static void cleanupCache()
{
	int i,j,k;
	de_inst_t* inst;
	de_inst_t* prev_inst;

	for(i = 0; i < num_tcfg_nodes; i++)
	{
		inst = tcfg[i]->bb->code;
		prev_inst = NULL;

		for(j = 0; j < tcfg[i]->bb->num_inst; j++)
		{
#ifdef MEM_FREE
		   freeCacheState(inst->acs_out);
#endif
			inst->acs_out = NULL;
			/* CAUTION :::: since the input abstract cache state of previous 
			 * instruction and the output abstract cache state of this 
			 * instruction are connected, there is no need to do a double memory 
			 * free */
			if (prev_inst) 
				prev_inst->acs_in = NULL;
			prev_inst = inst;
			inst++;
		}
	}	
}

/* Persistence cache analysis for data cache */
static void analyze_data_cache_ps()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];
	
	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], PERSISTENCE);
				}
				transformDataCacheState(tcfg[i], &change_flag, PERSISTENCE);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpCache();
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * data cache access */
	categorize_data_access_ps();	  
	
	/* Clean up the cache before another analysis starts  */
	cleanupCache();
}

/* must analysis of data cache */
static void analyze_data_cache_must()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];
	
	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], MUST);
				}
				transformDataCacheState(tcfg[i], &change_flag, MUST);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpCache();
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * data cache access */
	categorize_data_access();	  
	
	/* clean up the cache before another analysis starts */
	cleanupCache();
}

/* may analysis of data cache */
static void analyze_data_cache_may()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];
	
	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], MAY);
				}
				transformDataCacheState(tcfg[i], &change_flag, MAY);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpCache();
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * data cache access */
	categorize_data_access_miss();	  

	/* clean up the cache before another analysis starts */
	cleanupCache();
}

/* abstract interpretation based data cache analysis */
void analyze_data_cache()
{
	X = assoc_dl1; Y = nsets_dl1; B = bsize_dl1; 
	
	/* Set the L1 miss penalty, if unified cache is enabled 
	 * then unified cache latency otherwise memory latency 
	 */ 
	if(enable_ul2cache) {
	  l1 = cache_dl2_lat; 
	  l2 = mem_lat[0];  
	}
	else 
	  l1 = l2 = mem_lat[0]; 
	
	/* Must analysis for data cache */	  
	analyze_data_cache_must();	  
	/* May analysis for data cache */	  
	analyze_data_cache_may();
	/* Persistence analysis for data cache */
   analyze_data_cache_ps();

#ifdef _DEBUG	
	/* Print classification of data accesses in the program */	  
	print_classification_data();
	printf("Data cache analysis iteration count = %d\n", analysis);
#endif	

}

/* persistence analysis of l2 instruction cache */
static void analyze_abs_l2_instr_cache_ps()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];

	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], PERSISTENCE);
				}
				transforml2InstCacheState(tcfg[i], &change_flag, PERSISTENCE);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpInstCache();
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * instruction cache access */
	categorize_l2_inst_ps_access();	  
	
	/* clean up the cache before another analysis starts */
	cleanupCache();
}

/* must analysis of l2 instruction cache */
static void analyze_abs_l2_instr_cache_must()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];

	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], MUST);
				}
				transforml2InstCacheState(tcfg[i], &change_flag, MUST);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpInstCache();
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * instruction cache access */
	categorize_l2_inst_access();	  
	
	/* Clean up the cache before the analysis starts */	  
	cleanupCache();
}

/* must analysis of l1 instruction cache */
static void analyze_abs_instr_cache_must()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];


	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], MUST);
				}
				transformInstCacheState(tcfg[i], &change_flag);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpInstCache();
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * instruction cache access */
	categorize_inst_access();	  
	
	/* Clean up the cache aka free the memory required for cache 
	 * analysis */
	cleanupCache();
}

/* may analysis of l1 instruction cache */
static void analyze_abs_instr_cache_may()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];

	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], MAY);
				}
				transformInstCacheState(tcfg[i], &change_flag);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpInstCache();
#endif	
	
	/* Now categorize the all miss/non-classified access for
	 * instruction cache access */
	 categorize_inst_access_miss();	  
	
	/* Clean up the cache before another analysis starts */	  
	cleanupCache();
}

/* persistence analysis of l1 instruction cache */
static void analyze_abs_instr_cache_ps()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];

	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], PERSISTENCE);
				}
				transformInstCacheState(tcfg[i], &change_flag);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	dumpInstCache();
#endif	
	
	/* Now categorize the all miss/non-classified access for
	 * instruction cache access */
	 categorize_inst_access_ps();	  
	
	/* Clean up the cache first */	  
	cleanupCache();
}

/* abstract interpretation based analysis for instruction cache */
void analyze_abs_instr_cache()
{
	X = assoc; Y = nsets; B = bsize; l1 = cache_dl2_lat; l2 = mem_lat[0];  
	/* Do must analysis in instruction cache */	  
	analyze_abs_instr_cache_must();
	/* Do may analysis in instruction cache */	  
	analyze_abs_instr_cache_may();
	/* Do persistence analysis for instruction cache */
	analyze_abs_instr_cache_ps();

#ifdef _DEBUG
	/* Print categorization of all instructions */	  
	print_classification();
#endif
	
	if(enable_il2cache)
	{
		  X = assoc_l2; Y = nsets_l2; B = bsize_l2; l1 = cache_il2_lat; l2 = mem_lat[0];  
		  /* Do must analysis in L2 instruction cache */	  
		  analyze_abs_l2_instr_cache_must();
		  /* Do persistence analysis in L2 instruction cache */	  
		  analyze_abs_l2_instr_cache_ps();
	}	  
}

/* persistence analysis of unified l2 cache */
void analyze_unified_cache_ps()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];

	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], PERSISTENCE);
				}
				transformUnifiedCacheState(tcfg[i], &change_flag, PERSISTENCE);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	printf("Unified cache updated %d times\n", unified);	  
	dumpCache(); 
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * unified cache */
	 categorize_unified_ps_cache_access();	  

	 /* clean up the cache before another analysis starts */
	 cleanupCache();	  
}

/* must analysis of unified l2 cache */
void analyze_unified_cache_must()
{
	int change_flag = 1;
	tcfg_edge_t* edge;
	int i;
	int temp = 0;

	tcfg_node_t* bbi = tcfg[0];

	/* Initialize the abstract cache at the entry of the 
	 * program */	  
   for(i = 0; i < num_tcfg_nodes; i++)
		initializeCache(tcfg[i]);
	
	while(change_flag)
	{
		  change_flag = 0;

		  for(i = 0; i < num_tcfg_nodes; i++)
		  {
				/* Join cache states from predecessor basic 
				 * blocks */ 	
				for(edge = tcfg[i]->in; edge; edge = edge->next_in)
				{
					 JoinCacheState(edge->src, tcfg[i], MUST);
				}
				transformUnifiedCacheState(tcfg[i], &change_flag, MUST);
		  }
	}

	/* Dump the cache state at end of each basic block */	
#ifdef _DEBUG
	printf("Unified cache updated %d times\n", unified);	  
	dumpCache();	  
#endif	
	
	/* Now categorize the all hit/non-classified access for
	 * unified cache */
	 categorize_unified_cache_access();	  

	 /* free up the memory taken during cache analysis */
	 cleanupCache();
}

void analyze_unified_cache()
{
	/* set parameters for the unified cache */	  
	X = assoc_l2; Y = nsets_l2; B = bsize_l2; l1 = cache_dl2_lat; l2 = mem_lat[0];  
	analyze_unified_cache_must();
	analyze_unified_cache_ps();
}
