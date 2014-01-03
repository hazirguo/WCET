/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Infeasible path analysis for Chronos
 * Vivy Suhendra
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
 * 03/2007 symexec.c
 *
 * Version update
 * v1.1: 
 *   _ perform sym.exec to identify constant value in register first
 *     then detect induction val., to handle: add $1 $1 $2, where $2 = const
 *   _ perform simple memory modeling with lw,sw; to deal with register spill
 *   _ parse disassembly file using C-string rather than shell call -> faster
 * v1.0:
 *   _ add induction value detection of register value
 *   _ add merge register value from predecessors to support inter-block
 *     deritree construction (still intra-procedures)
 *   _ add other things necessary to perform register expansion for RTSS2010
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"
#include "common.h"
#include "loops.h"
#include "infeasible.h"

#define MIN_VAL (0-0xffff)

/*** MEMORY MODELING FRAMEWORK ***/
void    printMemList(FILE* fp, worklist_p);
mem_p   createMem(addr_t instAddr, reg_t writeAddr, reg_t regValue);
int     copyMem(mem_p dst, mem_p src);
void    clrMem(worklist_p memList);
void    loadMem(worklist_p memList, reg_t writeAddr, reg_t *regValue);
int     storeMem(worklist_p *memList, addr_t instAddr, 
                    reg_t writeAddr, reg_t regValue);
int     copyMemList(worklist_p *dstL, worklist_p srcL);
int     memListEq(worklist_p list1, worklist_p list2);
int     mergeMemList(worklist_p *dstList, worklist_p srcList);

/*** INDUCTION VALUE DETECTION ***/
biv_p   createBIV(reg_t *regList, insn_t *insn);
static int  regRedefined(char *regName, insn_t *insn);
static int  checkBivStride(reg_t *regList, insn_t *insn);
static void detectBIV();

/*** SYMBOLIC EXECUTION FRAMEWORK ***/
int     execute();

static int  doSymExec();
static int  joinBrach(inf_proc_t *iproc, inf_node_t *ib, int lpHeaderId);
static int  regDerive(inf_node_t *ib,insn_t *insn,int ignore,char *jal,int dbg);
static void analyzeBlock(inf_node_t *ib);
static void readSymExecResult(inf_node_t *ib);
static void initAll();
static void freeAll();

int     cntExec;
int     conflictDetectFlag; /*Global: if do conflicting pair detection*/
int     traceInductionFlag; /*Global: if trace induction value during sym.exec*/
int     traceMemFlag;   /*Global: if perform memory modeling during sym.exec*/ 

FILE* dbgExec; /*to debug the convergence*/

extern prog_t prog;
extern loop_t **loops;
extern int num_tcfg_loops;

/*** utility functions ***/
int getAddrD(dat_inst_t *d_inst) {
    insn_t      *insn;
    insn = (insn_t*)(d_inst->insn);
    return hexValue(insn->addr);
}
void printDataRef(FILE *fp,dat_inst_t *d_inst) {
    int i;
    insn_t      *insn;
    //de_inst_t   *inst;
    char        *deritree;

    insn = (insn_t*)(d_inst->insn);
    //inst = d_inst->inst;
    //deritree = d_inst->deritree;
    if (fp) {
        fprintf(fp, "\n\nDataRef: 0x%s %s %s %s %s\n", 
                    insn->addr, insn->op, insn->r1, insn->r2, insn->r3 );
        printExpr(fp,&(d_inst->addrExpr));
    }
}
int isMemAccess(char* isa_name)
{
	if(! strcmp(isa_name, "sw") || !strcmp(isa_name, "sh") 
	   || !strcmp(isa_name, "sb") || !strcmp(isa_name, "swl")
		|| !strcmp(isa_name, "swr") || !strcmp(isa_name, "s.s")
		|| !strcmp(isa_name, "s.d"))
	
		return 1;  
	
	else if(! strcmp(isa_name, "lw") || !strcmp(isa_name, "lh") 
		|| !strcmp(isa_name, "lb") || !strcmp(isa_name, "lwl")
		|| !strcmp(isa_name, "lwr") || !strcmp(isa_name, "lhu") 
		|| !strcmp(isa_name, "lbu") || !strcmp(isa_name, "lhun") 
		|| !strcmp(isa_name, "l.s") || !strcmp(isa_name, "l.d"))
		
		return 1;

	return 0;	
}
int isLoadInst(char *isa_name) {
    if(! strcmp(isa_name, "lw") || !strcmp(isa_name, "lh") 
            || !strcmp(isa_name, "lb") || !strcmp(isa_name, "lwl")
            || !strcmp(isa_name, "lwr") || !strcmp(isa_name, "lhu") 
            || !strcmp(isa_name, "lbu") 
            || !strcmp(isa_name, "l.s") || !strcmp(isa_name, "l.d")) {
        return 1;
    }
    else return 0;
}
int isStoreInst(char *isa_name) {
	if(! strcmp(isa_name, "sw") || !strcmp(isa_name, "sh") 
	   || !strcmp(isa_name, "sb") || !strcmp(isa_name, "swl")
		|| !strcmp(isa_name, "swr") || !strcmp(isa_name, "s.s")
		|| !strcmp(isa_name, "s.d")) {
		return 1;  
    }
    else return 0;
}
loop_t* getIbLoop(inf_node_t *ib) {
    int dbg = 0;
    loop_t *loop;
    if (ib->loop_id==-1) return loops[0];
    loop = loops[inf_loops[ib->loop_id].loop_id]; 
    return loop;
}
void copyInst(insn_t *dsrc, insn_t *ddes) {
    strcpy(ddes->addr,dsrc->addr);
    strcpy(ddes->op,dsrc->op);
    strcpy(ddes->r1,dsrc->r1);
    strcpy(ddes->r2,dsrc->r2);
    strcpy(ddes->r3,dsrc->r3);
}

static int checkBivStride(reg_t *regList, insn_t* insn) {
    char str[128];
    int  k;
    int  regPos1,regPos2,regPos3;
    int  flag;
    regPos1 = findReg(regList, insn->r1);
    regPos2 = findReg(regList, insn->r2);
    
    k = INFINITY;
    if (regPos2==28 || regPos2==29) return INFINITY;/*ignore special reg*/

    if (strcmp(regList[regPos1].name,regList[regPos2].name)!=0) return INFINITY;
    if (streq(insn->op,"addu")) {
        /* r1 = r1 + r3 */
        regPos3 = findReg(regList,insn->r3);
        if (regList[regPos3].t!=VALUE_CONST) return INFINITY;/*r3 not constant*/ 
        else {
            k = regList[regPos3].val;
            return k;
        }
    }
    else if (streq(insn->op,"addi") || streq(insn->op,"addiu")) {
        /* r1 = r2 + k */
        k = atoi(insn->r3);
    }
    return k;
}

biv_p createBIV(reg_t *regList, insn_t *insn) {
    biv_p biVar;
    biVar = calloc(1,sizeof(biv_s));
    //sprintf(biVar->regName,"%s",insn->r1);
    strcpy(biVar->opr,insn->op);
    biVar->insn = insn;
    biVar->stride = checkBivStride(regList, insn);
    initReg( &(biVar->initVal));
    biVar->flag = 0;
    strcpy(biVar->regName,insn->r1);
    return biVar;
}

