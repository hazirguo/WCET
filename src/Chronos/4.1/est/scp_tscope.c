#include "scp_address.h"

FILE    *dbgAddr;

extern int USE_DOUBLE_MISS;
extern int USE_SEGMENT_SIZE;
extern prog_t       prog;

extern tcfg_edge_t  **tcfg_edges;
extern int          num_tcfg_edges;

extern tcfg_node_t  **tcfg;
extern int          num_tcfg_nodes; 

extern int          num_tcfg_loops;
extern loop_t       **loop_map;
extern loop_t       **loops;
extern inf_loop_t   *inf_loops;

extern int X, Y, B;

saddr_p      curEnumBlk; 
int         totalBlk;
worklist_p  arraySizeCons;   /*constraint on array size*/

/*get real loop id, such that outer lp alway hts higher id*/
extern loop_t *getIbLoop(inf_node_t *ib);
int getIbLB(inf_node_t *ib) {
    if (ib->loop_id==-1) return 0;
    else return inf_loops[ib->loop_id].bound;
}

int         *iterValue;
static int getLpBound(loop_t *lp) {
    int dbg = 0;
    int lb;
    if (lp->rId==-1) return lp->bound;
    switch (lp->rType) {
        case EQL_LB://bound = iter of lp->rId
            lb = iterValue[lp->rId];
            if (lb=-1) lb = loops[lp->rId]->bound;
            break;
        case INV_LB://bound = maxbound - iter of lp->rId
            lb = lp->rBound - iterValue[lp->rId];
            if (lb=-1) lb = loops[lp->rId]->bound;
            break;
        default:
            lb = lp->bound;
    }
    if (dbg) fprintf(dbgAddr,"\nLoop L%d -from L%d, lb= %d",lp->id,lp->rId,lb);
    return lb;
}
/* enumerating access address of address expression expr */
int         minAddr, maxAddr;
worklist_p  lastNode;
worklist_p  *enumTSset;      //the temporal scope for addrset being enumerated
worklist_p  *enumAddrSet;   //list of enumerated address
static int cachedNode(int blkAddr) {
    saddr_p      smem;
    if (lastNode) {//blkAddr is previously generated addr
        smem = lastNode->data;
        if (smem->blkAddr == GET_MEM(blkAddr)) {
            mergeTSset(smem->tsList, *enumTSset, -1);
            return 1;
        }
    }
    return 0;
}
void enum_regular_address(dat_inst_t* d_inst, expr_p expr, int flag,
                int curEq, worklist_p curTSnode, int curAddr) {
    int         dbg = 0;
    int         i;
    saddr_p      smem;
    ts_p        ts;
    loop_t      *lp;
    int         lb;
    if (curEq < expr->varNum) {
        ts = curTSnode->data;
        lp = loops[expr->value[curEq].val]; 
        while (curTSnode) {
            ts = curTSnode->data;
            if (ts->loop_id == lp->id) break;
            else {
                ts->lw = 0;
                ts->up = max(lp->bound-1,0);
                curTSnode = curTSnode->next;
            }
        }
        lb = getLpBound(lp);
        for (i=0; i<lb; i++) {
            iterValue[lp->id] = i; 
            ts->lw = i;
            ts->up = i;
            enum_regular_address(d_inst, expr, flag, curEq+1, curTSnode->next,
                                    curAddr+expr->coef[curEq]*i);
        }
    }
    else {//record address
        //if (dbg) {fprintf(dbgAddr,"\nGenerated %d",curAddr);}
        if (curAddr < minAddr) minAddr = curAddr;
        if (curAddr > maxAddr) maxAddr = curAddr;
        if (cachedNode(GET_MEM(curAddr))) return;
        lastNode = findBlkAddr(GET_MEM(curAddr), *enumAddrSet); 
        if (lastNode) {
            smem = lastNode->data;
            if (smem->blkAddr == GET_MEM(curAddr)) {
                mergeTSset(smem->tsList, *enumTSset, -1);
            }
            else {
                goto ADD_ADDR;
            }
        }
        else {//add new node after lastNode
ADD_ADDR:
            smem =createSAddr(GET_MEM(curAddr),getAddrD(d_inst),flag,*enumTSset);
            addAfterNode(smem, &lastNode, enumAddrSet);
        }
    }
}
/* analyze data reference with regular access pattern */
int analyze_regular_access(dat_inst_t *d_inst, inf_node_t *ib) {
    int         dbg = 0;
    int         i,j,min, tmp;
    int         lpId, addr, flag;
    insn_t      *insn;
    expr_p      exp;
    reg_t       tmpReg;
    ts_p        ts;
    loop_t      *lp;
    int         lpIter[5];  
    worklist_p  tsList, tsNode, addrSet;

    initReg(&tmpReg);
    exp = &(d_inst->addrExpr);

    //check if it is really regular access (no unknown parameter)
    for (i=0; i<exp->varNum; i++) {
        if (exp->value[i].t==VALUE_PARA || exp->value[i].t==VALUE_UNDEF){
            analyze_unpred_access(d_inst,ib);return;}
    }

    //Sort BIV loopID in ascending order
    for (i=0; i<exp->varNum; i++) {
        min = i;
        for (j=i+1; j<exp->varNum; j++) {
            if (exp->value[j].val <= exp->value[min].val) min = j;
            #if 0
            if (exp->coef[i] = 0 - exp->coef[j]) {
                exp->value[i].val = max(exp->value[i].val,exp->value[j].val);
                exp->coef[i] = absInt(exp->coef[i]);
                exp->coef[j] = 0;
                exp->value[j].val = 999;
            }
            #endif
        }
        if (i==min) continue;//exp->value[i] is already min
        if (exp->value[min].val == exp->value[i].val) {
            //min & i are two biv of the same loop --> merge
            exp->coef[i] += exp->coef[min];
            exp->value[min].val = 999;exp->coef[min] =0;
        }
        else {//swap min & i
            cpyReg(&tmpReg, exp->value[i]);
            cpyReg(&(exp->value[i]), exp->value[min]);
            cpyReg(&(exp->value[min]), tmpReg);
            tmp = exp->coef[i]; exp->coef[i]=exp->coef[min]; exp->coef[min]=tmp;
        }
    }
    //Clear up merged register
    while (exp->varNum>0) {
        i = exp->varNum-1;
        if (exp->value[i].val == 999) exp->varNum--; 
        else break;
    }
    /*To deal with j = i*/
    if (dbg) {fprintf(dbgAddr,"\nSorted expr: ");printExpr(dbgAddr,exp);}

    //create the temporal scope for memory blocks of d_inst
    tsList  = NULL; tsNode = NULL;
    lp      = loops[exp->value[0].val];//inner most loop
    i       = 0;
    while (lp!=NULL) {
        if (0) fprintf(dbgAddr,"\n In loop L%d, lbound %d",lp->id,lp->bound-1);

        ts = (ts_p) malloc(sizeof(ts_s));
        if (lp->id == exp->value[i].val) {
            ts->loop_id = lp->id; ts->lw = 0; ts->up = 0; ts->flag = 0;i++;
        }
        else {
            ts->loop_id = lp->id; ts->lw = 0; ts->up = lp->bound; ts->flag = 0;
        }
        addAfterNode(ts, &tsNode, &tsList);
        //addToWorkList( &(orgTS),memTS); 
        lp = lp->parent;
    } 
    if (dbg) {fprintf(dbgAddr,"\nTemporal scope: ");printTSset(dbgAddr,tsList);}

    //enumerating possible memory blocks 
    addrSet = NULL; lastNode = NULL;
    enumTSset = &tsList;
    enumAddrSet = &addrSet;
    flag = 0;
    for (i=0; i<num_tcfg_loops; i++) iterValue[i]=-1;
    minAddr = exp->k; maxAddr = 0;
    enum_regular_address(d_inst, exp, flag, 0, tsList, exp->k);
    d_inst->addr_set = addrSet;
    if (dbg) {
        fprintf(dbgAddr,"\nGenerated range: [%x, %x], %d elems",
                            minAddr, maxAddr, GET_MEM(maxAddr-minAddr));
        //printSAddrSet(dbgAddr,d_inst->addr_set,1);
    }
}

