#include "scp_address.h" 
extern int X,Y,B,l1,l2;

void setAddrDebugFile(FILE *fp) {
    dbgAddr = fp;
}

/*** TEMPORAL SCOPE FUNCTIONS ***/
void cpyTS(ts_p dst, ts_p src) {
    dst->loop_id = src->loop_id;
    dst->lw = src->lw; dst->up = src->up; dst->flag = src->flag;
}
void printTS(FILE *fp, ts_p ts) {
    fprintf(fp," (L%d,%d,%d)",ts->loop_id, ts->lw, ts->up );fflush(fp);
}
void printTSset(FILE *fp, worklist_p tsSet) {
    worklist_p  tsNode;
    ts_p        ts;
    for (tsNode = tsSet; tsNode; tsNode = tsNode->next) {
        ts = (ts_p) (tsNode->data);printTS(fp, ts);
    }
}
/*Compare 2 TS, return 1 if tsSet1 == tsSet2, 0 otherwise*/
int eqTSset(worklist_p tsSet1, worklist_p tsSet2, int curLpId) {
    int dbg = 0;
    worklist_p  tsNode1, tsNode2, tmpNode, prvNode;
    ts_p        ts1,ts2;
    int flag;

    tsNode1 = tsSet1;
    tsNode2 = tsSet2;
    flag = 0;
    while (tsNode1 && tsNode2) {
        /*Assume TS in the same loop order*/
        ts1 = (ts_p)(tsNode1->data);
        ts2 = (ts_p)(tsNode2->data);
        if (cmpLpOrder(ts1->loop_id,ts2->loop_id)==1) {//ts1 is outerloop of ts2
            tsNode1 = tsNode1->next; continue;
        }
        else if (cmpLpOrder(ts1->loop_id,ts2->loop_id)==-1) {
            tsNode2 = tsNode2->next; continue;
        }
        else {//ts1->lpId == ts2->lpId
            if (cmpLpOrder(ts1->loop_id, curLpId)==-1) {//ts1=ts2 outerloop of lp
                //do not consider TS of this loop level
                break;
            }
            if (ts1->lw != ts2->lw || ts1->up != ts2->up) return 0;//not equal
            tsNode1 = tsNode1->next;
            tsNode2 = tsNode2->next;
        }
    }
    return 1;
}
/*compare 2 scope, return 0:inner 1:overlap -1:non-overlap*/
int cmpTS(ts_p as1, ts_p as2) {
    if (as2->lw == as1->lw && as1->up ==as2->up)  return EQUAL_TS; //inner scope
    if (as1->lw <= as2->lw && as2->lw <= as1->up) return OLAP_TS;//overlap scope
    if (as1->lw <= as2->up && as2->up <= as1->up) return OLAP_TS; 
    if (as2->lw <= as1->lw && as1->lw <= as2->up) return OLAP_TS; 
    if (as2->lw <= as1->up && as1->up <= as2->up) return OLAP_TS; 
    return SEP_TS;//separated scope
}