int saved2Mem(inf_node_t *node, biv_p biv) {
    int dbg = 0;
    int k;
    insn_t *insn;
    char *bivAddr;
    //check if biv saved to mem before it is redefined
    //return 1 if (i) biv is saved, or (ii) biv is redefined. return 0->unknown
    //because saved biv detection is unaffected by loop scope,
    //it will appear in outermost loop where it belongs (not the loop using it)
    bivAddr = ((insn_t*)(biv->insn))->addr;
    if (dbg) {
    }
    for (k=0; k<node->num_insn; k++) {
        insn = &(node->insnlist[k]);
        if (isStoreInst(insn->op) && strcmp(biv->regName,insn->r1)==0
                && hexValue(insn->addr)>hexValue(bivAddr)) {
            biv->flag |= BIV_SAVED;
            if (dbg) {
                fprintf(dbgExec,"\nSaved: insn %s |%s(%s,0,%d)",
                        insn->addr,((insn_t*)(biv->insn))->addr, 
                        biv->regName,biv->stride);
            }
            return 1;
        }
        //(1):biv is redefined by this insn
        else if (strcmp(bivAddr,insn->addr)!=0
                && (biv->flag & BIV_SAVED)==0
                && hexValue(insn->addr)>hexValue(bivAddr)
                && regRedefined(biv->regName, insn)) {
            if (dbg) {
                fprintf(dbgExec,"\nRedefined: insn %s |%s(%s,0,%d)",
                        insn->addr,bivAddr,biv->regName,biv->stride);
                fflush(dbgExec);
            }
            return 1;
            #if 0
            if (prvNode) prvNode->next = bivNode->next;
            else biv_list = bivNode->next;
            freeBIV(&biv); free(bivNode);
            if (prvNode) bivNode = prvNode->next;
            else bivNode = biv_list;
            #endif
        }
    }
    return 0;
}
/*** Loop induction variable detection ***/
static void detectBIV() {
    int     dbg = 0; /*dbg flag*/
    int     i,j,k,h;
    char    *bivAddr;
    
    biv_p   biv;
    int     str;
    worklist_p biv_list, bivNode, prvNode;
    insn_t  *insn;

    inf_node_t *cur;
    loop_t  *curLp;
    proc_t  *p;
    int     lpHeadId, lpTailId, pId, lpId;

    reg_t   *regList;

    for (i=num_tcfg_loops-1; i>0; i--) {
        curLp = loops[i];
        if (curLp==NULL) continue;

        biv_list = NULL;
        p = curLp->head->bb->proc;
        if (curLp->head) lpHeadId = curLp->head->bb->id;
        else lpHeadId = 0;
        if (curLp->tail) lpTailId = curLp->tail->bb->id;
        else lpTailId = p->num_bb-1;
        if (dbg) {
            fprintf(dbgExec,"\nLoop %d head (%d:%d) tail (%d:%d)",
                curLp->id, p->id, lpHeadId, p->id, lpTailId);
            fflush(dbgExec);
        }

        //detect possible biv within the loop
        for (j=lpHeadId; j<=lpTailId; j++) {
            /*scan through inst, detect basic induction var*/
            cur = &(inf_procs[p->id].inf_cfg[j]);
            regList = (reg_t*)(cur->regListIn);
            for (k=0; k<cur->num_insn; k++) {
                insn = &(cur->insnlist[k]);
                if (insn->flag==BIV_INST) continue;//induction inst of outer lp
                str = checkBivStride(regList,insn);
                if (str != INFINITY) {
                    if (dbg) {
                        fprintf(dbgExec,"\nBIV: insn: %s (%s,_,%d)",
                            insn->addr,insn->r1,str);fflush(dbgExec);
                    }
                    //insn->flag = BIV_INST;
                    biv = createBIV(regList,insn);
                    //check whether a biv is saved before it is redefined
                    for (h=j; h<= lpTailId; h++) {
                        if (saved2Mem(inf_procs[p->id].inf_cfg+h,biv)) break;
                    }
                    if (0) {
                        fprintf(dbgExec,"\nSaved BIV: insn: %s - ",insn->addr);
                        printBIV(dbgExec,biv);
                        fflush(dbgExec);
                    }
                    //biv->lp = curLp;
                    addToWorkList(&biv_list, biv);
                }
            }//end insn[k]
        }//end bb[j]

        for (j=lpHeadId; j<=lpTailId; j++) {
            cur = &(inf_procs[p->id].inf_cfg[j]);
            regList = (reg_t*)(cur->regListIn);
            for (k=0; k<cur->num_insn; k++) {
                insn = &(cur->insnlist[k]);
                bivNode = biv_list; prvNode = NULL;
                while (bivNode) {
                    biv = bivNode->data;
                    bivAddr = ((insn_t*)(biv->insn))->addr;
                    if (biv==NULL) {bivNode=bivNode->next; continue;}
                    //check whether a possible biv is not a real biv because it is redefined (before it is saved to memory)
                    if (strcmp(bivAddr,insn->addr)!=0
                            && (biv->flag & BIV_SAVED)==0
                            && regRedefined(biv->regName, insn)) {
                        if (dbg) {
                            fprintf(dbgExec,"\nRedefined: insn %s |%s(%s,0,%d)",
                                    insn->addr,bivAddr,biv->regName,biv->stride);
                            fflush(dbgExec);
                        }
                        if (prvNode) prvNode->next = bivNode->next;
                        else biv_list = bivNode->next;
                        freeBIV(&biv); free(bivNode);
                        if (prvNode) bivNode = prvNode->next;
                        else bivNode = biv_list;
                    }
                    else {//not redefined by this insn
                        prvNode = bivNode;
                        bivNode = bivNode->next;
                    }
                }//end bivNode
            }//end insn[k]
        }//end bb[j]

        #if 0
        //check whether a possible biv is not a real biv because it is redefined
        for (j=lpHeadId; j<=lpTailId; j++) {
            cur = &(inf_procs[p->id].inf_cfg[j]);
            regList = (reg_t*)(cur->regListIn);
            for (k=0; k<cur->num_insn; k++) {
                insn = &(cur->insnlist[k]);
                bivNode = biv_list; prvNode = NULL;
                while (bivNode) {
                    biv = bivNode->data;
                    bivAddr = ((insn_t*)(biv->insn))->addr;
                    if (biv==NULL) {bivNode=bivNode->next; continue;}
                    //(1):redefined by this insn
                    if (strcmp(bivAddr,insn->addr)!=0
                            && (biv->flag & BIV_SAVED)==0
                            && regRedefined(biv->regName, insn)) {
                        if (dbg) {
                            fprintf(dbgExec,"\nRedefined: insn %s |%s(%s,0,%d)",
                                    insn->addr,bivAddr,biv->regName,biv->stride);
                            fflush(dbgExec);
                        }
                        if (prvNode) prvNode->next = bivNode->next;
                        else biv_list = bivNode->next;
                        freeBIV(&biv); free(bivNode);
                        if (prvNode) bivNode = prvNode->next;
                        else bivNode = biv_list;
                    }
                    else {//not redefined by this insn
                        prvNode = bivNode;
                        bivNode = bivNode->next;
                    }
                }//end bivNode
            }//end insn[k]
        }//end bb[j]
        #endif

        //record real biv
        curLp->biv_list = biv_list;
        for (bivNode = biv_list; bivNode; bivNode=bivNode->next) {
            biv = bivNode->data;
            if (biv) ((insn_t*)(biv->insn))->flag |= BIV_INST;
        }
        if (dbg) {
            fprintf(dbgExec,"\n BIV list of L%d ",curLp->id);
            for (bivNode = biv_list; bivNode; bivNode = bivNode->next) {
                biv = (biv_p) bivNode->data;
                bivAddr = ((insn_t*)(biv->insn))->addr;
                lpId = curLp->id;
                //lpId = ((loop_t*)(biv->lp))->id;
                fprintf(dbgExec,"\n\t %s %s L%d",bivAddr,biv->regName,lpId);
                printBIV(dbgExec,biv);
            }
        }
    }//end loop[i]
}

/*** Functions for conflicting pair detection ***/
#if 0 /*HBK: this should belong to conflict.c or infeasible.c, not support now*/
int addAssign( char deritree[], cfg_node_t *bb, int lineno, int rhs, char rhs_var ) {
    int dbg = 0;
    int i;
    assign_t *assg;
    inf_node_t *ib;

    if (conflictDetectFlag==0) return;/*not do conflicting pair detection yet*/
    ib = &(inf_procs[bb->proc->id].inf_cfg[bb->id]);

    // check if there is previous assignment to the same memory address in the same block
    for( i = 0; i < ib->num_assign; i++ ) {
        assg = ib->assignlist[i];

        if( streq( assg->deritree, deritree )) {
            // overwrite this assignment
            assg->lineno  = lineno;
            assg->rhs     = rhs;
            assg->rhs_var = rhs_var;

            if( dbg & 0 ) { printf( "OVR " ); printAssign( assg, 0 ); }
            return 1;
        }
    }

    // else add new
    assg = (assign_t*) malloc( sizeof(assign_t) );
    strcpy( assg->deritree, deritree );
    assg->rhs           = rhs;
    assg->rhs_var       = rhs_var;
    assg->bb            = bb;
    assg->lineno        = lineno;

    ib->num_assign++;

    i = ib->num_assign;
    ib->assignlist = (assign_t**) realloc( ib->assignlist, i * sizeof(assign_t*) );
    ib->assignlist[i-1] = assg;

    if( dbg & 0 ) { printf( "NEW " ); printAssign( assg, 0 ); }
    return 0;
}

int addBranch( char deritree[], cfg_node_t *bb, int rhs, char rhs_var, char jump_cond ) {
    int dbg = 0;
    int i;
    branch_t *br;

    int numbb = prog.procs[bb->proc->id].num_bb;

    if (conflictDetectFlag==0) return;/*not do conflicting pair detection yet*/
    br = (branch_t*) malloc( sizeof(branch_t) );
    strcpy( br->deritree, deritree );
    br->rhs              = rhs;
    br->rhs_var          = rhs_var;
    br->jump_cond        = jump_cond;
    br->bb               = bb;

    br->num_BA_conflicts = 0;
    br->BA_conflict_list = NULL;
    br->num_BB_conflicts = 0;
    br->BB_conflict_list = NULL;

    inf_procs[bb->proc->id].inf_cfg[bb->id].branch = br;

    if( dbg & 0 ) { printBranch( br, 0 ); }
    return 0;
}
#endif

/*** MEMORY MODELING FRAMEWORK ***/