/***Detect AddrSet & their access scope of affine array access***/
int analyze_half_regular_access(dat_inst_t *d_inst, inf_node_t *ib) {
    //now just treat half-regular access ts unpredictable access
    analyze_unpred_access(d_inst,ib);
}

/*** Detect possible AddrSet of unpred. access and compute access scope ***/
int analyze_unpred_access(dat_inst_t *d_inst, inf_node_t* ib) {
    int dbg = 0;
    /* A[x] -> assume array A is global array
     * AddrSet(A) -> obtained from symbol table
     * Identifying access variable: compute addr.expr, ignoring unknown elem.
     * Addr. Expression A[x] = A[0] + T*4, how reg. expression derive now
     * Not necessary correct in all cases, or more aggressive optimization
     * Work with array index A[x]
     * Not work with pointer value
     */
    int initAddr = -1, addr;
    symbol_i    *gVar, *var;

    int         i;
    loop_t      *loop;
    saddr_p      memblk,curblk; 
    ts_p        memTS;
    worklist_p  tsNode, orgTS, blkNode;
    insn_t      *insn;
    int         foundRange;
    expr_p      exp;

    //create access scope for all possible address
    loop = getIbLoop(ib);
    tsNode = NULL;orgTS = NULL;
    while (loop!=NULL) {
        memTS = malloc(sizeof(ts_s));
        memTS->loop_id = loop->id;
        memTS->lw = 0;
        memTS->up = max(loop->bound-1,0);
        memTS->flag = 0; 
        addAfterNode(memTS, &tsNode, &orgTS);
        //addToWorkList( &orgTS,memTS); 
        loop = loop->parent;
    } 

    curblk      = NULL;
    blkNode     = NULL;
    foundRange  = 0;
    exp = &(d_inst->addrExpr);
    insn = (insn_t*)(d_inst->insn);
    initAddr = exp->k;
    //locate the symbol table segment
    for (i=0; i<prog.num_vars; i++) {
        gVar = &(prog.v_info[i]); 
        if (gVar->addr <= initAddr && initAddr < gVar->addr + gVar->size) {
            foundRange = 1;
            break;
        }
    }
    if (foundRange) {//unpredictable access, but find global var

        /*NOTE: stepSizeTable hts only 89 integer, but segment size = 1024*/
        /*can set this ts some user constraint, hard code for now*/
        if (strcmp(gVar->name,"stepsizeTable")==0) {
            gVar->size = 89*4; /*a kind of user constraint*/
        }
        if (strcmp(gVar->name,"indexTable")==0) {
            gVar->size = 16*4; /*a kind of user constraint*/
        }
        //Addr range of global var. too large --> consider unknown
        if (gVar->size > CACHE_SIZE) goto UNKNOWN_RANGE;

        if (dbg) {
            fprintf(dbgAddr,"\n Global var: %s [%x,%x], array sa: %x, size %d",
            gVar->name, gVar->addr,gVar->addr+gVar->size,initAddr,gVar->size);
            fflush(dbgAddr);
        }
        for (addr = gVar->addr; addr < gVar->addr+gVar->size; addr+=4) {
            if (curblk && GET_MEM(addr)==curblk->blkAddr) continue;
            memblk = createSAddr(GET_MEM(addr),
                    hexValue(insn->addr),0,orgTS);
            addAfterNode(memblk, &blkNode, &(d_inst->addr_set));
            curblk = memblk;
        }
    }
    else {//unpredictable access, unknown address range
        UNKNOWN_RANGE:
        if (dbg) {
            fprintf(dbgAddr,"\nUnknown addr range");fflush(dbgAddr);
        }
        memblk = createSAddr(UNKNOWN_ADDR,hexValue(insn->addr),0,orgTS);
        addAfterNode(memblk,&blkNode,&(d_inst->addr_set));
        return 0;
    }
    return 0;
}