/*compare two mem scopes, return 0:identical  1:conflict  -1:not conflict*/
int cmpTSset(worklist_p tsList1, worklist_p tsList2, int lpId) {
    int         dbg = 0;
    worklist_p  tsNode1, tsNode2;
    ts_p        ts1, ts2;
    int         cmp, lpOrder, sameLp;
    int         overlapScp, innerScp;

    /*Assume m1,m2 temporal scope list are arranged innermost to outermost */
    tsNode1 = tsList1; tsNode2 = tsList2;
    sameLp=0; overlapScp = 1; innerScp = 1;
    if (dbg) {
        fprintf(dbgAddr,"\nCompare TS ");printTSset(dbgAddr, tsList1);
        fprintf(dbgAddr,"  ");printTSset(dbgAddr,tsList2);
    }
    while (tsNode1 && tsNode2) {
        ts1 = (ts_p)(tsNode1->data);
        ts2 = (ts_p)(tsNode2->data);
        //as->lp_id > as->next->lp_id
        lpOrder = cmpLpOrder(ts1->loop_id, ts2->loop_id);

        if (lpOrder==1) tsNode2 = tsNode2->next;        //ts1 is outer loop
        else if (lpOrder==-1) tsNode1 = tsNode1->next;  //ts2 is outer loop
        else {//lpOrder == 0:                           //same loop
            sameLp = 1;
            if (isInner(lpId,ts1->loop_id)) {          //only consider from lpId
                cmp = cmpTS(ts1,ts2);
                if (dbg) {fprintf(dbgAddr," L%d,%d",ts1->loop_id,cmp);}
                if (cmp!=EQUAL_TS)  innerScp = 0;
                if (cmp==SEP_TS) { overlapScp =0; break; }
            }
            tsNode1 = tsNode1->next; tsNode2 = tsNode2->next;
        }
    }
    if (innerScp)           cmp = EQUAL_TS;
    else if (overlapScp)    cmp = OLAP_TS;
    else                    cmp = SEP_TS;
    if (dbg && cmp!=SEP_TS && cmp!=EQUAL_TS ) {
        fprintf(dbgAddr,"\nOverlap L%d",lpId);
        printTSset(dbgAddr,tsList1);
        printTSset(dbgAddr,tsList2);
    }
    return cmp;
}
/*Merge 2 TS, don't merge inner loop of lpId, assume tsSet in same loop order, 
    return 1 if dstTS is enlarged, 0 if otherwise*/
int mergeTSset(worklist_p dstSet, worklist_p srcSet, int lpId) {
    int dbg = 0;
    worklist_p  dstNode, srcNode, tmpNode, prvNode;
    ts_p        srcTS,dstTS;
    int flag;

    dstNode = dstSet;
    srcNode = srcSet;
    flag = 0;
    while (dstNode && srcNode) {
        srcTS = (ts_p)(srcNode->data);
        dstTS = (ts_p)(dstNode->data);
        //don't merge inner scope
        if (cmpLpOrder(dstTS->loop_id,lpId)<0) return;
        if (cmpLpOrder(srcTS->loop_id,lpId)<0) return;

        if (dstTS->loop_id != srcTS->loop_id) {//wrong assumption
            fprintf(dbgAddr,"\nWrong TS order: %d %d",
                    dstTS->loop_id,srcTS->loop_id);
            fprintf(dbgAddr,"\nSRC: ");printTSset(dbgAddr,srcSet);
            fprintf(dbgAddr,"\nDST: ");printTSset(dbgAddr,dstSet);
            fflush(dbgAddr);exit(0);
        }
        if (dstTS->lw > srcTS->lw) {
            dstTS->lw = srcTS->lw; flag = 1;
        }
        if (dstTS->up < srcTS->up) {
            dstTS->up = srcTS->up; flag = 1;
        }
        srcNode = srcNode->next; dstNode = dstNode->next;
    }//end while
    return flag;
}

