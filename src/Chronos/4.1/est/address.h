#ifndef __ADDRESS_H_
#define __ADDRESS_H_

typedef unsigned addr_t;

#define MAX_NO_REGISTERS 34
#define HI 32
#define LO 33
#define STACK_START 0x7fffc000
#define GLOBAL_START 0x10000000
#define RETURN_ADDRESS 0

#define SIZE_OF_BYTE 1
#define SIZE_OF_WORD 4
#define SIZE_OF_HALF_WORD (SIZE_OF_WORD)/2
#define SIZE_OF_BLOCK B
#define INFINITY 0x7fffc000
#define WIDENING_POINT 5
#define GET_MEM(x) ((x) / SIZE_OF_BLOCK)
#define REG_START 	16
#define REG_END 		23
#define REG_STACK		29
#define REG_GLOBAL	28

/* cfg node list */
struct slist
{
	void* dst;
	struct slist* next;
};	

typedef struct slist* slist_p;
typedef struct slist slist_s;

/* A strided interval domain */
struct ric {
	addr_t lower_bound;	  
	addr_t upper_bound;
	addr_t stride;
};
typedef struct ric ric_s;
typedef struct ric* ric_p;
/* Abstract memory locations */
struct abs_mem {
	int valid;	  
	int inst_addr;
	ric_p addr;
	ric_p value;
	struct abs_mem* next;
};
typedef struct abs_mem abs_mem_s;
typedef struct abs_mem* abs_mem_p;

void analyze_address();
int gcd(int a, int b);
#endif
