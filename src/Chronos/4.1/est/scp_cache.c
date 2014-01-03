#include "scp_cache.h"

FILE *dbgMpa;

extern int          USE_SEGMENT_SIZE;   /*flag: use segment size for unpred.A*/

extern int	        num_tcfg_loops;
extern loop_t       **loops;      /*array of loops*/
extern loop_t       **loop_s;   /*sping tbid -> loop*/

extern prog_t	    prog;
extern int	        num_tcfg_nodes;
extern tcfg_node_t  **tcfg;
extern int	        num_tcfg_edges;
extern tcfg_edge_t  **tcfg_edges;

extern int          X,Y,B,l1,l2;//X: cache-assoc, Y: # cache-set; B: block-size

scp_acs             *lpOut_acs; //summarized & recomputed acs of inner loops
scp_acs             *lpPersIn;  //lpPersIn[id] = PS blocks of lp->id
worklist_p          *lpReqPS;   //PS.blk needed in loop->id
worklist_p          *prvAcsNode;

int                 lp_coldms[36]; //lp_coldms[id] = number of cold miss in lp id
int                 *visited;   //flag whether this node is visited first time

/*for experiment statistic*/
int totalDataExec, totalPersMiss, totalNPersMiss;

/****** CACHE ANALYSIS *******/

void printCacheSet(FILE*fp, worklist_p set) {
    worklist_p blkNode, confNode;
    sblk_p  blk, confBlk;
    for (blkNode = set; blkNode; blkNode= blkNode->next) {
        blk = (sblk_p)(blkNode->data); 
        fprintf(fp," %x:%d",blk->m->blkAddr,blk->age);
        printTSset(fp,blk->m->tsList);
    }
    fflush(fp);
}

loop_t* bbi_lp(tcfg_node_t *bbi) {
    int dbg = 0;
    loop_t *lp;
    if (dbg) fprintf(dbgMpa,"\nBB %d inf_lp->id %d ",bbi->bb->id, bbi->loop_id);
    if (bbi->loop_id==-1) return loops[0]; //not in loop
    lp = loops[inf_loops[bbi->loop_id].loop_id]; 
    if (dbg) {fprintf(dbgMpa," -> lp->id %d", lp->id);fflush(dbgMpa);}
    return lp;
}
int bbi_lpId(tcfg_node_t *bbi) {
    int lpId = bbi_lp(bbi)->id;
    return lpId;
}

/*add saddr m to set strNode after prvNode, keep strNode an increasing set*/
void    addToIncSet(saddr_p m,worklist_p *prvNode,
                worklist_p *strNode, loop_t *lp) {
    worklist_p  curNode;
    saddr_p    curBlk;

    if ( (*prvNode)==NULL ) curNode = *strNode;
    else curNode = (*prvNode)->next;

    for ( ; curNode; curNode = curNode->next) {
        curBlk = (saddr_p)(curNode->data);
        if (curBlk->blkAddr < m->blkAddr) {
            *prvNode = curNode;
        }
        else if (curBlk->blkAddr == m->blkAddr) {
            if (cmpTSset(curBlk->tsList,m->tsList,lp->id)==0) {
                return;//already added
            }
            else {
                *prvNode = curNode;
            }
        }
        else {//prvBlk->blkAddr < m->blkAddr < curBlk->blkAddr
            addAfterNode(m,prvNode,strNode); 
            return;
        }
    }
    //prvBlk->blkAddr < m->blkAddr; curBlk = NULL
    addAfterNode(m,prvNode,strNode);
}

void    cpySBlk(sblk_p dst, sblk_p src) {
    int i;
    dst->age    = src->age;
    dst->m      = src->m;
    for (i=0; i<src->age-1; i++) {
        dst->ys[i]     = src->ys[i];
    }
    dst->flag   = src->flag;
}
sblk_p  createSBlk(saddr_p addrBlk) {
    sblk_p tmpBlk;
    tmpBlk          = malloc(sizeof(sblk_s));
    tmpBlk->m       = addrBlk;
    tmpBlk->age     = 1;
    tmpBlk->flag    = 0;
    return tmpBlk;
}

int     cmpSBlk(sblk_p sblk1, sblk_p sblk2) {
    if (sblk1->age != sblk2->age) return 1;
    if (sblk1->ys != sblk2->ys) return 1;
    return 0;
}