/*** Detect possible AddrSet of scalar access and compute access scope ***/
int analyze_scalar_access(dat_inst_t *d_inst, inf_node_t* ib) {
    int dbg = 0;
    int pos;
    int addr;

    insn_t*     insn;
    loop_t      *loop;
    saddr_p      memblk; 
    ts_p        memTS;
    worklist_p  tsNode,orgTS, blkNode;

    if (d_inst->addrExpr.varNum != 0) {
        printf("\nERR: not scalar access");printDataRef(stdout, d_inst);
    }

    addr = d_inst->addrExpr.k; 

    tsNode = NULL;
    orgTS = NULL;
    loop = getIbLoop(ib);
    while (loop!=NULL) {
        if (dbg) {
            fprintf(dbgAddr,"\n In loop L%d, lbound %d",loop->id,loop->bound-1);
            fflush(dbgAddr);}

        memTS = (ts_p) malloc(sizeof(ts_s));
        memTS->loop_id = loop->id;
        memTS->lw = 0;
        memTS->up = max(loop->bound-1,0);
        memTS->flag = 0;
        memTS->flag |= RENEWABLE; 
        addAfterNode(memTS, &tsNode, &orgTS);
        //addToWorkList( &(orgTS),memTS); 
        loop = loop->parent;
    } 
    blkNode = NULL;
    insn = (insn_t*)(d_inst->insn);
    memblk = createSAddr(GET_MEM(addr),hexValue(insn->addr), 1, orgTS);
    addAfterNode(memblk, &blkNode, &(d_inst->addr_set));
    return 0;
}