mem_p createMem(addr_t instAddr, reg_t writeAddr, reg_t regValue) {
    mem_p memBlk;
    memBlk = calloc(1,sizeof(mem_s));
    memBlk->instAddr = instAddr;
    initReg( &(memBlk->writeAddr) );
    initReg( &(memBlk->regValue) );
    cpyReg( &(memBlk->writeAddr),writeAddr);
    cpyReg( &(memBlk->regValue),regValue);
    return memBlk;
}
void freeMemBlk(mem_p *memBlk) {
    clrReg( &( (*memBlk)->writeAddr) ); 
    clrReg( &( (*memBlk)->regValue) ); 
    *memBlk=NULL;
}
void freeMemList(worklist_p *memList) {
    mem_p memBlk;
    worklist_p curNode, nextNode;
    curNode = *memList;
    while (curNode) {
        nextNode = curNode->next;
        memBlk = curNode->data;
        freeMemBlk( &memBlk );
        curNode->data = NULL;
        curNode->next = NULL;
        free(curNode);
        curNode = nextNode;
    }
    *memList = NULL;
}
int copyMem(mem_p dst, mem_p src) {
    worklist_p memNode;
    mem_p memBlk;
    int changed = 0;
    if (dst->instAddr != src->instAddr) {
        changed = 1;
        dst->instAddr = src->instAddr;
    }

    changed |= cpyReg( &(dst->writeAddr), src->writeAddr );
    changed |= cpyReg( &(dst->regValue), src->regValue );
    return changed;
}
void printMemList(FILE *fp,worklist_p memList) {
    worklist_p memNode;
    mem_p memBlk;
    fprintf(fp,"[ ");
    for (memNode = memList; memNode; memNode=memNode->next) {
        memBlk = memNode->data;
        if (memBlk->regValue.t==VALUE_UNPRED)continue;//dont print unpredictable
        fprintf(fp," (%x ",memBlk->instAddr);
        printReg(fp,memBlk->writeAddr);
        fprintf(fp," ");
        printReg(fp,memBlk->regValue); fprintf(fp,")");
    }
    fprintf(fp," ]");
    fflush(fp);
}
/* dst = src, old dst data will be removed */
int copyMemList(worklist_p *dstList, worklist_p srcList) {
    int dbg = 0;
    worklist_p srcNode, dstNode, prvNode, memNode;
    mem_p srcBlk, dstBlk, memBlk;
    int changed,flag;
    srcNode = srcList; dstNode= *dstList; prvNode = NULL;
    changed = 0;
    if (dbg) {
        fprintf(dbgExec,"\n CopyMem: SRC ");
        printMemList(dbgExec,srcList);
    }
    while (srcNode) {
        if (dstNode) {
            flag = copyMem(dstNode->data, srcNode->data);
            if (flag) changed = 1;
            prvNode = dstNode;
            dstNode = dstNode->next;
        }
        else {
            dstBlk = calloc(1,sizeof(mem_s));
            copyMem(dstBlk, srcNode->data);
            addAfterNode(dstBlk, &prvNode, dstList);
            changed = 1;
        }
        srcNode = srcNode->next;
    }
    if (prvNode) {//remove remaining nodes of dst after prvNode
        prvNode->next = NULL;
        freeMemList( &dstNode);
        changed = 1;
    }
    else if (*dstList!=NULL) {//prvNode==NULL -> dstList=NULL
        freeMemList(dstList);
        changed = 1;
    }//dstList==NULL -> do nothing

    if (dbg) {
        fprintf(dbgExec," ->DST ");
        printMemList(dbgExec,*dstList);
    }
    return changed;
}
/*save deriValue to writeAddr to memList, for st instruction at addr instAddr*/
int storeMem (worklist_p *memList, addr_t instAddr,
                reg_t writeAddr, reg_t regValue) {
    int dbg=0;
    mem_p memBlk;
    worklist_p memNode, prvNode, addLoc;
    int updated, predictable, found;
    char str[DERI_LEN],des[DERI_LEN];
    FILE *dbgExec = stdout;

    //ERR: not handle unpredictable, assume it won't interfere with pred.mem 
    if (writeAddr.t==VALUE_UNPRED || writeAddr.t==VALUE_UNDEF) return 0;

    if (regValue.t==VALUE_UNDEF || regValue.t==VALUE_UNPRED) predictable = 0;
    else predictable = 1;//type=CONST / EXPR / INDUCT
    
    if (dbg && predictable) {
        fprintf(dbgExec,"\nStore: 0x%x ",instAddr);printReg(dbgExec, writeAddr);
        fprintf(dbgExec," ");printReg(dbgExec, regValue);
    }
    if (dbg && predictable) {
        fprintf(dbgExec,"\nBefore ");printMemList(dbgExec, *memList);
    }

    updated = 0;
    prvNode = NULL; addLoc = NULL;
    for (memNode = *memList; memNode; memNode=memNode->next) {
        memBlk = memNode->data;
        prvNode = memNode;
        if (regEq(memBlk->writeAddr, writeAddr)==1) break;
    }
    if (memNode) {//found memBlk->writeAddr == writeAddr
        if (!regEq(memBlk->regValue, regValue)) {
            if (memBlk->instAddr==instAddr) {
                //same inst, same writeAddr, diff. regValue -> merge value
                //mergeReg( &(memBlk->regValue), regValue, 0);
                cpyReg( &(memBlk->regValue), regValue);
                //regUnknown( &(memBlk->regValue) );
            }
            else {//set new regValue to memBlk->writeAddr
                memBlk->instAddr = instAddr;
                if (predictable) cpyReg( &(memBlk->regValue), regValue);
                else regUnknown( &(memBlk->regValue) );
            }
        }//else memBlk->regValue == regValue: do nothing
    }
    else {//cannot find writeAddr in memList
        if (predictable) {
            if (dbg) {
                fprintf(dbgExec,"\nAdd new mem entry");
                fprintf(dbgExec,"\nStore: 0x%x ",instAddr);
                printReg(dbgExec, writeAddr);
            }
            memBlk=createMem(instAddr,writeAddr,regValue);
            addAfterNode(memBlk,&prvNode,memList);
        }
    }
    if (dbg && predictable) {
        fprintf(dbgExec,"\nAfter: ");printMemList(dbgExec, *memList);}
    return updated;
}
/*load deriValue from writeAddr in memList, for ld instruction*/
void loadMem (worklist_p memList, reg_t writeAddr, reg_t *regValue) {
    int dbg=0;
    FILE *dbgExec = stdout;
    mem_p memBlk;
    worklist_p memNode;

    if (dbg) printMemList(dbgExec, memList);
    regUnknown(regValue);
    for (memNode=memList; memNode; memNode=memNode->next) {
        memBlk = memNode->data;
        if (regEq(writeAddr,memBlk->writeAddr)) {
            cpyReg(regValue, memBlk->regValue);
            break;
        }
    }
    if (dbg && regValue->t!=VALUE_UNPRED) {
        fprintf(dbgExec,"\nLoad from i:%x",memBlk->instAddr);
        fprintf(dbgExec," a:");printReg(dbgExec,memBlk->writeAddr);
        fprintf(dbgExec," v:");printReg(dbgExec,*regValue);
    }
}
int memListEq(worklist_p dstList, worklist_p srcList) {
    int dbg = 0;
    worklist_p srcNode, dstNode;
    mem_p srcBlk, dstBlk;

    //not really correct, need to ensure certain order for the list
    //hopefully it can work, for now
    srcNode = srcList; dstNode = dstList;
    while (srcNode && dstNode) {
        srcBlk = srcNode->data; dstBlk=dstNode->data; 
        if (!regEq(srcBlk->writeAddr,dstBlk->writeAddr)
            || !regEq(srcBlk->regValue,dstBlk->regValue))
            return 0;
        srcNode = srcNode->next;
        dstNode = dstNode->next;
    }
    if (srcNode || dstNode) return 0;
    return 1;
}
int mergeMemList(worklist_p *dstList, worklist_p srcList) {
    int dbg = 0;
    worklist_p  memNode, srcNode, dstNode, srcPrv, dstTail, newList;
    mem_p   memBlk, srcBlk, dstBlk;
    int dstChanged;
    int cmpValue,flag;
    
    if (dbg) {
        fprintf(dbgExec,"\nMerge mem.list:");
        fprintf(dbgExec,"\nSrc: "); printMemList(dbgExec,srcList);
        fprintf(dbgExec,"\nDst: "); printMemList(dbgExec,*dstList);
    }
    dstChanged = 0;
    if (dbg && 0) fprintf(dbgExec,"\nMerging: ");
    dstTail = *dstList;
    while (dstTail && dstTail->next) dstTail = dstTail->next;
    newList = NULL;

    //a very simple but slow implementation of mem.search
    srcNode = srcList;
    while (srcNode) {
        srcBlk = srcNode->data;
        dstNode = *dstList;
        while (dstNode!=NULL) {
            dstBlk = dstNode->data;
            if (regEq(srcBlk->writeAddr,dstBlk->writeAddr)) {
                flag = mergeReg( &(dstBlk->regValue), srcBlk->regValue, 0);
                if (flag) dstChanged = 1;
                break;
            }
            dstNode = dstNode->next;
        }
        if (dstNode==NULL) {
            memBlk = createMem(srcBlk->instAddr,
                            srcBlk->writeAddr,srcBlk->regValue);
            addToWorkList(&newList,memBlk);
        }
        srcNode = srcNode->next;
    }
    if (dstTail) dstTail->next = newList;
    else *dstList = newList;

    if (dbg) {fprintf(dbgExec,"\nMERGED c:%d ",dstChanged);
                printMemList(dbgExec,*dstList);}
    return dstChanged;
}

// Note: for commutative operations on mixed-type operands
// (one is a variable and the other is a constant value),
// the derivation string is stored in the format "<var>+<const>"
#define PROCESS_STORE \
        if (traceMemFlag) {\
            /*HBK: sw -> save to mem model*/ \
            setInt(&tmpReg, atoi(insn->r2));\
            regOpr("+",&writeAddr,regList[r3],tmpReg);\
            storeMem(memList,instAddr,writeAddr,regList[r1]);\
            if (dbg) {\
                fprintf(dbgF,"\nStore (%x ",instAddr); \
                fprintf(dbgF," ");printReg(dbgF,writeAddr);\
                fprintf(dbgF," -> ");printReg(dbgF,regList[r1]);\
            }\
        }