/*Make dst similar to src*/ 
int     cpyACS(scp_acs dst, scp_acs src) {
    int dbg = 0;
    int i;
    int change;
    worklist_p sNode, dNode, prvNode, tmpNode;
    sblk_p sBlk, dBlk, tmpBlk;
    if (dbg) {
        fprintf(dbgMpa,"\nAcs copy");
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\nS[%d]: ",i);printCacheSet(dbgMpa,src[i]);
            fprintf(dbgMpa,"\nD[%d]: ",i);printCacheSet(dbgMpa,dst[i]);
        }
    }
    change = 0;//no change
    for (i=0; i<MAX_CACHE_SET; i++) {
        sNode = src[i];
        dNode = dst[i]; prvNode = NULL;
        while (sNode) {
            sBlk = (sblk_p)(sNode->data); 
            if (dNode==NULL) {//src still has, dst already short
                //fprintf(dbgMpa," f1");
                dBlk = malloc(sizeof(sblk_s));
                cpySBlk(dBlk,sBlk);
                addAfterNode(dBlk,&prvNode, &(dst[i]));
                sNode = sNode->next;
                continue;
            }
            //dNode != NULL
            dBlk = dNode->data;
            if (sBlk->m->blkAddr > dBlk->m->blkAddr) {
                //skip, cannot add sMem at this position
                prvNode = dNode;
                dNode = dNode->next;
            }
            if (sBlk->m->blkAddr==dBlk->m->blkAddr) {
                if (cmpSBlk(sBlk,dBlk)!=0) {
                    change = 1;
                    cpySBlk(dBlk, sBlk);
                }
                sNode = sNode->next;
                prvNode = dNode;
                dNode = dNode->next;
            }
            else {//prvBlk->m->addr < sBlk->m->blkAddr < dBlk->m->blkAddr
                change = 1;
                tmpBlk = malloc(sizeof(sblk_s)); 
                cpySBlk(tmpBlk, sBlk);
                addAfterNode(tmpBlk, &prvNode, &(dst[i]));
                sNode = sNode->next;
            }
        }
    }
    if (dbg) {
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\n--> c:%d D[%d]: ",change,i);
            printCacheSet(dbgMpa,dst[i]);
        }
    }
    return change;
}

//add scoped address mAcc to younger set of scoped cache block acsBlk
int    addToYS(sblk_p acsBlk, saddr_p mAcc) {
    int i;
    int ysSize;
    if (acsBlk->age == EVICTED) return 0;
    ysSize = acsBlk->age - 1;
    for (i=0; i<ysSize; i++) {
        if (acsBlk->ys[i]->blkAddr==mAcc->blkAddr) return 0;
    }
    acsBlk->ys[ysSize] = mAcc;
    acsBlk->age++;
    return 1;
}
int    clearYS(sblk_p acsBlk) {
    acsBlk->age = 1;
    return 1;
}
/* union younger set from src to dst */
int     unionYS(sblk_p dst, sblk_p src) {
    int     i, j;
    int     ysSizeDst, ysSizeSrc;
    int     changed;

    changed = 0;
    ysSizeSrc = src->age - 1;
    for (i=0; i<ysSizeSrc; i++) {
        if (dst->age == EVICTED) return changed;
        changed |= addToYS(dst,src->ys[i]);
    }
    return changed;   
}

//flush all cache blks aged by unpredictable access
void    flushCache(saddr_p mAcc, scp_acs acs_out) {
    int i;
    sblk_p      acsBlk;
    worklist_p  curNode, prvNode;
    for (i=0; i<MAX_CACHE_SET; i++) {
        for(curNode = acs_out[i]; curNode; curNode = curNode->next) {
            acsBlk = (sblk_p)(curNode->data); 
            acsBlk->age = EVICTED;
        }
    }
}

/* quick way to check if data ref. D accesses a single memory block only
 * We could determine if D renews m in loop L by checking access pattern of D if
 * (i)  D is loop-affine access 
 * (ii) addr stride of D in each iteration of L is less than block size 
 */
int    singletonAccQuick(dat_inst_t*  d_inst, int lpId) {//quick check, not formally proven, same result
    int     i;
    int     stride;
    expr_p  exp;
    int     resideLpId;   //inner most loop

    exp = (expr_p)(&(d_inst->addrExpr));
    
    if (exp->varNum==0) return 1;   //D is scalar access
    if (lpId==0) return 0;          //L_id is the outermost loop
    for (i=0; i<exp->varNum-1; i++) {
        if (exp->value[i].t!=VALUE_CONST) return 0; //D is not loop-affine access
    }

    i = exp->varNum-1; //induction variable of the inner most loop
    if (exp->value[i].t == VALUE_CONST && exp->value[i].val>=lpId) {
        if (exp->coef[i] < SIZE_OF_BLOCK) return 1;
    }
    return 0;
}

/* check if the access in temporal scope m is singleton (one single memory block only, no overlap)*/
int   singletonAcc(worklist_p *addrList, saddr_p tsM, dat_inst_t* d_inst, int lpId) {
    worklist_p  addrNode;
    saddr_p     mAcc;
    int         count, cmp;

    freeList(addrList);  
    for (addrNode = (d_inst->addr_set); addrNode; addrNode = addrNode->next) {
        mAcc = (saddr_p)(addrNode->data);
        cmp = cmpSAddr(tsM, mAcc, lpId); 
        switch (cmp) {
            case EQUAL_TS:
            case SEP_TS:
                break;
            default: //OLAP_TS
                return 0;//not singleton access
                //addToWorkList(addrList, mAcc);
        }
    }
    return 1;//is singleton access
}