void genAccessAddress(dat_inst_t *d_inst, inf_node_t* ib) {
    int dbg = 0;
    int i,j;
    insn_t      *insn;
    de_inst_t   *inst;
    char        *deritree;

    d_inst->addr_set = NULL;
    insn = d_inst->insn;
    if (isStoreInst(insn->op)) {
        if (dbg) {
            fprintf(dbgAddr,"\n Ignore store inst");
            printDataRef(dbgAddr,d_inst);
        }
        return;
    }
    switch (d_inst->addrExpr.varNum) {
        case 0:
            if (dbg) {
                fprintf(dbgAddr,"\nAnalyze scalar access");
                fprintf(dbgAddr," L%d ",getIbLoop(ib)->id);
                printDataRef(dbgAddr,d_inst);
            }
            analyze_scalar_access(d_inst,ib); 
            break;
        default:
            if (dbg) {
                fprintf(dbgAddr,"\nAnalyze regular/unpred access");
                fprintf(dbgAddr," L%d ",getIbLoop(ib)->id);
                printDataRef(dbgAddr,d_inst);
            }
            analyze_regular_access(d_inst,ib); 
            break;
            //printf("\n Panic, unknown type %d",d_inst->addrExpr.t);
            //exit(1);
    }
    if (dbg) {
        fprintf(dbgAddr, "\nGenerated addr ");
        printSAddrSet(dbgAddr,d_inst->addr_set,1);
        fprintf(dbgAddr, "\n");
    }

}