#define PROCESS_LOAD \
        regUnknown(regList+r1);\
        if (traceMemFlag) {\
            /*HBK: ld -> load from mem model*/ \
            setInt(&tmpReg, atoi(insn->r2));\
            regOpr("+",&writeAddr,regList[r3],tmpReg);\
            loadMem(*memList,writeAddr,regList+r1);\
            if (dbg) {\
                fprintf(dbgF,"\nLoad (%x ",instAddr); \
                fprintf(dbgF," ");printReg(dbgF,writeAddr);\
                fprintf(dbgF," -> ");printReg(dbgF,regList[r1]);\
            }\
        }

#define PROCESS_SLTI \
        /* set r1 to 1 if r2 < atoi(r3) */ \
        if (regList[r2].t == VALUE_CONST) {\
            if (regList[r2].val<atoi(insn->r3)) \
                setInt(regList+r1,1);\
            else setInt(regList+r1,0);\
        }\
        else regUnknown(regList+r1);

#define PROCESS_SLT \
        /* set r1 to 1 if r2 < r3 */ \
        if (regList[r2].t == VALUE_CONST && regList[r2].t==regList[r3].t) {\
            if ( regList[r2].val < regList[r3].val ) \
                setInt(regList[r1],1);\
            else setInt(regList[r1],0);\
        }\
        else regUnknown(regList[r1]);

#define ADD_ASSIGN 
        /*add assignments for AB/BB conflict detection, ignore for now*/


int ignoreRegUpdate(char *r) {
    //$30 can be used as normal register when optimization flag is on
    if (streq(r,"$31")// || streq(r,"$30")|| streq(r,"$29")|| streq(r,"$28")
            || streq(r,"$0"))
        return 1;
    else return 0;
}
static int regDerive(inf_node_t *ib, insn_t *insn, 
                int ignore, char *jal, int dbg) {
    FILE        *dbgF = stdout;
    int         r1, r2, r3;
    addr_t      instAddr;
    insn_t      *insnTmp;
    biv_p       biv, bivInit; 
    char        *op;

    worklist_p  *memList;
    reg_t       *regList;
    reg_t       writeAddr, writeValue, tmpReg;  //for load/store

    initReg(&writeAddr);
    initReg(&writeValue);
    initReg(&tmpReg);

    ignore = 1; //no AB - BB detection supported, for now
    memList = &(ib->memListOut);

    op = insn->op;
    sscanf(insn->addr,"%x",&instAddr);
    regList = (reg_t*)(ib->regListOut);
    //if (dbg) printRegList(dbgF, (reg_t*)(ib->regListOut) );
    r1 = findReg(regList, insn->r1);
    r2 = findReg(regList, insn->r2);
    r3 = findReg(regList, insn->r3);
    if( dbg ) { 
        fprintf(dbgF,"\n%s ",insn->r1);
        if (r1>=0) printReg(dbgF,regList[r1]); 
        fprintf(dbgF," %s ",insn->r2);
        if (r2>=0) printReg(dbgF,regList[r2]); 
        fprintf(dbgF," %s ",insn->r3);
        if (r3>=0) printReg(dbgF,regList[r3]); 
        fprintf(dbgF," | ");
    } 
    #if 0
    if (deriveUnknown(regList,insn)) {
        if (dbg) { printf( "\n--> unpred. " ); printReg(stdout,regList[r1]);}
        return 1;
    }
    #endif
    if (insn->flag==BIV_INST) {
        /*Derivation of inductive instruction*/
        if (dbg) {printf("\nInduction: ");printReg(stdout,regList[r1]);}
        if (regList[r1].t!=VALUE_BIV) {
            biv = createBIV(regList, insn);
            if (regList[r1].t!=VALUE_UNDEF) {
                updateInitVal(biv,regList[r1]);
            }
            regList[r1].t = VALUE_BIV;
            regList[r1].biv = biv;
        }
        else {
            bivInit = regList[r1].biv;
            insnTmp = bivInit->insn;
            if (strcmp(insnTmp->addr,insn->addr) != 0) {
                //different biv of different instruction 
                biv = createBIV(regList, insn);
                updateInitVal(biv,regList[r1]);
                regList[r1].t = VALUE_BIV;
                regList[r1].biv = biv;
            }
            //else do nothing, induction value remains the same for loop
            
        }
        if (dbg) { printf( "\n--> biv: " ); printReg(stdout,regList[r1]);}
        return 1;
    }

    if (streq(op,"beq") || streq(op,"bne")) {
        if (ignore) return 0;
        //add branches for AB / BB detection
    }
    else if (streq(op,"bgez") || streq(op,"blez") || streq(op,"bgtz")) {
        if (ignore) return 0;
        //add branches for AB / BB detection
    }
    else if ( streq(op,"sw")||streq(op,"sb")||streq(op,"sh")||streq(op,"sbu") ){
        PROCESS_STORE;
        //ADD_ASSIGN;
    }
    else if (ignoreRegUpdate(insn->r1)==1) {
        return 0;
    }
    else if (streq(op,"lw")||streq(op,"lb")||streq(op,"lbu")
            ||streq(op,"l.d")||streq(op,"lh")) {
        PROCESS_LOAD;
        //ADD_ASSIGN;
    }
    else if (streq(op,"lui")) {
        setInt(regList+r1, atoi(insn->r2)*65536);
        //ADD_ASSIGN;
    }
    else if( streq(op, "addiu") || streq(op, "addi") )
    {
        setInt(&tmpReg, atoi(insn->r3));
        regOpr("+",regList+r1,regList[r2],tmpReg);
        //ADD_ASSIGN;  
    }
    else if (streq(op,"ori")) {
        setInt(&tmpReg, atoi(insn->r3));
        regOpr("|",regList+r1,regList[r2],tmpReg);
        //ADD_ASSIGN;
    }
    else if( streq(op, "xori") )
    {
        setInt(&tmpReg, atoi(insn->r3));
        regOpr("^",regList+r1,regList[r2],tmpReg);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "andi") )
    {
        setInt(&tmpReg,atoi(insn->r3));
        regOpr("&",regList+r1,regList[r2],tmpReg);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "sll") )
    {
        setInt(&tmpReg,hexValue(insn->r3));
        regOpr("<<",regList+r1,regList[r2],tmpReg);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "srl") || streq(op,"sra") )
    {
        setInt(&tmpReg,hexValue(insn->r3));
        regOpr(">>",regList+r1,regList[r2],tmpReg);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "slti") || streq(op, "sltiu") )
    {
        PROCESS_SLTI;
        //ADD_ASSIGN;  
    }
    else if( streq(op, "slt") || streq(op, "sltu") )
    {
        //PROCESS_SLT;
        ADD_ASSIGN;  
    }
    else if( streq(op, "addu") )
    {
        regOpr("+",regList+r1,regList[r2],regList[r3]);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "subu") )
    {
        regOpr("-",regList+r1,regList[r2],regList[r3]);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "or") )
    {
        regOpr("^",regList+r1,regList[r2],regList[r3]);
        //ADD_ASSIGN;  
    }
    else if( streq(op, "jal") ) *jal = 1;
    else if( streq(op, "j") );
    else {
        if (dbg) printf( "Ignoring opcode: %sn", op );
        regUnknown(regList+r1);
    }
    if (dbg) { 
        fprintf(dbgF,"\n ==> %s ",regList[r1].name); 
        printReg(dbgF,regList[r1]);
    }

    clrReg(&tmpReg);
    clrReg(&writeAddr);
    clrReg(&writeValue);
    return 1;
}

/* join different incoming mem/reg values into one representative value
 * For loop header (lpHeader = 1), we ignore incoming values outside the loop, 
 * only join values from the back-edge. By merging values 
 * from current edge & back edge, we merge values from 
 * different iterations into one and identify loop invariants.
 * Non-invariant becomes unpredictable*/