/* scope-aware update PS, update acs after accessing d_inst in loop lp*/
void    PS_data_update(scp_acs acs_out, dat_inst_t *d_inst, loop_t *lp) {
    int         dbg = 0;
    int         i,j,s;
    int         cmp, found;
    int         singleton;
    worklist_p  sblkNode,addrNode,olapNode,prvNode,agedNode,addr_set,addrList;
    saddr_p     mAcc;
    sblk_p      acsBlk, olapBlk, tmpBlk;
    worklist_p  Xf[MAX_CACHE_SET];          
    worklist_p  newBlk[MAX_CACHE_SET];
    worklist_p  addPos[MAX_CACHE_SET];

    agedNode = NULL; addrList = NULL;
    for (i=0; i<MAX_CACHE_SET; i++) {
        newBlk[i] = NULL; addPos[i] = NULL; Xf[i] = NULL;
    }

    /*divide addr_set into different cache set (X_{f_i})*/
    addr_set = d_inst->addr_set;
    singleton = singletonAccQuick(d_inst, lp->id);
    for (addrNode = addr_set; addrNode; addrNode = addrNode->next) {
        mAcc = (saddr_p)(addrNode->data);
        if (mAcc->blkAddr == UNKNOWN_ADDR) {
            flushCache(mAcc,acs_out);
            continue;
        }
        s = GET_SET(mAcc->blkAddr);
        addAfterNode(mAcc, &(addPos[s]), &(Xf[s]));
    }
    for (i=0; i<MAX_CACHE_SET; i++) addPos[i]=NULL;

    if (dbg) {fprintf(dbgMpa," canRenew: %d", singleton);}
    if (dbg) {
        #if 0
        fprintf(dbgMpa,"\n\nAddrSet: ");printSAddrSet(dbgMpa,addr_set,0);
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\nXf[%d] ",i);printSAddrSet(dbgMpa,Xf[i],0);
        }
        #endif
    }

    /*add newly accessed scoped blks to younger set if overlap*/
    for (i=0; i<MAX_CACHE_SET; i++) {
        for (addrNode = Xf[i]; addrNode; addrNode=addrNode->next) {
            mAcc = (saddr_p)(addrNode->data);
            found = 0;
            prvNode = NULL;
            for (sblkNode = acs_out[i]; sblkNode; sblkNode=sblkNode->next) {
                acsBlk = (sblk_p)(sblkNode->data); 
                if (acsBlk->m->blkAddr <= mAcc->blkAddr) prvNode = sblkNode;
                if (acsBlk->age==EVICTED && acsBlk->m->blkAddr!=mAcc->blkAddr){
                    //acsBlk is evicted -> no need to age it further
                    continue;
                }
                cmp = cmpSAddr(acsBlk->m,mAcc,lp->id);
                switch (cmp) {
                    case EQUAL_TS:          //acsBlk is identical with mAcc
                        found = 1;
                        //remember the position of accessed addr in the cache
                        addToWorkList(&(newBlk[i]),mAcc);
                        addToWorkList(&(addPos[i]),sblkNode);
                        break;

                    case OLAP_TS:           //acsBlk is overlap with mAcc
                        if (0) {
                            fprintf(dbgMpa,"\nOverlap: ");
                            printSAddr(dbgMpa,acsBlk->m,1);
                            fprintf(dbgMpa," - ");
                            printSAddr(dbgMpa,mAcc,1);
                        }
#if AVOID_ONE_ACCESS_MULTIPLE_AGING 
                        if ( acsBlk->flag == AGED) break;//already aged
#endif
                        addToYS(acsBlk,mAcc);
                        acsBlk->flag = AGED;
                        addToWorkList(&agedNode,acsBlk);
                        break;
                    case SEP_TS:            //separated TS -> no interaction
                        //do nothing
                        break;
                    default:
                        printf("\n Panic, unknown cmp %d",cmp); exit(1);
                }
            }//finish for acs_out[i]
            if (!found) {                   //mAcc is a new scoped block
                addToWorkList(&(newBlk[i]),mAcc);
                addToWorkList(&(addPos[i]),prvNode);
            }
        }//finish X_f_i
        while (!isEmpty(newBlk[i])) {
            mAcc    = removeOneFromWorkList(&(newBlk[i]));
            prvNode = removeOneFromWorkList(&(addPos[i]));
            if (prvNode == NULL) goto ADD_NEW_BLK;
            acsBlk  = prvNode->data;
            if (acsBlk->m->blkAddr==mAcc->blkAddr) {
                cmp = cmpSAddr(acsBlk->m,mAcc,lp->id);
                if (cmp == EQUAL_TS) {
                    if (singletonAcc(&addrList,acsBlk->m,d_inst,lp->id)==1) {//formally proven approach
                        clearYS(acsBlk);
                    }
                    #if 0  
                    if (singleton==1) { //quicker way, not formally proven, same result
                        clearYS(acsBlk);
                    }
                    #endif
                }
                else if (cmp!=EQUAL_TS) {
                    goto ADD_NEW_BLK;
                }
            }
            else {//mAcc is a new scoped block in this acs
                if (acsBlk->m->blkAddr<mAcc->blkAddr) {
                ADD_NEW_BLK:
                    tmpBlk  = createSBlk(mAcc); 
                    addAfterNode(tmpBlk,&prvNode, &(acs_out[i]));
                }
                //else not new scp_blk -> do nothing
            }
        }

    }//finish MAX_CACHE_SET
    
    /*reset aged mark*/
    while (!isEmpty(agedNode)) {
        tmpBlk = removeOneFromWorkList(&agedNode);
        tmpBlk->flag = 0;
    }
    if (dbg) {
        for (i=0; i<1; i++) {
            fprintf(dbgMpa,"\nD[%d] ",i);printCacheSet(dbgMpa,acs_out[i]);
        }
    }
}

