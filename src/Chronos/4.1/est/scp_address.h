#ifndef REG_ADDRESS_H
#define REG_ADDRESS_H
#include "cache.h"
#include "stdlib.h"
#include "infeasible.h"
#include "tcfg.h"
#include "loops.h"
#include "isa.h"
#include "common.h"
#include "symexec.h"

/*** TEMPORAL SCOPE DATA STRUCTURE ***/
struct temporal_scope {
    int loop_id;
    int lw;
    int up;
    int flag;
};
typedef struct temporal_scope   ts_s;
typedef struct temporal_scope*  ts_p;

#define EQUAL_TS    0
#define OLAP_TS     1
#define SEP_TS      -1
int     cmpTS(ts_p ts1, ts_p ts2);
int     cmpTSset(worklist_p tsSet1, worklist_p tsSet2, int lpId);
void    cpyTS(ts_p src, ts_p dst);
void    printTS(FILE *fp, ts_p as);
void    printTSset(FILE *fp, worklist_p tsSet);
int     eqTSset(worklist_p tsSet1,worklist_p tsSet2, int lpId);
int     mergeTSset(worklist_p dstSet, worklist_p srcSet, int lpId);

/*estimate execution counts of lp given temporal scope*/
int     estScopeSize(worklist_p tsSet, int lpId);

/*** SCOPED MEMORY BLOCK DATA STRUCTURE ***/
#define SELF_CONFLICT   1
#define RENEWABLE       1
#define UNKNOWN_ADDR    -1
struct scoped_address {
    int                 blkAddr;    /*Memory block*/
    int                 instAddr;   /*Inst addr*/
    worklist_p          tsList;     /*Defined temporal scope of this memscp*/ 
    //worklist_p confSet;           /*Memscp that conflict with this memscp*/
    int                     flag;   /*0: cannot renew, 1: can renew*/ 
};

typedef struct scoped_address  saddr_s;
typedef struct scoped_address* saddr_p;
void    printSAddr(FILE *fp, saddr_p memblk, int full);   //print one scoped mem
void    printSAddrSet(FILE *fp, worklist_p sAddrSet,int full);//print set scpmem
void    cpySAddr(saddr_p dst, saddr_p src);
saddr_p  createSAddr(int addrBlk, int instAddr, int flag, worklist_p orgTSset);
worklist_p  findBlkAddr(int addrBlk, worklist_p SAddrSet);
void        rearrangeTS(saddr_p smem);
int         cmpSAddr(saddr_p smem1, saddr_p smem2, int lpId);

/*** SCOPE-AWARE ADDRESS ANALYSIS ***/
FILE *dbgAddr;
struct size_cons {
    char name[256];
    int  size;
    struct size_cons *next;
};
typedef struct size_cons size_cons_s;
typedef struct size_cons* size_cons_p;

/*** regular address analysis & scope detection ***/
void enum_regular_address(dat_inst_t* d_inst, expr_p expr, int flag,
                int curEq, worklist_p curTSnode, int curAddr);

/*** Detect possible AddrSet of regular access and compute access scope ***/
int analyze_regular_access(dat_inst_t *d_inst, inf_node_t* ib);

/*** Detect possible AddrSet of unpred. access and compute access scope ***/
int analyze_unpred_access(dat_inst_t *d_inst, inf_node_t* ib);

/*** Detect possible AddrSet of scalar access and compute access scope ***/
int analyze_scalar_access(dat_inst_t *d_inst, inf_node_t* ib);

/*** generate AddrSet & access scope of data reference D ***/
void genAccessAddress(dat_inst_t *d_inst, inf_node_t* ib);

/*** general calling function ***/
void classified_address_analysis();
#endif