static int joinBranch(inf_proc_t *iproc, inf_node_t *ib, int isLpHead) {
    int dbg = 0;
    int active_reg = 14;
    int i,j,id;

    cfg_node_t *bb, *src;
    inf_node_t *isrc;
    cfg_edge_t *edge;
    reg_t *regSrc, *regCur, *regList;

    int bflag, flag, cmpFlag;
    biv_p biVar; 
    loop_t *lp;
    int cpFwd;

    char str[12];

    bb = ib->bb;
    lp = getIbLoop(ib);
    if (dbg) {
        printf("\nMerge incoming of c%d.%d, e:%d sa:%s, lpHeader %d ",
                iproc->proc->id,bb->id,cntExec,ib->insnlist[0].addr,isLpHead);
        fprintf(dbgExec,"\nMerge incoming of c%d.%d, e:%d sa:%s lpHeader %d",
                iproc->proc->id,bb->id,cntExec,ib->insnlist[0].addr,isLpHead);
    }
    
    if (dbg && 0) {
        printf("\nBefore merge\n");
        regList = (reg_t*)(ib->regListIn);
        for (j=11; j<26; j++) {printf("Reg %d: ",j);printReg(stdout,regList[j]);}
    }

    bflag = 0;
    /*merge all out. values of predecessor to in. value of current blk*/
    for (i=0; i<bb->num_in; i++) {
        edge = bb->in[i];
        src = edge->src;
        isrc = &(iproc->inf_cfg[src->id]);
        if (isLpHead) {//for lpHeader node: ignore in values from outside lp
            if (src->id < bb->id) continue; 
        }
        if (dbg) {
            fprintf(dbgExec,"\nsrc c%d.%d L:%d, dst: c%d.%d L:%d",
                iproc->proc->id, src->id, isrc->loop_id, 
                iproc->proc->id, bb->id, ib->loop_id);
        }

        regCur = (reg_t*)(ib->regListIn);
        regSrc = (reg_t*)(isrc->regListOut);
        /*merge memory value*/
        flag = mergeMemList( &(ib->memListIn), isrc->memListOut);
        if (dbg && flag) fprintf(dbgExec,"\n Changed in mem value");
        if (flag) bflag = 1;
        /*merge register value*/
        if (dbg) fprintf(dbgExec,"\nMerge register list");
        for (j=0; j< NO_REG; j++) {
            if (dbg && !regEq(regCur[j],regSrc[j])) {
                fprintf(dbgExec,"\n %s c%d.%d ",regCur[j].name,
                            iproc->proc->id,bb->id);
                printReg(dbgExec,regCur[j]);
                fprintf(dbgExec," <= rS: c%d.%d ",iproc->proc->id,src->id);
                printReg(dbgExec,regSrc[j]);
            }
            flag = mergeReg( regCur+j, regSrc[j], isLpHead );
            if (dbg && flag) {
                fprintf(dbgExec," => %s = ",regCur[j].name);
                printReg(dbgExec,regCur[j]);
            }
            if (flag) bflag = 1;
        }

    }

    if (dbg && 0) {
        printf("\nAfter merge, change:%d\n",bflag);
        regList = (reg_t*)(ib->regListIn);
        for (j=11; j<26; j++) {printf("Reg %d:",j);printReg(stdout,regList[j]);}
    }
    return bflag;
}

/*Check if insn is a base loop induction insn $1 = $1 + k*/
extern int gcd(int a, int b);

/*** Check basic induction format ***
 * i = i + k 
 * addu $1 $1 $2, $2=k
 * addi $1 $1 k
 * return k
 * return INFINITY if k not defined
 ***/
/*Derivation of unknown value, in addition to available derive*/
int deriveUnknown(reg_t *regList, insn_t *insn) {
    int dbg = 0;
    int flag;

    int regPos1, regPos2, regPos3;
    regPos1 = findReg(regList, insn->r1);
    regPos2 = findReg(regList, insn->r2);
    regPos3 = findReg(regList, insn->r3);
    flag = 0;
    if ( (regPos2>-1 && regList[regPos2].t==VALUE_UNPRED)
        && (regPos3>-1 && regList[regPos3].t==VALUE_UNPRED) ) {
        regUnknown(regList+regPos1);
        flag = 1;
    }
    return flag;
}
int  biv2LoopId(biv_p biv) {
    int         dbg = 0;
    int         i;
    int         bivAddr, lpAddrStr, lpAddrEnd;
    cfg_node_t  *bb;
    insn_t      *insn;
    loop_t      *lp;

    insn = (insn_t*)(biv->insn);
    for (i=1; i<num_tcfg_loops; i++) {
        lp = loops[i]; 
        bb = lp->head->bb; lpAddrStr = bb->sa;
        bb = lp->tail->bb; lpAddrEnd = bb->sa+bb->size-1; 
        bivAddr = hexValue(insn->addr);
        if (lpAddrStr <= bivAddr && bivAddr<lpAddrEnd) {
            if (dbg) {
                fprintf(dbgExec,"\n BIV inst 0x%s in L%d ",insn->addr,lp->id);
                printBIV(dbgExec,biv);
            }
            return lp->id;
        }
    }
    return -1;//cannot find
}
/*Expand addrExpr to non-recursive, non-biv expression*/
void expandAddrExpr(expr_p exp, reg_t  *addrExpr) {
    int         dbg = 0;
    reg_t*      curReg;
    worklist_p  expandList;
    biv_p       biv;
    int         i, varNum, lpId;

    if (dbg) {fprintf(dbgExec,"\nAddrExpr: ");printReg(dbgExec,*addrExpr);}
    clrExpr(exp);
    expandList = NULL;
    addToWorkList(&expandList,addrExpr);
    while (!isEmpty(expandList)) {
        curReg = (reg_t*) removeOneFromWorkList(&expandList);
        if (dbg){fprintf(dbgExec,"\nExpand reg: ");printReg(dbgExec,*curReg);}
        if (dbg) {fprintf(dbgExec,"\nCur expr: ");printExpr(dbgExec,exp);}
        switch (curReg->t) {
            case VALUE_UNDEF:
                fprintf(dbgExec,"\nWarning: unknown parameter in addrExpr");
                varNum = exp->varNum;
                strcpy( exp->value[varNum].para, "T" );
                exp->coef[varNum] = 1;
                exp->varNum++;
                break;
            case VALUE_CONST:
                exp->k += curReg->val;
                break;
            case VALUE_PARA:
                fprintf(dbgExec,"\nWarning: unknown parameter in addrExpr");
                varNum = exp->varNum;
                cpyReg( &(exp->value[varNum]), *curReg);
                exp->coef[varNum] = 1;
                exp->varNum++;
                break;
            case VALUE_BIV:
                //biv is represented in addrExpr as lpId * stride
                varNum = exp->varNum;
                lpId = biv2LoopId(curReg->biv);
                setInt( &(exp->value[varNum]), lpId );
                //cpyReg( &(exp->value[varNum]), *curReg);
                exp->coef[varNum] = curReg->biv->stride;
                if (dbg) fprintf(dbgExec," %d:L%d",
                                exp->coef[varNum],exp->value[varNum].val);
                exp->varNum++;
                addToWorkList(&expandList,&(curReg->biv->initVal));
                break;
            case VALUE_EXPR:
                exp->k += curReg->expr->k;
                for (i=0; i<curReg->expr->varNum; i++) {
                    varNum = exp->varNum;
                    exp->coef[varNum] = curReg->expr->coef[i];
                    if (curReg->expr->value[i].t==VALUE_BIV) {
                        biv = curReg->expr->value[i].biv;
                        lpId = biv2LoopId(biv);
                        setInt( &(exp->value[varNum]), lpId );
                        exp->coef[varNum] *= biv->stride;
                        if (dbg) fprintf(dbgExec," %d:L%d",
                                exp->coef[varNum],exp->value[varNum].val);
                        addToWorkList(&expandList,&(biv->initVal));
                    }
                    else {
                        cpyReg( &(exp->value[varNum]), curReg->expr->value[i]); 
                    }
                    exp->varNum++;
                }
                break;
            default:
                printf("\n Panic (expandBIV) , unknown type %d",curReg->t);
                exit(1);
        }
        if (exp->varNum==MAX_EXPR_LEN) {
            printf("\nPanic, exceed MAX_EXPR_LEN ");
            exit(1);
        }
    }
    if (dbg) {fprintf(dbgExec,"\nExpanded expr: ");printExpr(dbgExec,exp);}
}
static void readSymExecResult(inf_node_t *ib) {
    int dbg = 0;
    int k, r1,r2,r3;
    insn_t  *insn;
    reg_t   *listIn, *listOut;
    char    jal, ignore;  // procedure call cancels conflict
    reg_t   tmpReg, exprReg;
    initReg(&tmpReg);initReg(&exprReg);

    cfg_node_t  *bb;
    dat_inst_t  *dat;
    int         datNum;

    /*init data inst in bb node*/
    bb = ib->bb;
    if (ib->num_insn!=bb->num_inst) {printf("\nPanic ib!=bb"); exit(0);}

    bb->num_d_inst=0;
    for (k=0; k<ib->num_insn; k++) {
        insn = &(ib->insnlist[k]);
        if (isLoadInst(insn->op) || isStoreInst(insn->op)) bb->num_d_inst++;
    }
    bb->d_instlist = calloc(bb->num_d_inst,sizeof(dat_inst_t));

    //pairDetectionFlag = 1;

    listIn = (reg_t*)(ib->regListIn);
    listOut = (reg_t*)(ib->regListOut);
    copyMemList( &(ib->memListOut), ib->memListIn);
    for ( k=0; k<NO_REG; k++ ) {
        cpyReg(listOut+k, listIn[k]);
    }

    datNum = 0;
    for ( k = 0; k < ib->num_insn; k++ ) {
        insn = &(ib->insnlist[k]);
        if (isLoadInst(insn->op) || isStoreInst(insn->op)) {
            r1 = findReg(listOut, insn->r1);
            r2 = findReg(listOut, insn->r2);
            r3 = findReg(listOut, insn->r3);
            if (dbg) {
                printf("\n ");printInstr(insn);
                #if 0
                printf("\n%s ",insn->r1);
                if (r1>=0) printReg(stdout,listOut[r1]); 
                printf(" %s ",insn->r2);
                if (r2>=0) printReg(stdout,listOut[r2]); 
                #endif
                printf("| %s = ",insn->r3);
                if (r3>=0) printReg(stdout,listOut[r3]); 
                printf("\n");
            }
            setInt(&tmpReg, atoi(insn->r2));

            dat             = &(((dat_inst_t*)(bb->d_instlist))[datNum]);
            datNum++;
            dat->insn       = &(ib->insnlist[k]);
            dat->addr_set   = NULL;
            dat->resideLpId = getIbLoop(ib)->id;
            regOpr("+",&exprReg,listOut[r3],tmpReg);
            initExpr( &(dat->addrExpr) );
            expandAddrExpr( &(dat->addrExpr), &exprReg);
            if (dbg) {printDataRef(dbgExec,dat);}
        }
        ignore = jal; jal = 0;
        regDerive(ib,insn,ignore,&jal,0);/*derive normally*/
    }
    clrReg(&tmpReg);clrReg(&exprReg);
}
static void analyzeBlock(inf_node_t *ib) {
    int     dbg = 0;
    int     i, j, k, m, id;
    int     r1, regPos2, regPos3;


    char    jal, ignore;  // procedure call cancels conflict
    char    flag;
    insn_t  *insn;
    reg_t   *regList, *listIn, *listOut, tmpReg;
    FILE    *fp = stdout;

    jal = 1;

    initReg(&tmpReg);
    if (dbg) {
        fprintf(dbgExec,"\nAnalyze block (%d,%d)", ib->bb->proc->id, ib->bb->id);
        printf("\nAnalyze block (%d,%d)", ib->bb->proc->id, ib->bb->id);
        if (0) printMemList(fp,ib->memListOut);
        fflush(dbgExec);
    }

    //copy in-status to out-status
    copyMemList(&(ib->memListOut), ib->memListIn);
    listIn = (reg_t*)(ib->regListIn);
    listOut= (reg_t*)(ib->regListOut);
    for (j=0;j<NO_REG;j++) cpyReg(listOut+j,listIn[j]);
    regList = (reg_t*)(ib->regListOut);
    for( k = 0; k < ib->num_insn; k++ ) {
        insn = &(ib->insnlist[k]);
        if(dbg) { 
            //printf( "\n[%2d,%2d,%2d] ", i, j, k ); 
            printf("\n k:%d ",k); printInstr( insn ); 
            //fprintf(dbgExec,"\n k:%d ",k);fprintInstr( dbgExec, insn ); 
        }

        ignore = jal; jal = 0;
        regDerive(ib,insn,ignore,&jal,dbg);/*derive normally*/
        if (dbg) printf("\n");
    } // end for instr
    if (dbg) {
        fprintf(dbgExec,"\nFinish block (%d,%d)", ib->bb->proc->id, ib->bb->id);
        printf("\nFinish block (%d,%d)", ib->bb->proc->id, ib->bb->id);
        if (0) printMemList(fp,ib->memListOut);
        fflush(dbgExec);fflush(stdout);
    }
}