/* scope-aware join PS, join src to dst */
int PS_join (scp_acs dst, scp_acs src, loop_t* lp) {
    int         dbg = 0;
    int         i,j;
    int         changed, cmp;
    worklist_p  sNode, dNode, prvNode, tmpNode;
    sblk_p      sBlk, dBlk, tmpBlk;

    if (dbg) {
        //fprintf(dbgMpa,"\nJoin 2 ACS");
        for (i=0; i<1; i++) {
            fprintf(dbgMpa,"\nS[%d] ",i);printCacheSet(dbgMpa,src[i]);
            fprintf(dbgMpa,"\nD[%d] ",i);printCacheSet(dbgMpa,dst[i]);
        }
    }

    changed = 0;
    for (i=0; i<MAX_CACHE_SET; i++) {
        sNode   = src[i];
        dNode   = dst[i];
        prvNode = NULL;
        while (sNode) {
            sBlk = (sblk_p)(sNode->data);
            if (dNode) {
                dBlk = (sblk_p)(dNode->data);
                if (sBlk->m->blkAddr == dBlk->m->blkAddr) {
                    if (eqTSset(sBlk->m->tsList, dBlk->m->tsList, lp->id)) {
                        changed |= unionYS(dBlk,sBlk);
                        prvNode = dNode;
                        sNode   = sNode->next;
                        dNode   = dNode->next;
                    }
                    else goto NEXT_SRC_NODE;        //same addr, diff TS
                }
                else if (dBlk->m->blkAddr < sBlk->m->blkAddr) {
NEXT_SRC_NODE:
                    prvNode = dNode;
                    dNode   = dNode->next;
                }
                else {//prvBlk->blkAddr < sBlk->blkAddr < dBlk->blkAddr
                    goto ADD_NEW_BLK;
                }
            }
            else {//dNode==NULL                     //add new dNode for sNode
ADD_NEW_BLK:
                if (0) {
                    fprintf(dbgMpa,"\nAdd ");
                    printSAddr(dbgMpa,sBlk->m,1);
                    fprintf(dbgMpa," after ");
                    if (prvNode) {
                        tmpBlk = prvNode->data;
                        printSAddr(dbgMpa,tmpBlk->m,1);
                    }
                    else {
                        fprintf(dbgMpa," D[%d] head",i);
                    }
                }
                tmpBlk  = malloc(sizeof(sblk_s));
                cpySBlk(tmpBlk, sBlk);
                addAfterNode(tmpBlk, &prvNode, &(dst[i]));
                sNode   = sNode->next;
                changed = 1;
            }
        }//end while sNode
    }//end for cache_set

    if (dbg && changed) {
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\nS[%d] ",i);printCacheSet(dbgMpa,src[i]);
            fprintf(dbgMpa,"\n-->D[%d] ",i);printCacheSet(dbgMpa,dst[i]);
        }
    }
    return changed;
}

int     absInt(int x) {
    if (x>=0) return x;
    else return 0-x;
}