/*estimate execution counts of lp given temporal scope*/
extern loop_t **loops;
//estimate the number of executions of inst in loop lpId, restricted by tsSet
int estScopeSize(worklist_p tsSet, int lpId) {
    int         dbg = 0;
    int         exec;
    worklist_p  tsNode;
    ts_p        ts;
    int         scpSize;
    loop_t      *lp;
    exec    = 1;
    lp      = loops[lpId];
    tsNode  = tsSet;
    while (lp && lp->id!=0) {
        while (tsNode) {
            ts = (ts_p)(tsNode->data);
            if (lp->id > ts->loop_id) tsNode = tsNode->next;
            else break;
        }
        if (tsNode) {
            ts = (ts_p)(tsNode->data);
            if (lp->id == ts->loop_id) {
                scpSize = (ts->up - ts->lw +1);
                if (0) fprintf(dbgAddr," L%d,|%d,%d|=%d",
                        ts->loop_id,ts->lw,ts->up,scpSize);
                exec *= scpSize;
                tsNode = tsNode->next;
            }
            else goto NO_TS_DEFINED;
        }
        else {//lp->id < ts->loop_id
            NO_TS_DEFINED:
            exec *= lp->bound - 1;
        }
        lp = lp->parent;
    }
    if (dbg) {
        fprintf(stdout,"\n Loop L%d exec: %d ",lpId, exec);
        printTSset(stdout,tsSet);
    }
    return exec;
}
int estScopeSizeOld(worklist_p tsSet, int lpId) {
    int         dbg = 1;
    int         exec;
    worklist_p  tsNode;
    ts_p        ts;
    int         scpSize;
    loop_t      *lp;
    exec = 1;
    lp      = loops[lpId];
    tsNode  = tsSet;
    for (tsNode = tsSet; tsNode; tsNode = tsNode->next) {
        ts = (ts_p)(tsNode->data);
        scpSize = (ts->up - ts->lw +1);
        if (dbg) fprintf(dbgAddr," L%d,|%d,%d|=%d",
                    ts->loop_id,ts->lw,ts->up,scpSize);
        exec *= scpSize;
        if (cmpLpOrder(ts->loop_id, lpId)==0) return exec;
    }
    printf("\nPanic: L%d not in tsSet ",lpId);printTSset(stdout,tsSet);
    exit(1);
    return exec;
}

/*** SCOPED MEMORY BLOCK FUNCTIONS ***/
void printSAddrSet(FILE *fp, worklist_p addrSet, int full) {
    saddr_p      memblk;
    worklist_p  addrNode;
    for (addrNode = addrSet; addrNode; addrNode = addrNode->next) {
        memblk = (saddr_p)(addrNode->data);
        printSAddr(fp,memblk,full);
    }
}
void printSAddr(FILE *fp, saddr_p memblk, int full) {
    //int full = 1;
    worklist_p  cur;
    ts_p        ts;
    fprintf(fp, " <%x:%x:%d", memblk->blkAddr,memblk->instAddr,memblk->flag);
    if (full) printTSset(fp,memblk->tsList);
    fprintf(fp, ">");
}
void cpySAddr (saddr_p dst, saddr_p src) {
    worklist_p  srcNode, dstNode, curNode, prvNode;
    ts_p        ts0, ts1;

    dst->blkAddr = src->blkAddr; 

    dstNode = dst->tsList; prvNode = NULL;
    for (srcNode = src->tsList; srcNode; srcNode = srcNode->next) {
        ts0 = (ts_p) (srcNode->data);
        if (dstNode) {
            ts1 = (ts_p)(dstNode->data);
            cpyTS(ts1,ts0);
            prvNode = dstNode;
            dstNode = dstNode->next;
        }
        else {//dstTS = NULL
            ts1 = malloc(sizeof(ts_s));
            cpyTS(ts1,ts0);
            addAfterNode(ts1, &prvNode, &(dst->tsList));
        }
    }
}
/*create a new mem scope with a given blkAddr and access scope*/
int totalBlk;
saddr_p createSAddr(int blkAddr, int instAddr, int flag, worklist_p orgTSset) {
    int dbg = 0;
    saddr_p      newBlk;
    worklist_p  blkNode, lpNode, tmpNode, prvNode;
    ts_p        blkTS,lpTS;

    newBlk              = (saddr_p) malloc(sizeof(saddr_s));      
    newBlk->blkAddr     = blkAddr;
    newBlk->instAddr    = instAddr;
    newBlk->flag        = flag;
    newBlk->tsList      = NULL;
    //newBlk->ys          = NULL;
    blkNode = NULL;
    for (lpNode = orgTSset; lpNode; lpNode = lpNode->next) {
        lpTS    = (ts_p)(lpNode->data);
        blkTS   = (ts_p) malloc(sizeof(ts_s));
        cpyTS(blkTS, lpTS);
        addAfterNode(blkTS, &blkNode, &(newBlk->tsList));
    }
    totalBlk++;
    if (dbg) {
        fprintf(dbgAddr,"\nCreate accAddr %x: instAddr %x",blkAddr,instAddr);
        //fprintf(dbgAddr,"\norgAS: ");printAccScpSet(dbgAddr,orgASset);
        fprintf(dbgAddr,"\ngenTS: ");printTSset(dbgAddr,newBlk->tsList);
        fflush(dbgAddr);
    }
    return newBlk;
}
/*find SAddrNode with blkAddr = addr in a SAddrSet
    return memNode if found
    otherwise return prvNode where prv->blkAddr < addr < next->blkAddr*/