/*Check if regName is redefined in insn*/
static int regRedefined(char* regName, insn_t *insn) {
    char *op = insn->op;
    int  regPos1;

    /*regName is redefined if (insn r1=regName, insn is assigment*/
    if (strcmp(regName, insn->r1)!=0) return 0;
    //regName == insn->r1
    if (streq(op,"lw") || streq(op,"lb") || streq(op,"lh") || streq(op,"lbu"))
        return 1;
    if (streq(op,"addu") || streq(op,"addiu") || streq(op,"addi"))
        return 1;
    if (streq(op,"ori") || streq(op,"xori") || streq(op,"andi"))
        return 1;
    if (streq(op,"sll") || streq(op,"srl") || streq(op,"slti") 
            || streq(op,"sltu")) return 1;
    if (streq(op,"subu") || streq(op,"or") || streq(op,"xor"))
        return 1;
    return 0;
}
static void reInit(inf_proc_t* p, int startId, int endId, int *visited) {
    int dbg = 0;
    inf_node_t *ib;
    int i;
    //reinitialize everything within the loop
    for (i=startId; i<=endId; i++) {
        visited[i]=0;
        if (i==0) continue;//bb0 has no incoming block
        ib = &(p->inf_cfg[i]); 
        clearRegList(ib->regListIn);
        clearRegList(ib->regListOut);
        freeMemList( &(ib->memListIn));
        //ib->memListIn = NULL; //ERR: memory leak
        freeMemList( &(ib->memListOut));
        //ib->memListOut = NULL; //ERR: memory leak
        if (dbg) {
            fprintf(dbgExec,"\nReinit c%d.%d",p->proc->id,ib->bb->id);
            printMemList(dbgExec,ib->memListIn);
            printMemList(dbgExec,ib->memListOut);
            fflush(dbgExec);
        }
    }
}

/*** Register expansion framework to trace reg. value and induction var ***
 * Detect loop induction variable (w/o initial value)
 * Perform register expansion (except for induction var)
 * Collect init value of induction vars
 * Collect AB - BB conflicting pairs 
 **/
/*symbolic execution within a particular loop*/
int loopSymExec(loop_t *lp, inf_proc_t *p, int mustFlag, P_Queue **outQueue) {
    int dbg = 0;
    int i,j,k;
    int lpTailId, lpHeadId;

    inf_node_t *inNode;
    loop_t *inLp;
    inf_node_t *ib;
    cfg_node_t *src, *dst;
    int pchange, bbchange;
    reg_t * listIn, *listOut;

    P_Queue *pQueue,*qElem;
    int ibId, lpId;
    int *pInt;
    int visited[MAX_OVRL_NODES];

    if (lp) lpHeadId = lp->head->bb->id;
    else lpHeadId = 0; //outermost loop
    if (lp && lp->tail) lpTailId = lp->tail->bb->id;
    else lpTailId = p->num_bb-1;

    inNode = NULL;
    if (lp) lpId = lp->id; else lpId = 0;

    //check if fixed-point - no change in in-status
    pchange = 0;
    ib = &(p->inf_cfg[lpHeadId]);
    listIn = (reg_t*)(ib->regListIn);
    for (i=0; i<ib->bb->num_in; i++) {
        inNode = &(p->inf_cfg[ib->bb->in[i]->src->id]);
        listOut = (reg_t*)(inNode->regListOut);
        for (k=0; k<NO_REG; k++) 
            if ( !regEq(listIn[k],listOut[k]) ) {
                if (0) {
                    printf("\nRegIn ");printReg(stdout, listOut[k]);
                    printf("\nRegCur ");printReg(stdout,listIn[k]);
                }
                pchange = 1;
            }
    }

    if (dbg) {
        fprintf(dbgExec,"\nSym.exec of loop L%d, [%d,%d]",
                lpId,lpHeadId,lpTailId);
        printf("\nSym.exec of loop L%d, [%d,%d], %d",
                lpId,lpHeadId,lpTailId,mustFlag);
    }
    if (!mustFlag && !pchange && inNode!=NULL) {
        if (dbg) fprintf(dbgExec,"\nNo change in loop L%d",lpId);
        if (dbg) printf("\nNo change in loop L%d",lpId);
        return 0;
    }

    reInit(p, lpHeadId, lpTailId, visited);

    //copy in-status to loop head
    ib = &(p->inf_cfg[lpHeadId]);
    listIn = (reg_t*)(ib->regListIn);
    if (ib->bb->num_in>0) {
        inNode = &(p->inf_cfg[ib->bb->in[0]->src->id]);
        listOut = (reg_t*)(inNode->regListOut);
        if (dbg) {
            printf("\nCopy ACS from loop pre-header  c%d.%d -> c%d.%d",
                    p->proc->id,inNode->bb->id, p->proc->id, ib->bb->id);
            //printf("\nACS pre-header c%d.%d ",p->proc->id, inNode->bb->id);
            //printRegList(stdout,listOut);
        }
        copyMemList( &(ib->memListIn), inNode->memListOut);
        for (k=0; k<NO_REG; k++) {
            if (!regEq( listIn[k], listOut[k] )) {
                if (dbg) {
                    printf("\n Changed: %s, cur: ",listIn[k].name);
                    printReg(stdout,listIn[k]);
                    printf(" new: "); printReg(stdout,listOut[k]);
                }
                cpyReg(listIn+k, listOut[k]);
            }
        }
    }
    else inNode = NULL;

    //perform fixed-point sym.exec within the loop
    while (1) {
        //merge lpHeader input with previous iteration value
        ib = &(p->inf_cfg[lpHeadId]);
        bbchange = joinBranch(p,ib,1);
        if (dbg) {
            fprintf(dbgExec,"\nStart next L%d iter at lpHeader c%d.%d",lpId,
                    p->proc->id, ib->bb->id);
            printf("\nStart next L%d iter at lpHeader c%d.%d",lpId,
                    p->proc->id, ib->bb->id);
        }
        if (bbchange==0 && visited[lpHeadId]==1) break;

        visited[lpHeadId] = 1;
        reInit(p, lpHeadId+1, lpTailId, visited);
        pQueue = NULL;
        p_enqueue(&pQueue,&lpHeadId,lpHeadId);
        while (!p_queue_empty(&pQueue)) { 
            cntExec++; //count total node sym.exec performed
            if (cntExec==1000) {
                printf("\nToo many iterations, no terminate?");exit(1);
            }
            pInt = (int*)p_dequeue(&pQueue);ibId = *pInt;
            ib = &(p->inf_cfg[ibId]);

            if (dbg) {//print outgoing nodes of current node
                fprintf(dbgExec,"\nA_ bb %d:%d (",ibId,ib->bb->id);
                if (ib->bb->out_n) {
                    dst = ib->bb->out_n->dst;
                    fprintf(dbgExec,"%d,",dst->id);fflush(dbgExec);
                }
                if (ib->bb->out_t) {
                    dst = ib->bb->out_t->dst;
                    fprintf(dbgExec,"%d,",dst->id);fflush(dbgExec);
                }
                fprintf(dbgExec,")\n");fflush(dbgExec);
            }

            //if (dbg) fprintf(dbgExec,"\nIncoming b%d ",ib->bb->id);
            for (i=0; i<ib->bb->num_in; i++) {
                /*Assume: bb->in[i]->id > bb->id --> loop back edge -> loop head*/
                inNode = &(p->inf_cfg[ib->bb->in[i]->src->id]);
                //if (dbg) fprintf(dbgExec," b%d ",inNode->bb->id);
                if (inNode->bb->id >= ib->bb->id) ib->bb->loop_role = LOOP_HEAD;
            }
            if (ib->bb->id > lpHeadId && ib->bb->loop_role == LOOP_HEAD) {
                //ib is lpHead of an inner loop 
                inLp = getIbLoop(ib);
                loopSymExec(inLp,p,!visited[ib->bb->id], &pQueue);
                if (dbg) {
                    fprintf(dbgExec,"\nBack to L%d [%d,%d]",lpId,lpHeadId,lpTailId);
                    printf("\nBack to L%d [%d,%d]",lpId,lpHeadId,lpTailId);
                }
                visited[ib->bb->id] = 1;
                continue;
            }

            /*Merge in-status of all predecessor bb*/
            if (ib->bb->id==lpHeadId) {
                bbchange = 1;
            }
            else {
                bbchange = joinBranch(p,ib, 0);
                if (dbg) fprintf(dbgExec,"\nJoin in-status of c%d.%d",
                        p->proc->id, ib->bb->id);
            }
            if (bbchange || visited[ib->bb->id]==0) {
                visited[ib->bb->id]=1;
                analyzeBlock(ib);
                //do not add loop header back to the queue
                if (ib->bb->out_n) {
                    dst = ib->bb->out_n->dst;
                    if (lpHeadId < dst->id && dst->id <= lpTailId)
                        //within the current loop
                        p_enqueue(&pQueue,&(dst->id),dst->id);
                    else if (dst->id > lpTailId)
                        //outside the current loop
                        p_enqueue(outQueue,&(dst->id),dst->id);
                    //fprintf(dbgExec,"Out_n %d\n",dst->id);fflush(dbgExec);
                }
                if (ib->bb->out_t) {
                    dst = ib->bb->out_t->dst;
                    if (lpHeadId < dst->id && dst->id <= lpTailId)
                        //within the current loop
                        p_enqueue(&pQueue,&(dst->id),dst->id);
                    else if (dst->id > lpTailId)
                        //outside the current loop
                        p_enqueue(outQueue,&(dst->id),dst->id);
                    //fprintf(dbgExec,"Out_t %d \n",dst->id);fflush(dbgExec);
                }
            }
        }//end while queuing
    }
    
    if (dbg && lp) fprintf(dbgExec,"\nFinish sym.exec of loop l%d",lp->id);
    return 1;
}
int doSymExec() {
    int dbg = 0;
    int i, j, k, m, id;
    inf_proc_t  *p;
    reg_t       *regList;
    inf_node_t  *ib;

    if (dbg) {
        printf("\nStart reg. expansion");fflush(stdout);
        fprintf(dbgExec,"\nStart reg. expansion");
    }

    /*perform register expansion*/
    for( i = 0; i < prog.num_procs; i++ ) {
        p = &(inf_procs[i]);
        ib = &(p->inf_cfg[0]);
        regList = (reg_t*)(ib->regListIn);
        setInt(regList+0,0);
        setInt(regList+28,GLOBAL_START);
        setInt(regList+29,STACK_START);
        regUnknown(regList+30);

        loopSymExec(NULL,p,1,NULL);//perform symexec from outermost loop
        if (dbg) fprintf(dbgExec,"\nEnd of p: %d",i);fflush(dbgExec);
    }
    if (dbg) {
        printf("\nEnd reg. expansion");fflush(stdout);
        fprintf(dbgExec,"\nEnd reg. expansion");
    }
}