/*** Pers. analysis within a basic block ***/
void transform_bbi_dcache(tcfg_node_t *bbi, loop_t* lp,int type) {
    int dbg = 0;
    dat_inst_t*     d_inst;
    insn_t*         insn;
    int             n_inst;
    int             renewFlag;
    
    cpyACS(bbi->acs_out, bbi->acs_in); 
    for (n_inst = 0; n_inst < bbi->bb->num_d_inst; n_inst++) {
        d_inst  = (dat_inst_t*)(bbi->bb->d_instlist);
        d_inst  = d_inst + n_inst;
        insn    = d_inst->insn;

        if (!isMemAccess(insn->op)) {
            printf("\nErr: not ld/st inst: ");
            printInstr(insn);
            continue;
        }
        if (type != PERSISTENCE) {
            printf("\nErr: not implemented");fflush(stdout);
            exit(1);
        }
        if (dbg) {
            fprintf(dbgMpa,"\n\nDataRef ");fprintInstr(dbgMpa,insn);
        }

#if WRITE_THRU 
        if (isLoadInst(insn->op)) {
            PS_data_update(bbi->acs_out,d_inst, lp); 
        }
        else {//write-through no allocate policy
            if (dbg) {
                fprintf(dbgMpa,"write inst, do nothing");
            }
            //write inst -> do nothing
            continue;
        }
#else
        PS_update(bbi->acs_out,d_inst, renewFlag, lp); 
#endif
    }
}

/*** Analyze ps within a loop, according to mpa algorithm ***/
void analyze_loop_ps(loop_t *lp) {
    int         dbg = 0;
    int         i,j;
    int         bbid, hdLpId, blkLpId;
    tcfg_node_t *lpHead, *lpTail, *curNode; 
    tcfg_edge_t *in_edge, *out_edge, *tmp_edge;
    loop_t      *blk_lp;
    int         changed,flag;
    int         iterCount;

    P_Queue *pQueue;    //priority queue of node to process


    if (dbg){fprintf(dbgMpa,"\n\n=== Analyze L%d ",lp->id);fflush(dbgMpa);}

    /*check if this lp has been analyzed*/
    if (lp->flags == LOOP_ANALYZED){fprintf(dbgMpa," : lp analyzed");return;}
    else lp->flags = LOOP_ANALYZED; //mark analyzed

    hdLpId = lp->id;

    pQueue = NULL;
    lpHead = lp->head;
    lpTail = lp->tail;  /*NOTE: Vivy assume lp has 1 lpTail -> true?*/
    if (lpTail==NULL) lpTail = tcfg[num_tcfg_nodes-1];
    if (dbg) {fprintf(dbgMpa," [%d,%d]",lpHead->id, lpTail->id);fflush(dbgMpa);}

    /*Reinitialize ACS before analyzing this loop*/
    for (i=lpHead->id; i<=lpTail->id; i++) {
        curNode = tcfg[i];
        visited[i] = 0;
        for (j=0; j<MAX_CACHE_SET; j++) {
            curNode->acs_in[j] = NULL;
            curNode->acs_out[j] = NULL;
        }
    }
    
    iterCount = 0;
    p_enqueue(&pQueue,lpHead,lpHead->id);
    while (!p_queue_empty(&pQueue)) {
        curNode = (tcfg_node_t*) p_dequeue(&pQueue);
        if (curNode==lpHead) iterCount++;
        blk_lp = bbi_lp(curNode);
        blkLpId = bbi_lpId(curNode);

        /*ignore blks belong to outer lp*/
        if (cmpLpOrder(hdLpId,blkLpId)==-1) continue;

        if (dbg) {
            fprintf(dbgMpa,"\n\nAnalyze bbi (%d,%d), L%d",
                    bbi_pid(curNode),bbi_bid(curNode),blkLpId);fflush(dbgMpa);
        }
        // merge ACS of incoming edges
        changed = 0;
        for (in_edge=curNode->in; in_edge!=NULL; in_edge=in_edge->next_in) {
            if (cmpLpOrder(hdLpId,bbi_lpId(in_edge->src))>=0) {
                flag = PS_join(curNode->acs_in, in_edge->src->acs_out, lp);
                if (dbg) {
                    fprintf(dbgMpa,"\nJoin (%d->%d) : changed %d",
                            in_edge->src->id,curNode->id, flag); 
                }
                if (flag==1) changed=1;
            }
        }
        if (visited[curNode->id]==0) {changed=1; visited[curNode->id]=1;}

        if (changed) {
            // perform abs.int within the block
            transform_bbi_dcache(curNode,lp,PERSISTENCE);
            //enqueue outgoing bbi
            for (out_edge=curNode->out; out_edge; out_edge=out_edge->next_out){
                p_enqueue(&pQueue,out_edge->dst, out_edge->dst->id); 
            }
        }
    }//finish analyze this lp
    if (1) {printf("\nFinish analysis L%d in %d rounds",lp->id,iterCount);}
}

/****** COMPUTE CACHE MISSES FROM ANALYSIS RESULT ******/