/*NOTE: bad design*/
void readExtraCons(char *bin_fname) {
    int dbg = 0;
    FILE *fcons;
    char fname[256], str[256], token[256];
    int pos;
    int idL1, idL2, k; 
    loop_t *l1, *l2; 

    sprintf(fname,"%s.econ",bin_fname);//extra constraint
    fcons = fopen(fname,"r");
    if (!fcons) return;
    while (fgets(str,256,fcons)!=0) { 
        pos = 0;
        getNextToken(token, str, &pos, " ");
        if (dbg) {
            fprintf(dbgAddr,"Cons Type %s",token);fflush(dbgAddr);
        }
        if (strcmp(token,"eql")==0) {
            //format: eql L1_id L2_id k 
            //relative loop bound: lp bound L1 = lp iteration L2 + k
            sscanf(str+pos,"%d %d %d", &idL1, &idL2, &k);
            l1 = loops[idL1];
            l2 = loops[idL2];
            if (isInner(idL1,idL2) == 0) {
                printf("\nWrong RLB cons: L%d is not inner loop of L%d",
                        l1->id, l2->id);
                exit(1);
            }
            l1->rId = l2->id;
            l1->rBound = k;
            l1->rType = EQL_LB;
            if (dbg) {
                fprintf(dbgAddr,"\nEqual outer interation bound:"); 
                fprintf(dbgAddr," LB L%d <= LI L%d + %d", idL1, idL2, k);
                fflush(dbgAddr);
            }
        }
        else if (strcmp(token,"inv")==0) {
            //format: inv L1_id L2_id k 
            //inverse loop bound: lp bound L1 = k - lp iteration L2
            sscanf(str+pos,"%d %d %d", &idL1, &idL2, &k);
            l1 = loops[idL1];
            l2 = loops[idL2];
            if (isInner(l1->id,l2->id) == 0) {
                printf("\nWrong RLB cons: L%d is not inner loop of L%d",
                        l1->id, l2->id);
                exit(1);
            }
            l1->rId = l2->id;
            l1->rBound = k;
            l1->rType = INV_LB;
            if (dbg) {
                fprintf(dbgAddr,"\nInverse outer interation bound:"); 
                fprintf(dbgAddr," LB L%d <= %d - LI L%d", idL1, k, idL2);
                fflush(dbgAddr);
            }
        }
        else {
            fprintf(dbgAddr,"\nUnknown constraint: %s",str); 
            fflush(dbgAddr);
        }
    }
}
void initMem() {
    dbgAddr = fopen("dbg_addr.dbg","w");
    iterValue = calloc(num_tcfg_loops,sizeof(int));
}
void freeMem() {
    fclose(dbgAddr);
    free(iterValue);
}
void initAll(char *bin_fname) {
    int dbg = 0;
    int i,j,k;
    loop_t          *loop, *preLoop;
    inf_node_t      *ib;
    inf_proc_t      *ip;

    initMem();
    totalBlk = 0;
    curEnumBlk = NULL; 
    for (i=0; i<num_tcfg_loops; i++) {
        loop = loops[i];
        loop->bound = 0;
        loop->rId = -1;
        loop->rType = -1;
        loop->rBound = -1;
    }
    /*set loop bound for all loops*/
    for (i=0; i<prog.num_procs; i++) {
        ip = &(inf_procs[i]);
        for (j=0; j<ip->num_bb; j++) {
            ib = &(ip->inf_cfg[j]);
            loop = getIbLoop(ib);
            if (loop) {
                loop->bound = getIbLB(ib);
                loop->rId = -1;
                loop->rType = -1;
                loop->rBound = -1;
            }
        }
    }
    loops[0]->bound = 1;

    /*read loop iteration constraint*/
    readExtraCons(bin_fname);

    loops[0]->exec = 1;
    /*set absolute max exec count*/
    for (i=num_tcfg_loops-1; i>0; i--) {
        loop = loops[i];
        loop->exec = 1;
        if (loop->rId==-1) {
            if (loop->parent) loop->exec = loop->parent->exec*loop->bound;
        }
        else {
            printf("\n L%d rbIb %d rType %d",loop->id, loop->rId,loop->rType);
            preLoop = loops[loop->rId];
            if (!preLoop) continue;
            loop->exec = 0;
            switch(loop->rType) {
                case EQL_LB:
#if 0
                    for (j = 0; j < preLoop->bound; ) {
                        loop->exec += (j+loop->rBound);
                    }
#endif
                loop->exec = (preLoop->bound*(preLoop->bound-1)/2);
                break;
                case INV_LB:
                    for (j = preLoop->bound; j>=0; j--) {
                        loop->exec +=(loop->rBound - j);
                    }
                break;
                default:
                    printf("\nUnknown relative bound cons type: %d",
                            loop->rType); fflush(stdout);
                    exit(1);
                    break;
            }
            if (preLoop->parent) {
                loop->exec = loop->exec * preLoop->parent->exec;
            }
        }
    }

    if (dbg) {
        for (i=1; i<num_tcfg_loops; i++) {
            loop = loops[i];
            if (loop->rId==-1) {
                fprintf(dbgAddr,"\n Loop L%d lb:%d entry:%d exec:%d",
                        loop->id, loop->bound, loop->parent->exec, loop->exec);
            }
            else {
                preLoop = loops[loop->rId];
                switch(loop->rType) {
                    case EQL_LB:
                    case INV_LB:
                        fprintf(dbgAddr,"\n Loop L%d lb:L%d entry:%d exec:%d",
                                loop->id, preLoop->id, preLoop->exec, loop->exec);
                    break;
                    default:
                    break;
                }

            }
            fflush(dbgAddr);
        }
    }
}
void readSizeCons(char *bin_fname) {
    int dbg = 0;
    FILE *gArray;
    int  i;
    char str[256];
    int  size;
    size_cons_p sCons;

    sprintf(str,"%s.sizeCons",bin_fname);
    gArray = fopen(str,"r");
    while (!feof(gArray)) {
        sCons = malloc(sizeof(size_cons_s));
        fscanf(gArray,"%s %d",sCons->name,&(sCons->size));
        addToWorkList(&arraySizeCons,sCons);
        if (dbg) fprintf(dbgAddr,"\nSize constraint: %s %d",
                sCons->name,sCons->size);
    }
}
void classified_address_analysis(char *bin_fname) {
    int dbg = 0;
    int i,j,k;
    P_Queue         *pQueue;

    tcfg_node_t     *tbb,*tsrc,*tdes;
    cfg_node_t      *bb,*src,*des;
    tcfg_edge_t     *tedge;
    cfg_edge_t      *edge;
    inf_node_t      *ib,*isrc,*ides;
    inf_proc_t      *ip;
    // inf_edge_t      *iedge;

    de_inst_t       *inst;
    dat_inst_t      *d_inst;

    loop_t          *loop, *preLoop;

    FILE *dbgAddr;

    dbgAddr = fopen("dbg_addr.dbg","w");
    printf("\nPerforming address analysis");fflush(stdout);
    setAddrDebugFile(dbgAddr);
    initAll(bin_fname);
    /*generate mem.blk & access scope for each data reference*/
    for (i=0; i<prog.num_procs; i++) {
        ip = &(inf_procs[i]);
        for (j=0; j<ip->num_bb; j++) {
            ib = &(ip->inf_cfg[j]);
            if (dbg) {
                fprintf(dbgAddr,"\nBB (%d,%d) L%d  num:%d  sa:%s",
                        i,j,ib->loop_id, ib->num_insn, ib->insnlist[0].addr);
                fflush(dbgAddr);
            }
            bb = ib->bb;
            for (k = 0; k<bb->num_d_inst; k++) {
                d_inst = (dat_inst_t*)(bb->d_instlist);
                d_inst = d_inst + k;
                genAccessAddress(d_inst,ib);
            }
        }
    }

    if (1) {
        fprintf(dbgAddr,"Created memblk: %d",totalBlk);fflush(dbgAddr);
        printf("Created memblk: %d",totalBlk);fflush(stdout);
    }
    freeMem();
}