static int initBlock() {
    int dbg=0;
    int i,j,k;
    inf_proc_t *p;
    inf_node_t *ib;
    reg_t *regList;
    for( i = 0; i < prog.num_procs; i++ ) {
        p = &(inf_procs[i]);
        for( j = 0; j < p->num_bb; j++ ) {
            ib = &(p->inf_cfg[j]);
            ib->memListIn = NULL;
            ib->memListOut= NULL;
            //init regListIn
            ib->regListIn = calloc(NO_REG,sizeof(reg_t));
            initRegSet( (reg_t*)(ib->regListIn));
            //init regListOut
            ib->regListOut = calloc(NO_REG,sizeof(reg_t));
            initRegSet( (reg_t*)(ib->regListOut));
        }
    }
}
static void initAll() {
    dbgExec = fopen("dbg_exec.dbg","w");
    setDebugFile("dbg_comp.dbg");
    initBlock();

}
static void freeAll() {
    fclose(dbgExec);
}

int execute() {
    int dbg = 0;
    int i,j,k;
    inf_proc_t *p;
    inf_node_t *ib;
    reg_t *listIn, *listOut;

    ticks a,b;

    initAll();
    cntExec = 0;

    a= getticks();
    /* do symbolic execution to detect register with constant value */
    /* for detection of induction value, static memory address, etc */
    conflictDetectFlag = 0;
    traceInductionFlag = 0;
    traceMemFlag = 0;
    if (dbg) printf ("\nTrace constant value");
    doSymExec();
    if (dbg) printf ("\nFinish tracing constant value");

    /* reinitialize register value, keep known const*/
    for (i=0; i<prog.num_procs; i++) {
        p= &(inf_procs[i]);
        for (j=0; j<p->num_bb; j++) {
            ib = &(p->inf_cfg[j]); 
            listIn = (reg_t*)(ib->regListIn);
            listOut= (reg_t*)(ib->regListOut);
            for (k=0; k<NO_REG; k++) {
                //if ( k == 28 || k == 29 ) continue;// || k == 30
                if (listIn[k].t!=VALUE_CONST) {
                    clrReg( listIn+k );
                    clrReg( listOut+k );
                }
            }
        }
    }
    
    /* detect induction value in register */
    if (dbg) printf ("\nDetect induction instruction");
    detectBIV();
    if (dbg) printf ("\nFinish detecting induction instruction");
    
    #if 1
    /* do symbolic execution again, now with induction var */
    /* NOTE: and with memory modeling if implemented */
    traceMemFlag = 1;
    traceInductionFlag = 1;
    if (dbg) printf("\nTrace mem & induction value");
    doSymExec();
    if (dbg) printf("\nFinish tracing mem & induction value");

    /* read symbolic execution result */
    if (dbg) {
        fprintf(dbgExec,"\nRead sym.exec result");fflush(dbgExec);
        printf("\nRead sym.exec result");fflush(stdout);
    }
    //conflictDetectFlag = 1;
    for (i=0; i<prog.num_procs; i++) {
        p= &(inf_procs[i]);
        for (j=0; j<p->num_bb; j++) {
            ib = &(p->inf_cfg[j]); 
            readSymExecResult(ib);
        }
    }
    #endif
    b= getticks();
	printf("\n===================================================\n");
	printf("Establish trace link %lf secs\n", (b - a)/((1.0) * CPU_MHZ));
	printf("===================================================\n");

    freeAll();

    return 1;	
}
#if 0
/*** Loop induction variable detection ***
* Simple scheme to detect loop induction variable
* Detect basic induction variable
** Find pattern: $i = $i + k -> basic induction ($i, INIT ,k)
** Check if $i is NOT redefined within the loop scope -> verified $i
** Back track before loop header to get $i = INIT
* Detect derived induction variable
** Find pattern $j = $i + k || $j = $i * k
** Check if $j is NOT redefined within the loop -> verified $j
* Set induction value for induction register in the loop
***/
static void detectBIV_old() {
    int dbg = 0; /*debug flag*/
    int i,j,k,m;
    biv_p biVar;
    
    insn_t *insn;
    int i_str, i_init;                          /*induction var detection*/
    reg_t *regList;
    worklist_p  biv_list, bivNode, prvNode;

    loop_t *cur_lp, *par_lp;                    /*lp in consideration*/
    P_Queue *pQueue, *eElem;                    /*processing priority queue*/
    inf_node_t *head,*tail, *cur;               /*for traversing inf_node*/
    cfg_node_t *blk, *blk_head, *blk_tail, *dst;
    cfg_edge_t *edge;
    int visited[256];                           /*visited flag*/

    if (dbg) {
        fprintf(dbgExec,"\nStart detecting induction var %d",num_tcfg_loops);
        fflush(dbgExec);
    }
    //fprintf(dbgExec,"\nFlag 0");fflush(dbgExec);
    for (i=0; i<num_tcfg_loops; i++) {
        cur_lp = loops[i]; 
        //fprintf(dbgExec,"\nLoops[%d]->id = %d",i,cur_lp->id);
        if (dbg && (!cur_lp || !(cur_lp->head) || !(cur_lp->tail))){
            if (!cur_lp) {
                fprintf(dbgExec,"\nNull lp_id %d",i);fflush(dbgExec);
            }
            else {
                if (!cur_lp->head) fprintf(dbgExec,"\nNull lp head");
                else fprintf(dbgExec,"\nHead id: %d",cur_lp->head->bb->id);
                if (!cur_lp->tail) fprintf(dbgExec,"\nNull lp tail");
                else fprintf(dbgExec,"\nTail id: %d",cur_lp->tail->bb->id);
            }
            fflush(dbgExec);
        }
        if (cur_lp==NULL || cur_lp->head==NULL || cur_lp->tail==NULL) continue;

        biv_list = NULL;
        blk_head = cur_lp->head->bb;
        blk_tail = cur_lp->tail->bb;
        if (dbg) {
            fprintf(dbgExec,"\nLoop %d head (%d:%d) tail (%d:%d)",
                cur_lp->id, blk_head->proc->id, blk_head->id,
                    blk_tail->proc->id, blk_tail->id);
            fflush(dbgExec);
        }
        for (j=blk_head->id; j<=blk_tail->id; j++) visited[j]=0;

        /*scan from lp to tail, detect basic induction var*/
        pQueue = NULL;
        p_enqueue(&pQueue, blk_head, blk_head->id);
        visited[blk_head->id]=1;
        while (!p_queue_empty(&pQueue)) {
            blk = (cfg_node_t*) p_dequeue(&pQueue);
            /*add outgoing blk to queue*/
            edge = blk->out_n;
            if (edge) {
                dst = edge->dst;
                if (blk_head->id<=dst->id && dst->id<=blk_tail->id
                        &&visited[dst->id]==0) {
                    p_enqueue(&pQueue, dst, dst->id);
                    visited[dst->id]=1;
                }
            }
            edge = blk->out_t;
            if (edge) {
                dst = edge->dst;
                if (blk_head->id<=dst->id && dst->id<=blk_tail->id
                        &&visited[dst->id]==0) {
                    p_enqueue(&pQueue, dst, dst->id);
                    visited[dst->id]=1;
                }
            }

            /*scan through inst, detect basic induction var*/
            cur = &(inf_procs[blk->proc->id].inf_cfg[blk->id]);
            if (dbg) {
                fprintf(dbgExec,"\nBB (%d,%d) L%d  num:%d  sa:%s",blk->proc->id,
                blk->id,getIbLoop(cur)->id, cur->num_insn, cur->insnlist[0].addr);
                fflush(dbgExec);
            }

            regList = (reg_t*)(cur->regListOut);
            for (k=0; k<cur->num_insn; k++) {
                insn = &(cur->insnlist[k]);
                /*Check basic induction variable*/
                if (insn->flag==BIV_INST) continue;
                i_str = checkBivStride(regList,insn);
                if (i_str != INFINITY) {
                    if (dbg) {
                        fprintf(dbgExec,"\nBIV: insn: %s (%s,0,%d)",
                            insn->addr,insn->r1,i_str);
                        fflush(dbgExec);
                    }
                    //insn->flag = BIV_INST;
                    //biVar->lp = cur_lp;
                    biVar = createBIV(regList,insn);
                    addToWorkList(&biv_list, biVar);
                }
                /*  Check whether an induction variable register is redefined
                 *  An induction variable candidate is a real induction var iff
                 *  (1) it is not redefined within the lp
                 *  (2) it is saved to memory before it is redefined
                 */
                bivNode = biv_list; prvNode = NULL;
                while (bivNode) {
                    biVar = (biv_p)bivNode->data;
                    /*Check if BIV is saved*/
                    if (isStoreInst(insn->op) 
                            && strcmp(biVar->regName,insn->r1)==0) {
                        biVar->flag |= BIV_SAVED;
                        if (dbg) {
                            fprintf(dbgExec,"\nSaved: insn %s |%s(%s,0,%d)",
                                    insn->addr,((insn_t*)(biVar->insn))->addr, 
                                        biVar->regName,biVar->stride);
                        }
                    }

                    /*Check if BIV is redefined*/
                    if (biVar 
                    && strcmp(((insn_t*)(biVar->insn))->addr,insn->addr)!=0 
                        && (biVar->flag & BIV_SAVED)==0
                        && regRedefined(biVar->regName, insn)) {
                        //fprintf(dbgExec,"\nFlag 4");fflush(dbgExec);
                        if (dbg) {
                            fprintf(dbgExec,"\nRedefined: insn %s |%s(%s,0,%d)",
                                    insn->addr,((insn_t*)(biVar->insn))->addr, 
                                        biVar->regName,biVar->stride);
                            fflush(dbgExec);
                        }
                        //biVar->is->flag = 0;//may be ind. var of inner lp
                        free(biVar);
                        bivNode->data = NULL;
                        if (prvNode) {
                            //fprintf(dbgExec,"\nFlag 5");fflush(dbgExec);
                            prvNode->next = bivNode->next;
                            free(bivNode); 
                            bivNode = prvNode->next;
                        } 
                        else {//prvNode = NULL
                            //fprintf(dbgExec,"\nFlag 6");fflush(dbgExec);
                            biv_list = bivNode->next;    
                            free(bivNode);
                            if (biv_list) bivNode = biv_list->next;
                            else bivNode = NULL;
                        }
                    }
                    else {
                        prvNode = bivNode;
                        bivNode = bivNode->next;
                    }
                }//end while 
            }//end for insn
        }//end for, end detecting BIV in this lp
        for (bivNode = biv_list; bivNode; bivNode=bivNode->next) {
            biVar = bivNode->data;
            if (biVar) ((insn_t*)(biVar->insn))->flag |= BIV_INST;
        }
        bivNode = biv_list;
        if (dbg) {
            fprintf(dbgExec,"\nDetected inVar ");
            while (bivNode!=NULL) {
                biVar = (biv_p) bivNode->data;
                fprintf(dbgExec," (%s,0,%d) ",biVar->regName,biVar->stride);
                bivNode = bivNode->next;
            }
        }

        //setInductionValueForAllReg();//error
        cur_lp->biv_list = biv_list;
        if (dbg) {
            fprintf(dbgExec,"\n BIV list of L%d ",cur_lp->id);
            for (bivNode = biv_list; bivNode; bivNode = bivNode->next) {
                biVar = (biv_p) bivNode->data;
                printBIV(dbgExec,biVar);
            }
        }
    }
}
int loopSymExec(loop_t *lp, inf_proc_t *p, int mustFlag, P_Queue **outQueue) {
    int dbg = 0;
    int i,j,k;
    int lpTailId, lpHeadId;

    inf_node_t *inNode;
    loop_t *inLp;
    inf_node_t *ib;
    cfg_node_t *src, *dst;
    int pchange, bbchange;
    reg_t * listIn, *listOut;

    P_Queue *pQueue,*qElem;
    int ibId, lpId;
    int *pInt;
    int visited[MAX_OVRL_NODES];

    //check if fixed-point - no change in in-status
    pchange = 0;
    ib = &(p->inf_cfg[lpHeadId]);
    listIn = (reg_t*)(ib->regListIn);
    for (i=0; i<ib->bb->num_in; i++) {
        inNode = &(p->inf_cfg[ib->bb->in[i]->src->id]);
        listOut = (reg_t*)(inNode->regListOut);
        for (k=0; k<NO_REG; k++) 
            if ( !regEq(listIn[k],listOut[k]) ) {
                if (0) {
                    printf("\nRegIn ");printReg(stdout, listOut[k]);
                    printf("\nRegCur ");printReg(stdout,listIn[k]);
                }
                pchange = 1;
            }
    }

    if (dbg) {
        fprintf(dbgExec,"\nSym.exec of loop L%d, [%d,%d]",
                    lpId,lpHeadId,lpTailId);
        printf("\nSym.exec of loop L%d, [%d,%d], %d",
                    lpId,lpHeadId,lpTailId,mustFlag);
    }
    if (!mustFlag && !pchange && inNode!=NULL) {
        if (dbg) fprintf(dbgExec,"\nNo change in loop L%d",lpId);
        if (dbg) printf("\nNo change in loop L%d",lpId);
        return 0;
    }

    reInit(p, lpHeadId, lpTailId, visited);

    //copy in-status to loop head
    ib = &(p->inf_cfg[lpHeadId]);
    listIn = (reg_t*)(ib->regListIn);
    if (ib->bb->num_in>0) {
        inNode = &(p->inf_cfg[ib->bb->in[0]->src->id]);
        listOut = (reg_t*)(inNode->regListOut);
        for (k=0; k<NO_REG; k++) {
            if (!regEq( listIn[k], listOut[k] )) {
                if (dbg) {
                    printf("\n Changed: %s, cur: ",listIn[k].name);
                    printReg(stdout,listIn[k]);
                    printf(" new: "); printReg(stdout,listOut[k]);
                }
                cpyReg(listIn+k, listOut[k]);
            }
        }
        copyMemList( &(ib->memListIn), inNode->memListOut);
    }
    else inNode = NULL;

    //perform fixed-point sym.exec  within the loop
    pchange = 0;
    while (pchange) {

    }
}
#endif