/* collect scoped blks persistence entering lp */
void getOuterPS(loop_t *lp) {
    /* For each loop, collect PS.blk when entering the loop -> lpPersIn
    * Collect PS.blk needed inside the loop -> lpReqPS
    * if m in lpReqPS & m notin lpPersIn -> m need 1 cold miss each entering lp*/
    int dbg = 0;
    int i;
    tcfg_node_t     *head, *src;
    tcfg_edge_t     *in_e, *cur_e;
    worklist_p      setNode,lpNode,prvNode;
    sblk_p          setBlk,lpBlk;
    scp_acs         lpIn;
    loop_t          *tmpLp, *parLp;
    
    head = lp->head;                        //structured loop -> 1 lp head
    lpIn = lpPersIn[lp->id];                //persistence blks entering loop lp
    if (dbg) fprintf(dbgMpa,"\nPers.blk when entering L%d",lp->id);

    for (in_e = head->in; in_e; in_e = in_e->next_in) {
       src = in_e->src; 
       if (bbi_lp(src)->id==lp->id) continue;
       for (i=0; i<MAX_CACHE_SET; i++) {
            prvNode = NULL;
            for (setNode = src->acs_out[i]; setNode; setNode = setNode->next) {
                setBlk = (sblk_p)(setNode->data);
                if (setBlk->age < PSEUDO) {//PS
                    addAfterNode(setBlk,&prvNode,&(lpIn[i])); 
                }
            }
       }
       if (dbg) {
            fprintf(dbgMpa,"\nEnter L%d from (%d,%d)",
                lp->id,bbi_pid(src),bbi_bid(src));
            for (i=0; i<MAX_CACHE_SET; i++) {
                fprintf(dbgMpa,"\nS[%d] ",i);
                printCacheSet(dbgMpa,src->acs_out[i]);
                fprintf(dbgMpa,"\nList of PS blks ");
                fprintf(dbgMpa,"\n->S[%d] ",i);printCacheSet(dbgMpa,lpIn[i]);
            }
       }
    }
}

/* estimate cold miss of persistent blks each time entering lp*/
void estLpMissPS(loop_t *lp) {
    int         dbg = 0;
    int         i, s;
    worklist_p  inNode, reqNode, prvNode;
    saddr_p     reqBlk;
    sblk_p      inBlk;
    worklist_p  *prvInNode;
    worklist_p  prvReqNode;
    loop_t      *parLp;
    int         found, lpMiss;
    int         lpEntry;
    int         accessNum;

    if (dbg) {
        fprintf(dbgMpa,"\n\nEst cold miss in L%d",lp->id);
        fprintf(dbgMpa,"\n LPS set:");
        printSAddrSet(dbgMpa,lpReqPS[lp->id],0);
        fprintf(dbgMpa,"\n lpPS_in set");
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\n S[%d]: ",i);
            printCacheSet(dbgMpa,lpPersIn[lp->id][i]);
        }
        fprintf(dbgMpa,"\n Miss est: ");
    }
    prvReqNode = NULL;
    prvInNode = calloc(MAX_CACHE_SET,sizeof(worklist_p));

    lp_coldms[lp->id] = 0; 
    for (reqNode = lpReqPS[lp->id]; reqNode; reqNode = reqNode->next) {
        reqBlk = (saddr_p)(reqNode->data);     
        //fprintf(dbgMpa," R:%x ",reqBlk->blkAddr);
        found = 0;

        s = GET_SET(reqBlk->blkAddr);
        prvNode = prvInNode[s];
        if (prvNode) inNode = prvNode->next;
        else inNode = lpPersIn[lp->id][s];

        for (  ; inNode; inNode = inNode->next ) {
            inBlk = (sblk_p)(inNode->data);
            if (inBlk->m->blkAddr < reqBlk->blkAddr) {
                prvNode = inNode; 
            }
            else if (inBlk->m->blkAddr == reqBlk->blkAddr && lp->parent) {
                if (cmpSAddr(inBlk->m, reqBlk,lp->id)==0) {
                    //regBlk PS in lp InACS -> no cold miss needed
                    found = 1;
                addToIncSet(reqBlk,&prvReqNode,&(lpReqPS[lp->parent->id]),lp);
                    if (dbg) {fprintf(dbgMpa," U:%x ",reqBlk->blkAddr);}
                    break; 
                }
                else {
                    prvNode = inNode;
                }
            }
            else {//prvBlk->blkAddr < reqBlk->blkAddr < inBlk->blkAddr
                //regBlk not PS in lp InACS -> need 1 cold ms each enter
                found = 1;
                if (lp->parent) 
                    lpMiss=estScopeSize(reqBlk->tsList,lp->parent->id); 
                else lpMiss = 1;
                lp_coldms[lp->id]+=lpMiss;
                if (dbg) {fprintf(dbgMpa," C:%x:%d ",reqBlk->blkAddr, lpMiss);}
                break;
            }
        }
        if (!found) {
            if (lp->parent) lpMiss=estScopeSize(reqBlk->tsList,lp->parent->id); 
            else lpMiss = 1;
            lp_coldms[lp->id]+=lpMiss;
            if (dbg) {fprintf(dbgMpa," C:%x:%d ",reqBlk->blkAddr,lpMiss);}
        }
        prvInNode[s] = prvNode;
    }
    free(prvInNode);

    if (lp->parent) lpEntry = lp->parent->exec;
    else lpEntry = 1;

    totalPersMiss += lp_coldms[lp->id];

    if (dbg) {
        fprintf(dbgMpa,"\nCold miss in L%d: %d",lp->id, lp_coldms[lp->id]);}
    if (1) {
        printf("\nIn L%d, Cold miss %d, Entry %d, Exec %d",
                lp->id,lp_coldms[lp->id],lpEntry, lp->exec);
        fflush(stdout);
    }
}