worklist_p  findBlkAddr(int blkAddr, worklist_p SAddrSet) {
    int dbg = 0;
    worklist_p  curNode,prvNode;
    saddr_p      curBlk, prvBlk;
    if (dbg) {
        fprintf(dbgAddr,"\nSearch %x in ",blkAddr);
        for (curNode = SAddrSet; curNode; curNode=curNode->next) {
            curBlk = (saddr_p)(curNode->data);
            fprintf(dbgAddr," %x",curBlk->blkAddr);
        }
    }
    //let's just make our life simple, search sequentially
    prvNode = NULL;
    for (curNode = SAddrSet; curNode; curNode=curNode->next) {
        curBlk = (saddr_p)(curNode->data);
        if (curBlk->blkAddr == blkAddr) return curNode;
        if (curBlk->blkAddr > blkAddr) {
            //prvBlk->blkAddr < blkAddr < curBlk->blkAddr: not found
            if (dbg) fprintf(dbgAddr,"\nNF, return %x",prvBlk->blkAddr);
            return prvNode;
        }
        prvNode = curNode;prvBlk = curBlk;
    }
    if (dbg) fprintf(dbgAddr,"\nNot found: %x",blkAddr);
    return prvNode;
}
void rearrangeTS(saddr_p smem) {
    worklist_p  curNode, tmpNode, prvNode, outerNode;
    ts_p        curTS, outerTS, tmpTS;
    ts_s        bufTS;
    int         maxLoopId;

    prvNode = NULL;
    for (curNode = smem->tsList; curNode; curNode=curNode->next) {
        outerNode   = curNode; 
        curTS       = (ts_p)(curNode->data);
        outerTS     = (ts_p)(curNode->data);
        maxLoopId   = outerTS->loop_id;
        for (tmpNode = curNode->next; tmpNode; tmpNode=tmpNode->next) {
            tmpTS = (ts_p)(tmpNode->data);
            if (cmpLpOrder(tmpTS->loop_id, maxLoopId)==1) {
                outerNode = tmpNode;
                maxLoopId = tmpTS->loop_id;
            }
        }
        outerTS = (ts_p)(outerNode->data);

        cpyTS(&bufTS, outerTS);
        cpyTS(outerTS, curTS);
        cpyTS(curTS, &bufTS);
    }
}
/*  compare two scoped addresses, return 
    + 1 if conflict in overlapped scope, 
    + 0 if identical & inner scope (smem1 can be renewed)
    + -1 if no iteraction*/
int cmpSAddr(saddr_p m1, saddr_p m2, int lpId) {
    int tsCmp;
    if (m1->blkAddr==UNKNOWN_ADDR || m2->blkAddr==UNKNOWN_ADDR) return OLAP_TS;
    if ( GET_SET(m1->blkAddr) != GET_SET(m2->blkAddr) ) return SEP_TS;

    //if (m1->blkAddr == m2->blkAddr) return 0;
    tsCmp = cmpTSset(m1->tsList, m2->tsList, lpId);
    if (m1->blkAddr==m2->blkAddr && tsCmp==EQUAL_TS) return EQUAL_TS;
    else if (m1->blkAddr!=m2->blkAddr && tsCmp!=SEP_TS) return OLAP_TS;
    else return SEP_TS;
}
