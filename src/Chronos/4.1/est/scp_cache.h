#ifndef __MPA_CACHE_H_
#define __MPA_CACHE_H_
#include "scp_address.h"

/*** SCOPED CACHE ANALYSIS ***/

#define EVICTED     (X+1)
#define MAX_A       4       /*MAX_CACHE_ASSOCIATIVITY*/
struct scope_block { 
    saddr_p     m;          /*scope address of this scoped block*/
    int         age;        /*relative age of this scoped block*/
    saddr_p     ys[MAX_A];  /*younger set*/
    int         flag;
};
typedef struct scope_block  sblk_s;
typedef struct scope_block* sblk_p;

sblk_p  createSBlk  (saddr_p blkAddr);
void    cpySBlk     (sblk_p dst, sblk_p src);
int     mergeSBlk   (sblk_p dst, sblk_p src);
int     cmpSBlk     (sblk_p sblk1, sblk_p sblk2);

//add scoped address mAcc to younger set of scoped cache block acsBlk
int     addToYS     (sblk_p acsBlk, saddr_p mAcc);
int     clearYS     (sblk_p acsBlk);
int     unionYS     (sblk_p dstBlk, sblk_p srcBlk);

/*ACS structure: scp_acs[i]=cacheSet[i], addr in increasing order*/
#define scp_acs worklist_p* 

void addToIncSet(saddr_p m,worklist_p *prvNode,worklist_p *strNode, loop_t *lp);


/****** SCOPED PERSISTENCE ANALYSIS FUNCTION *******/
#define WRITE_THRU  1
#define AVOID_ONE_ACCESS_MULTIPLE_AGING 1
/*fdct not converge if AVOID_MULTIPLE_AGING*/
#define AGED        0x1
/* scope-aware update PS, update acs after accessing addr_set in loop lp*/
int  canRenew(dat_inst_t* d_inst, int lpId);
void PS_update(scp_acs acs_out, worklist_p addr_set, loop_t *lp);

/* scope-aware join PS, to join two ACS */
int  PS_join(scp_acs src, scp_acs dst, loop_t *lp);

/* cache analysis within a basic block */
void transform_bbi_dcache(tcfg_node_t *bbi, loop_t* lp,int type);


/****** estimate cache miss of PS blks in loop L ******/

/* collect scoped blks persistent when entering loop lp */
void        getOuterPS(loop_t* lp);

/* estimate cold miss of persistent blks each time entering lp*/
void        estLpMissPS(loop_t* lp);

/***** estimate all miss of non-persistent blks in node bbi ******/
void        estNodeAllMiss(tcfg_node_t* bbi);

/***** multi-level cache analysis framework *****/

/* PS analysis within loop lp */
void        analyze_loop_ps(loop_t *lp);

/*general handler for mpa analysis framework*/
void        mpa_datacache();

#endif