void estNodeAllMiss(tcfg_node_t* bbi) {
    int dbg = 0;
    dat_inst_t*     d_inst;
    insn_t*         insn;
    int             n_inst,cs, flag;
    int             num_PS, max_miss, max_exec, count2x; 
    
    worklist_p      confScp, PS_set, miss_set, LPS_req;
    worklist_p      addrNode, setNode, csNode, prvReqNode, prvNode;
    saddr_p         addrBlk;
    sblk_p          setBlk;

    loop_t          *lp, *tmpLp, *parLp;

    bbi->max_miss = 0;
    bbi->dcache_delay = 0;
    lp = bbi_lp(bbi);
    max_exec = lp->exec;
    cpyACS(bbi->acs_out, bbi->acs_in); 
    if (dbg) {
        fprintf(dbgMpa,"\n\nEst non-PS miss in bbi (%d,%d), L%d, ME: %d",
                bbi_pid(bbi),bbi_bid(bbi),bbi_lp(bbi)->id, max_exec);
    }
    for (n_inst = 0; n_inst < bbi->bb->num_d_inst; n_inst++) {
        d_inst = (dat_inst_t*)(bbi->bb->d_instlist);
        d_inst = d_inst + n_inst;
        insn = d_inst->insn;
        max_miss = 0; num_PS = 0; 
        prvNode = NULL; LPS_req = NULL;
        if (dbg){miss_set = NULL; PS_set = NULL;}
        d_inst->max_exec = max_exec;
        totalDataExec +=max_exec;
        #if WRITE_THRU
        if (isStoreInst(insn->op)) {
            d_inst->max_miss = max_exec; 
            totalNPersMiss += d_inst->max_miss;
            if (1) {
                printf("\n\nDataRef: ");printInstr(d_inst->insn);
                printf("maxPS: %d, maxMs: %d, maxExec: %d",
                        num_PS, d_inst->max_miss, max_exec);
                fflush(stdout);
            }
            continue;
        }
        #endif 

        for (addrNode = d_inst->addr_set; addrNode; addrNode = addrNode->next) {
            addrBlk = (saddr_p)(addrNode->data);
            if (addrBlk->blkAddr==UNKNOWN_ADDR) goto ALL_MISS;
            cs = GET_SET(addrBlk->blkAddr);
            if (cs<0 || cs >= MAX_CACHE_SET) {
                printf("\nPanic: unknown cache set %d",cs);
                exit(1);
            }
            for (setNode=bbi->acs_out[cs]; setNode; setNode = setNode->next) {
                setBlk = (sblk_p)(setNode->data);
                if (addrBlk->blkAddr > setBlk->m->blkAddr) continue;
                if (addrBlk->blkAddr == setBlk->m->blkAddr) {
                    if (cmpSAddr(addrBlk,setBlk->m,lp->id)==0) {
                        if (setBlk->age==EVICTED) {//non-PS
                            //max_miss+=estConfScope(setBlk->m,bbi->acs_out[cs],lp);
                            max_miss+=estScopeSize(setBlk->m->tsList,lp->id);
                            if (dbg) addToWorkList(&miss_set, addrBlk);
                        }
                        else {//PS
                            addAfterNode(addrBlk,&prvNode,&LPS_req);
                            num_PS++; 
                        }
                        break;
                    }
                    else continue;
                }
                else {//prvBlk->blkAddr < addrBlk->blkAddr < setBlk->blkAddr
                    //not yet loaded to acs -> PS
                    addAfterNode(addrBlk,&prvNode, &LPS_req);
                    num_PS++; 
                    break;
                }
            }
        }

        if (max_miss  < max_exec) {
            d_inst->max_miss = max_miss;
            prvReqNode = NULL;
            for (setNode = LPS_req; setNode; setNode=setNode->next) {
                addrBlk = (saddr_p)(setNode->data); 
                addToIncSet(addrBlk,&prvReqNode,&(lpReqPS[lp->id]),lp);
                if (dbg) addToWorkList(&PS_set, addrBlk);
            }
            while(!isEmpty(LPS_req)) removeOneFromWorkList(&LPS_req);
        }
        else {//just all miss 
            ALL_MISS:
            max_miss = max_exec;
            d_inst->max_miss = max_miss;
        }

        bbi->max_miss += d_inst->max_miss;
        totalNPersMiss += d_inst->max_miss;

        if (dbg) {
            fprintf(dbgMpa,"\n\nDataRef: ");
            fprintInstr(dbgMpa,d_inst->insn);
            fprintf(dbgMpa,"\n#PS: %d, max_miss: %d, max_exec: %d",
                        num_PS, d_inst->max_miss, max_exec);
            fprintf(dbgMpa,"\nList of miss.blk :");
            printSAddrSet(dbgMpa,miss_set,0);
            #if 0
            fprintf(dbgMpa,"\nList of PS.req :");
            printSAddrSet(dbgMpa,PS_set,0);
            #endif
            while(!isEmpty(PS_set)) removeOneFromWorkList(&PS_set);
            while(!isEmpty(miss_set)) removeOneFromWorkList(&miss_set);
        }

        
        if (1) {
            printf("\n\nDataRef: ");printInstr(d_inst->insn);
            printf("\nmaxPS: %d, maxMs: %d, maxExec: %d",
                        num_PS, d_inst->max_miss, max_exec);
            fflush(stdout);
        }
        PS_data_update(bbi->acs_out,d_inst, lp); 
    }
}

/****** OTHER ROUTINES *******/

void init_mem() {
    int i,j;
    dbgMpa = fopen("dbg_mpa.dbg","w");
    visited = calloc(num_tcfg_nodes,sizeof(int));
    for (i=0; i<num_tcfg_nodes; i++) {
        tcfg[i]->acs_in = calloc(MAX_CACHE_SET,sizeof(worklist_p));
        tcfg[i]->acs_out = calloc(MAX_CACHE_SET,sizeof(worklist_p));
        for (j=0; j<MAX_CACHE_SET; j++) {
            tcfg[i]->acs_in[j] = NULL;
            tcfg[i]->acs_out[j] = NULL;
        }
    }
    lpOut_acs = calloc(num_tcfg_loops,sizeof(worklist_p*));
    lpPersIn = calloc(num_tcfg_loops,sizeof(worklist_p*));
    lpReqPS = calloc(num_tcfg_loops,sizeof(worklist_p));
    for (i=0; i<num_tcfg_loops; i++) {
        lpReqPS[i] = NULL;
        lpOut_acs[i] = calloc(MAX_CACHE_SET,sizeof(worklist_p));
        lpPersIn[i] = calloc(MAX_CACHE_SET,sizeof(worklist_p));
    }
    prvAcsNode = calloc(MAX_CACHE_SET,sizeof(worklist_p));
}
void free_mem() {
    int i,j;
    for (i=0; i<num_tcfg_nodes; i++) {
        free(tcfg[i]->acs_in);
        free(tcfg[i]->acs_out);
    }
    for (i=0; i<num_tcfg_loops; i++) {
        free(lpOut_acs[i]);
        free(lpPersIn[i]);
    }
    free(prvAcsNode);
    free(lpOut_acs);
    free(lpPersIn);
    free(lpReqPS);
    free(visited);
    fclose(dbgMpa);
}
/***  Multi-level PSistence analysis of data cache, general handle ***/
void mpa_datacache() {
    int dbg=0;
    int i,j;
    inf_node_t *ib;
    tcfg_node_t *bbi;

    init_mem();
    for (i=0; i<num_tcfg_nodes; i++) {
        bbi = tcfg[i];
        ib = &(inf_procs[bbi_pid(bbi)].inf_cfg[bbi_bid(bbi)]);
        bbi->loop_id = ib->loop_id;
    }

    /*PS analysis from outer-most loop to inner loop*/
    analyze_loop_ps(loops[0]);    
    for (i=num_tcfg_loops-1; i>0; i--) analyze_loop_ps(loops[i]);

    #if 1
    /*collect PS blk of incoming edge of each lp*/
    for (i=0; i<num_tcfg_loops; i++) getOuterPS(loops[i]);      
    
    /*deriving cache miss for each data reference*/
    totalDataExec = 0;
    totalPersMiss = 0;
    totalNPersMiss = 0;
    for (i=0; i<num_tcfg_nodes; i++) estNodeAllMiss(tcfg[i]);
    /*eliminate duplicated cold miss of identical memScp*/
    for (i=1; i<num_tcfg_loops; i++) estLpMissPS(loops[i]);      
    estLpMissPS(loops[0]);      

    if (1) {
        printf("\nTotal data ref %d",totalDataExec);
        printf("\nTotal PS. miss %d",totalPersMiss);
        printf("\nTotal non-PS. miss %d",totalNPersMiss);
    }
    #endif
    
    free_mem();
}
