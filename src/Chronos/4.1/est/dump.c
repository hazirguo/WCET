
/*** Estimating cache miss ***/
/* MaxMiss(D)
   MaxMiss(L)
*/

/* Collect the PS blks when entering loop lp  */
void collectOuterPS(loop_t *lp) {
    /* For each loop, collect PS.blk when entering the loop -> lpPersIn
    * Collect PS.blk needed inside the loop -> lpReqPS
    * if m in lpReqPS & m notin lpPersIn -> m need 1 cold miss each entering lp*/
    int dbg = 0;
    int i;
    tcfg_node_t *head, *src;
    tcfg_edge_t *in_e, *cur_e;
    worklist_p  setNode,lpNode,prvNode;
    sblk_p   setBlk,lpBlk;
    mpa_acs     lpIn;
    loop_t      *tmpLp, *parLp;
    
    head = lp->head;//structured loop has only 1 head
    lpIn= lpPersIn[lp->id];
    if (dbg) fprintf(dbgMpa,"\nPers.blk when entering L%d",lp->id);

    for (in_e = head->in; in_e; in_e = in_e->next_in) {
       src = in_e->src; 
       if (bbi_lp(src)->id==lp->id) continue;
       if (dbg) {
           fprintf(dbgMpa,"\nEnter L%d from (%d,%d)",
                        lp->id,bbi_pid(src),bbi_bid(src));
       }
       for (i=0; i<MAX_CACHE_SET; i++) {
            prvNode = NULL;
            if (dbg) {
                fprintf(dbgMpa,"\nS[%d] ",i);
                printCacheSet(dbgMpa,src->acs_out[i]);
            }
            for (setNode = src->acs_out[i]; setNode; setNode = setNode->next) {
                setBlk = (sblk_p)(setNode->data);
                if (setBlk->age < PSEUDO) {//PS
                    addAfterNode(setBlk,&prvNode,&(lpIn[i])); 
                }
            }
            if (dbg) {
                fprintf(dbgMpa,"\n->S[%d] ",i);printCacheSet(dbgMpa,lpIn[i]);
            }
       }
    }
}

/***  Estimating cache miss due to access non-PS blks ***
 * Scan each tcfg -> check each blk m of data reference D
 * m not PS -> D all misses in conflict.scope of m
 * m PS -> add m to lpReqPS, list of PS.blk requested in loop
 */
void estimateNodeAllMiss(tcfg_node_t* bbi) {
    int dbg = 1;
    dat_inst_t*     d_inst;
    insn_t*         insn;
    int             n_inst,cs;
    int             num_PS, max_miss, max_exec, count2x; 
    
    worklist_p      confScp, PS_set, miss_set, LPS_req;
    worklist_p      addrNode, setNode, csNode, prvReqNode, prvNode;
    saddr_p        addrBlk;
    sblk_p       setBlk;

    loop_t          *lp, *tmpLp, *parLp;

    bbi->max_miss = 0;
    bbi->dcache_delay = 0;
    lp = bbi_lp(bbi);
    max_exec = lp->exec;
    join2acs(bbi->acs_in, bbi->acs_out); 
    if (dbg) {
        fprintf(dbgMpa,"\n\nEst induced miss in bbi (%d,%d), L%d, ME: %d",
                bbi_pid(bbi),bbi_bid(bbi),bbi_lp(bbi)->id, max_exec);
    }
    for (n_inst = 0; n_inst < bbi->bb->num_d_inst; n_inst++) {
        d_inst = &(bbi->bb->d_instlist[n_inst]);
        insn = d_inst->insn;
        max_miss = 0; num_PS = 0; 
        prvNode = NULL; LPS_req = NULL;
        if (dbg){miss_set = NULL; PS_set = NULL;}
        d_inst->max_exec = max_exec;
        totalDataExec +=max_exec;
        #if 0
        if (isStoreInst(isa[d_inst->inst->op_enum].name)) {
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
            cs = GET_SET(addrBlk->blkAddr);
            for (setNode=bbi->acs_out[cs]; setNode; setNode = setNode->next) {
                setBlk = (sblk_p)(setNode->data);
                if (addrBlk->blkAddr > setBlk->m->blkAddr) continue;
                if (addrBlk->blkAddr == setBlk->m->blkAddr) {
                    if (compareMemScp(addrBlk,setBlk->m,lp)==0) {
                        if (setBlk->age==PSEUDO) {//non-PS
                            //max_miss+=estConfScope(setBlk->m,bbi->acs_out[cs],lp);
                            max_miss+=estScopeSize(setBlk->m->accScp,lp);
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
            fprintf(dbgMpa,"\nList of PS.req :");
            printMemScpSet(dbgMpa,PS_set,0);
            while(!isEmpty(PS_set)) removeOneFromWorkList(&PS_set);
            fprintf(dbgMpa,"\nList of miss.blk :");
            printMemScpSet(dbgMpa,miss_set,0);
            while(!isEmpty(miss_set)) removeOneFromWorkList(&miss_set);
        }

        
        if (1) {
            printf("\n\nDataRef: ");printInstr(d_inst->insn);
            printf("maxPS: %d, maxMs: %d, maxExec: %d",
                        num_PS, d_inst->max_miss, max_exec);
            fflush(stdout);
        }
        PS_update(bbi->acs_out,d_inst->addr_set,lp); 
    }
}

/*Estimate cold miss of PS blks each time entering a loop*/
/*Must visit lp in increasing id order, so inner loop is visited first*/
void estLpMissPS(loop_t *lp) {
    int dbg = 0;
    int i, s;
    worklist_p  inNode, reqNode, prvNode;
    saddr_p    reqBlk;
    sblk_p   inBlk;
    worklist_p  *prvInNode;
    worklist_p  prvReqNode;
    loop_t      *parLp;
    int found, lpMiss;
    int lpEntry;
    int accessNum;

    if (dbg) {
        fprintf(dbgMpa,"\n\nEst cold miss in L%d",lp->id);
        fprintf(dbgMpa,"\n LPS set:");
        printMemScpSet(dbgMpa,lpReqPS[lp->id],0);
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
                if (compareMemScp(inBlk->m, reqBlk,lp)==0) {
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
                if (lp->parent) lpMiss=estScopeSize(reqBlk->accScp,lp->parent); 
                else lpMiss = 1;
                lp_coldms[lp->id]+=lpMiss;
                if (dbg) {fprintf(dbgMpa," C:%x:%d ",reqBlk->blkAddr, lpMiss);}
                break;
            }
        }
        if (!found) {
            if (lp->parent) lpMiss = estScopeSize(reqBlk->accScp,lp->parent); 
            else lpMiss = 1;
            lp_coldms[lp->id]+=lpMiss;
            if (dbg) {fprintf(dbgMpa," C:%x:%d ",reqBlk->blkAddr,lpMiss);}
        }
        prvInNode[s] = prvNode;
    }
    free(prvInNode);

    if (lp->parent) lpEntry = lp->parent->exec;
    else lpEntry = 1;
    //lp_coldms[lp->id] *=lpEntry;
    //if (lp_coldms[lp->id]>lp_allms[lp->id]) //worse than all miss -> just AM
     //   lp_coldms[lp->id] = lp_allms[lp->id];

    totalPersMiss += lp_coldms[lp->id];

    if (dbg) {
        fprintf(dbgMpa,"\nCold miss in L%d: %d",lp->id, lp_coldms[lp->id]);
    }
    if (1) {
        printf("\nL%d, Cold miss %d, Entry %d, Exec %d",
                lp->id,lp_coldms[lp->id],lpEntry, lp->exec);
        fflush(stdout);
    }
}

int estConfScope(memscp_p m, worklist_p acs, loop_t *curLp) {
    int dbg = 0;
    int totalConf, scpConf, maxLw, minUp;
    worklist_p  setNode, mAsNode, cAsNode;
    accScp_p    mAs,cAs;
    mpablk_ip   setBlk;
    int         lpOrder;
    int         confSize, PSSize;
    fragmented_scope *fragScope;
    totalConf = 0;
    fragScope = calloc(num_tcfg_loops,sizeof(fragmented_scope));
    for (setNode = acs; setNode; setNode=setNode->next) {
        setBlk = (mpablk_ip) (setNode->data);
        if (compareMemScp(setBlk->m, m, curLp)!=1) continue;

        scpConf = 1;
        if (dbg) {
            fprintf(dbgMpa,"\nConfScope: ");
            printMemScp(dbgMpa,m,1);
            printMemScp(dbgMpa,setBlk->m,1);
            fprintf(dbgMpa,"\n--> ");
        }
        mAsNode = m->accScp;
        cAsNode = setBlk->m->accScp;
        while (mAsNode && cAsNode) {
            mAs = (accScp_p) (mAsNode->data);
            cAs = (accScp_p) (cAsNode->data);
            if (dbg && 0) {
                fprintf(dbgMpa," (mL%d,cL%d)",
                    mAs->loop_id, cAs->loop_id);
            }
            lpOrder = cmpLpOrder(mAs->loop_id,cAs->loop_id);
            if (lpOrder==1) {
                mAsNode = mAsNode->next;
            }
            else if (lpOrder==-1) {
                cAsNode = cAsNode->next;
            }
            else {//same loop
                if (isPredecessor(curLp, loops[mAs->loop_id])) {
                    maxLw = max(mAs->lw, cAs->lw);
                    minUp = min(mAs->up, cAs->up);
                    if (dbg) {
                        fprintf(dbgMpa," (L%d,%d,%d) ",
                                mAs->loop_id, maxLw,minUp);
                    }
                    scpConf *= (minUp-maxLw)+1;
                }
                mAsNode = mAsNode->next; cAsNode = cAsNode->next;
            }
        }
        if (dbg) {fprintf(dbgMpa," = %d",scpConf);}
        totalConf += scpConf;
    }
    return totalConf;
}

void estUnknownMiss() {
    #if 0
    if (addrBlk->blkAddr == UNKNOWN_ADDR) {
        //double miss for unknown
        if (USE_DOUBLE_MISS) {
            max_miss = 2*max_exec;
            all_PS = 0;
            d_inst->max_miss += max_miss;
            count2x = 1;
            if (dbg) {
                fprintf(dbgMpa,"\nDataRef: ");
                fprintInstr(dbgMpa,d_inst->insn);
                fprintf(dbgMpa," DOUBLE MISS : %d",d_inst->max_miss);
                fflush(dbgMpa);
            }
        }
        else {
            max_miss = max_exec;
            all_PS = 0;
            count2x = 0;
            if (dbg) {
                fprintf(dbgMpa,"\nDataRef: ");
                fprintInstr(dbgMpa,d_inst->insn);
                fprintf(dbgMpa," ALL MISS : %d",d_inst->max_miss);
                fflush(dbgMpa);

            }
        }
        continue;
    }
    #endif
}
void estimateInducedMiss(tcfg_node_t* bbi, int type) {
    int dbg = 1;
    dat_inst_t*     d_inst;
    insn_t*         insn;
    int             n_inst,cs;
    int             num_PS, max_miss, max_exec, count2x; 
    int             all_PS;
    
    worklist_p      confScp, PS_set, miss_set, PS_req;
    worklist_p      addrNode, setNode, csNode, prvReqNode, prvNode;
    saddr_p        addrBlk;
    sblk_p       setBlk;

    loop_t          *lp, *tmpLp, *parLp;

    bbi->max_miss = 0;
    bbi->dcache_delay = 0;
    lp = bbi_lp(bbi);
    max_exec = lp->exec;
    join2acs(bbi->acs_in, bbi->acs_out);


    if (dbg) {
        fprintf(dbgMpa,"\n****\nEst non-PS miss in bbi (%d,%d), L%d, ME: %d",
                bbi_pid(bbi),bbi_bid(bbi),bbi_lp(bbi)->id, max_exec);
    }
    for (n_inst = 0; n_inst < bbi->bb->num_d_inst; n_inst++) {
        d_inst = &(bbi->bb->d_instlist[n_inst]);
        insn = d_inst->insn;
        num_PS = 0; 
        PS_set = NULL;

        max_miss = 0; 
        miss_set = NULL;


        d_inst->max_exec = max_exec;
        totalDataExec +=max_exec;


        PS_req = NULL; 
        prvNode = NULL;
        all_PS = 1;
        if (dbg) {
            fprintf(dbgMpa,"\n\nDataRef: ");
            fprintInstr(dbgMpa,d_inst->insn);
        }
        for (addrNode = d_inst->addr_set; addrNode; addrNode = addrNode->next) {
            addrBlk = (saddr_p)(addrNode->data);
            cs = GET_SET(addrBlk->blkAddr);
            if (0) fprintf(dbgMpa," %x:",addrBlk->blkAddr);
            for (setNode=bbi->acs_out[cs]; setNode; setNode = setNode->next) {
                setBlk = (sblk_p)(setNode->data);
                if (addrBlk->blkAddr > setBlk->m->blkAddr) continue;
                if (addrBlk->blkAddr == setBlk->m->blkAddr) {
                    if (setBlk->age==PSEUDO) {//non-PS
                        //max_miss+=estConfScope(setBlk->m,bbi->acs_out[cs],lp);
                        if (0) fprintf(dbgMpa,"m",addrBlk->blkAddr);
                        all_PS = 0;
                        //max_miss+=estScopeSize(setBlk->m->accScp,lp);
                        //NOTE: need better impl. of conf set
                        if (dbg) addToWorkList(&miss_set, addrBlk);
                        break;
                    }
                    else {//PS
                        if (0) fprintf(dbgMpa,"p",addrBlk->blkAddr);
                        addAfterNode(addrBlk,&prvNode, &PS_req);
                        //addToIncSet(addrBlk,&prvReqNode,&(lpReqPS[lp->id]));
                        num_PS++; 
                        break;
                    }
                }
                else {//prvBlk->blkAddr < addrBlk->blkAddr < setBlk->blkAddr
                    //not yet loaded to acs -> PS
                    //addToIncSet(addrBlk,&prvReqNode,&(lpReqPS[lp->id]));
                    if (1) fprintf(dbgMpa,"p",addrBlk->blkAddr);
                    addAfterNode(addrBlk,&prvNode, &PS_req);
                    num_PS++; 
                    break;
                }
            }
        }

        //Classic classification
        if (all_PS) {//all PSistence
            //rough est of all possible miss caused by this access in a loop
            parLp = lp;
            while (parLp!=NULL) {
                lp_allms[parLp->id]+=max_exec;
                parLp = parLp->parent;
            }
            max_miss = 0;
            prvReqNode = NULL;
            for (setNode = PS_req; setNode; setNode = setNode->next) {
                addrBlk = (saddr_p)(setNode->data);
                addToIncSet(addrBlk, &prvReqNode, &(lpReqPS[lp->id]));
            }
        }
        else {//non-PS -> all miss
            max_miss = max_exec;
        }
        d_inst->max_miss = max_miss;
        bbi->max_miss += d_inst->max_miss;
        totalNPersMiss += d_inst->max_miss;

        if (dbg) {
            fprintf(dbgMpa,"\n#PS: %d, max_miss: %d, max_exec: %d",
                        num_PS, max_miss, max_exec);
            fprintf(dbgMpa,"\nList of PS.req :");
            printMemScpSet(dbgMpa,PS_req,0);
            while(!isEmpty(PS_req)) removeOneFromWorkList(&PS_req);
            fprintf(dbgMpa,"\nList of miss.blk :");
            printMemScpSet(dbgMpa,miss_set,0);
            while(!isEmpty(miss_set)) removeOneFromWorkList(&miss_set);
        }
        if (1) {
            printf("\n\nDataRef: ");printInstr(d_inst->insn);
            printf("#PS: %d, max_miss: %d, max_exec: %d",
                        num_PS, max_miss, max_exec);
            fflush(stdout);
        }
        PS_update(bbi->acs_out,d_inst->addr_set,lp); 
    }
}

int Olap(sblk_p acsBlk, worklist_p acsSet, loop_t *lp) {
    int dbg = 0; int flag;
    int cnt = 0;
    worklist_p acsNode;
    sblk_p cnfBlk;
    flag = 0;
    for (acsNode=acsSet; acsNode; acsNode=acsNode->next) {
        cnfBlk = (sblk_p) (acsNode->data);
        if (compareMemScp(acsBlk->m, cnfBlk->m, lp)==1) {
            cnt++;
            if (dbg && !flag) {
                fprintf(dbgMpa,"\nScpConflict with %x:",acsBlk->m->blkAddr);
                flag = 1;
            }
            if (dbg)fprintf(dbgMpa," %x",cnfBlk->m->blkAddr,cnfBlk->age);
        }
        if (cnt == PSEUDO) return cnt;
    }
    return cnt;
}
/*merge function*/
void mergeInnerLoopAnalysis() {
        #if 0
        if (cmpLpOrder(hdLpId,blkLpId)==1) {/* blk is head of inner loop*/
            if (dbg) {
                fprintf(dbgMpa,"\nAnalyze inner L%d of L%d",blkLpId,hdLpId);
                fflush(dbgMpa);
            }
            //analyze the inner loop
            analyze_loop_ps(blk_lp);
            //merge inner loop result with current loop
            inner_tail = blk_lp->tail;
            for (in_edge = cur->in; in_edge; in_edge = in_edge->next_in) {
                if (bbi_lpId(in_edge->src)!=hdLpId) continue;
                //in_edge->src is blk from curLoop to innerLoop 
                change = 0;
                for (out_edge = inner_tail->out; 
                        out_edge; out_edge = out_edge->next_out) {
                    tmp = out_edge->dst;
                    if (bbi_lpId(tmp)==hdLpId) {
                        if (dbg) {
                        fprintf(dbgMpa,"\nMerge L%d: (%d,%d)-> L%d -> (%d,%d)",
                            hdLpId, bbi_pid(in_edge->src),bbi_bid(in_edge->src),
                            blkLpId, bbi_pid(tmp),bbi_bid(tmp));
                            fflush(dbgMpa);
                        }
                        //out_edge->dst is blk from innerLoop back to curLoop
                        change = PS_merge(in_edge->src->acs_out,
                                    tmp->acs_in,blk_lp);
                        if (visited[tmp->id]==0) {
                            change = 1; visited[tmp->id]=1;
                        }
                        if (change) {
                            // perform abs.int within the block
                            transform_bbi_dcache(tmp,loop,PERSISTENCE);
                            for (tmp_edge= tmp->out; 
                                    tmp_edge; tmp_edge = tmp_edge->next_out){
                            p_enqueue(&pQueue,tmp_edge->dst,tmp_edge->dst->id); 
                            }
                        }
                    }
                }
            }
        }
        #endif
}
/*** update AS after data access ***/
void PS_update_Old(scp_acs acs_out, worklist_p addr_set,  loop_t *curLoop) {
    int dbg = 0;
    int i;
    worklist_p      addrNode, confNode;
    worklist_p      acsNode, curNode, prvNode;
    worklist_p      renewList, prvRenew;   /*list of mem.blk to renew */
    worklist_p      newList, prvNew;   /*list of mem.blk to renew */
    worklist_p      agedList, prvAged; /*list of aged mem.blk*/

    saddr_p addrBlk; 
    sblk_p tmpBlk, acsBlk,confBlk;
    int s;
    int flag, found, selfConf, change, added, maxAge;


    renewList = NULL; 
    /*Impl.note: addr_set->addr & acs->addr is in increasing order
     *addrNode->addr >= prvNode->addr
     *check addrNode->addr \in acs -> only need to search from prvNode*/
    if (dbg) {
        //fprintf(dbgMpa,"\n\nPersistent update L%d",curLoop->id);
        fprintf(dbgMpa,"\n\nAddrSet: ");printMemScpSet(dbgMpa,addr_set,1);
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\nS[%d] ",i);printCacheSet(dbgMpa,acs_out[i]);
        }
    }

    //search through acs, renew cache age if possible, add new cache blk
    for (i=0;i<MAX_CACHE_SET;i++) prvAcsNode[i]=NULL;
    for (addrNode = addr_set; addrNode; addrNode=addrNode->next) {
        addrBlk = (saddr_p)(addrNode->data);    
        if (addrBlk->blkAddr==UNKNOWN_ADDR) break;
        s = GET_SET(addrBlk->blkAddr); 
        
        prvNode = prvAcsNode[s];
        if (prvNode == NULL) acsNode = acs_out[s];
        else acsNode = prvNode->next;

        found = 0;
        while (acsNode) {
            acsBlk = (sblk_p)(acsNode->data);
            //fprintf(dbgMpa,"\n%x:%x == %x:%x ? ",
            //    acsBlk->m->blkAddr,acsBlk->m->instAddr,
            //    addrBlk->blkAddr,addrBlk->instAddr);
            if (acsBlk->m->blkAddr == addrBlk->blkAddr 
                    && compareMemScp(acsBlk->m,addrBlk,curLoop)==0) {
                if (addrBlk->flag==1) acsBlk->age = 0;/*renew age*/
                found = 1;
                break;
            }
            else if (acsBlk->m->blkAddr > addrBlk->blkAddr ) {
                //prvBlk->blkAddr < addrBlk->blkAddr < acsBlk->blkAddr
                //suitable place to insert
                break;
            }
            else {
                // acsBlk->m->blkAddr < addrBlk->blkAddr
                // blkAddr==addrBlk && acsBlk->m->instAddr!=addr->instAddr
                //keep moving up to search for mem.blk or correct place to add
                prvNode = acsNode;
                acsNode = acsNode->next;
            }
        }
        if (!found) {
            //fprintf(dbgMpa," F3");
            tmpBlk          = malloc(sizeof(sblk_s));
            tmpBlk->m       = addrBlk;
            tmpBlk->age     = 0;
            tmpBlk->flag    = addrBlk->flag;
            addAfterNode(tmpBlk,&prvNode, &(acs_out[s]));
        }
        prvAcsNode[s] = prvNode;
    }

    //age existing memory block in acs, age only 1 each data access
    agedList=NULL;
    for (addrNode = addr_set; addrNode; addrNode=addrNode->next) {
        addrBlk = (saddr_p)(addrNode->data);
        if (addrBlk->blkAddr==UNKNOWN_ADDR) {
            updateUnknownAddr();
            break;
        }
        s = GET_SET(addrBlk->blkAddr);
        for (acsNode=acs_out[s]; acsNode; acsNode=acsNode->next) {
            acsBlk = (sblk_p)(acsNode->data);
            if (!acsBlk->wasAged && acsBlk->age<PSEUDO 
                    && acsBlk->age < maxConflict(acsBlk, acs_out[s], curLoop)
                    && compareMemScp(acsBlk->m,addrBlk, curLoop)==1) {
                acsBlk->wasAged = 1;
                acsBlk->age++;
                addToWorkList(&agedList, acsBlk);
            }
        }
    }
    //reset wasAged to 0 for next access update
    while (!isEmpty(agedList)) {
        acsBlk = removeOneFromWorkList(&agedList);
        acsBlk->wasAged = 0;
    }
    if (dbg) {
        for (i=0; i<MAX_CACHE_SET; i++) {
            fprintf(dbgMpa,"\n-->S[%d] ",i);printCacheSet(dbgMpa,acs_out[i]);
        }
    }
}

/*Update cache state if accessed addr is unknown*/
void updateUnknownAddr() {
    #if 0
    if (addrBlk->blkAddr==UNKNOWN_ADDR) {
        if (USE_DOUBLE_MISS) {
            //instead of aging the whole cache set, consider double miss
            return;
        }
        else {
            for (i=0; i<MAX_CACHE_SET; i++) {
                prvNode = NULL;
                for (acsNode = acs_out[i]; acsNode; acsNode=acsNode->next){
                    acsBlk = (sblk_p)(acsNode->data);
                    acsBlk->age = min(acsBlk->age+1, PSEUDO);
                    prvNode = acsNode;
                }
                if (prvNode) {
                    acsBlk = (sblk_p)(prvNode->data);
                    if (acsBlk->m->blkAddr != UNKNOWN_ADDR) {
                        tmpBlk = malloc(sizeof(mpablk_i));
                        tmpBlk->m = addrBlk;
                        //tmpBlk->m = createMemScp(addrBlk->blkAddr, 
                        //       addrBlk->flag, addrBlk->accScp);
                        tmpBlk->age = PSEUDO;
                        tmpBlk->flag = addrBlk->flag;
                        addAfterNode(tmpBlk,&prvNode, &(acs_out[i]));
                    }
                }
            }
            return;
        }
    }
    #endif
}
int cmp_mpaInst(sblk_p src, sblk_p dst) {
    if (src->age != dst->age) return 1;
    if (src->m->blkAddr != dst->m->blkAddr) return 2;
    return 0;
}
/*** Join function: join cache state from multiple incoming edges ***/
int PS_join_Old(scp_acs src, scp_acs dst ) {
    int dbg = 0;
    int i;
    worklist_p sNode, dNode, prvNode, tmpNode; 
    sblk_p sBlk, dBlk, tmpBlk;
    int change;

    if (dbg) fprintf(dbgMpa,"\nPersistent join");
    change = 0;
    for (i=0; i<MAX_CACHE_SET; i++) {
        if (dbg) {
            fprintf(dbgMpa,"\nS[%d]: ",i);printCacheSet(dbgMpa,src[i]);
            fprintf(dbgMpa,"\nD[%d]: ",i);printCacheSet(dbgMpa,dst[i]);
        }
        sNode = src[i];
        dNode = dst[i]; prvNode = NULL;
        /*blks are listed in increasing order*/
        /*merge src -> dst must also in increasing order*/
        while (sNode) {
            sBlk = (sblk_p)(sNode->data);
            if (dNode) {
                dBlk = (sblk_p)(dNode->data); 
                if (sBlk->m->blkAddr == dBlk->m->blkAddr 
                        && sBlk->m->instAddr==dBlk->m->instAddr) {
                    if (sBlk->age > dBlk->age) {
                        change = 1;
                        dBlk->age = sBlk->age;
                    }
                    if (sBlk->flag==0 && dBlk->flag!=0) {
                        change = 1;
                        dBlk->flag = 0;
                    }
                    prvNode = dNode;
                    sNode = sNode->next;
                    dNode = dNode->next;
                }
                else if (sBlk->m->blkAddr > dBlk->m->blkAddr) {
                    //ignore, cannot add new sBlk here to keep intergrity
                    prvNode = dNode;
                    dNode = dNode->next;
                }
                else {
                    //sBlk->m->blkAddr < dBlk->m->blkAddr
                    // || sBlk->m->addrInst != dBlk->m->addrInst
                    tmpBlk = malloc(sizeof(mpablk_i));
                    copy_mpaInst(sBlk, tmpBlk);
                    addAfterNode(tmpBlk, &prvNode, &(dst[i]));
                    sNode = sNode->next;
                    change = 1;
                }
            }
            else {//dNode = NULL -> adding sNode
                tmpBlk = malloc(sizeof(mpablk_i));
                copy_mpaInst(sBlk, tmpBlk);
                addAfterNode(tmpBlk,&prvNode,&(dst[i]));
                sNode = sNode->next;
                change = 1;
            }
        }//end while, sNode = NULL;
        if (dbg && change) {
            fprintf(dbgMpa,"\n--> D[%d]: ",i);printCacheSet(dbgMpa,dst[i]);
        }
        else if (dbg) {//!change
            fprintf(dbgMpa,"\n--> ");
        }
    }//end for
    return change;
}

#if 0
char* expandInVar( reg_t *regList, char* deritree) {
    int dbg = 0;
    char *str, *tmpStr;
    char tmp[256], regVal[256], token[256];
    int  pos, regNum, curReg;
    int  cnt,flag;//count token
    biv_p biVar;
    str = (char*)calloc(256,sizeof(char));
    pos = 0;
    cnt = 0;
    if (dbg) {fprintf(dbgAddr,"\nExpand deri:%s ",deritree);fflush(dbgAddr);}
    str[0]='\0';
    while (getNextElem(deritree,&pos,token)!=0) {
        //fprintf(dbgAddr,"\n Token: %s",token);
        if (token[0]=='$') {//some induction vars
            regNum = findReg(token);
            if (dbg) {
                fprintf(dbgAddr,"\nExpand inReg: ");
                printRegVal(dbgAddr,regList[regNum]);
            }
            if (regNum<0 || regList[regNum].inVar==NULL) {
                printf("\nInvalid Reg or not BIV: deri:%s tk:%s",deritree,token);
                fflush(stdout);
                exit(1);
            }
            /*Add init*/
            biVar = (biv_p)regList[regNum].inVar;
            tmpStr = expandInVar(regList,biVar->init);
            if (tmpStr==NULL) {
                printf("\nNULL expandInVar(%s)",biVar->init);fflush(stdout);
                exit(1);
            }
            strcat(str,"(");
            strcat(str,tmpStr);
            free(tmpStr);
            /*Mark reg name*/
            strcat(str,"+");strcat(str,regList[regNum].name);
            strcat(str,")");
        }
        else {
            strcat(str,token);
        }
        cnt++;
    }
    if (dbg) {fprintf(dbgAddr,"\nExpanded reg:%s ",str);fflush(dbgAddr);}
    return str;
}
#endif
#if 0
/*check if bivA belong to loop lp*/ //OBSOLETE???
biv_p findLpBIV(loop_t *lp, biv_p bivA) {
    worklist_p  item;
    biv_p       tmpBiv;
    if (lp==NULL) return NULL;
    for (item = (worklist_p)(lp->biv_list); item; item = item->next) {
        tmpBiv = (biv_p)(item->data);
        if (regEq(tmpBiv,biv)) return biv;
    }
    return NULL;
}
int analyze_regular_access_old(dat_inst_t *d_inst, inf_node_t* ib) {
    int dbg = 0, *visited;
    int i,j,k,pos, minCoef;
    char str[DERI_LEN],des[DERI_LEN];
    char *tmpStr;

    int inc; //affine eq is always incretsing
    int monotone; //affine eq is monotone during enum, always inc or dec
    int inorder; //affine eq follows loop heirarchy order
    poly_expr_t *addrEq, *maxEq, *tmpEq, *curEq, *prvEq;

    poly_expr_t bufEq;
    accScp_s    bufScp;
    loop_t *loop, *curLp, *outerMost, *tmpLp;
    accScp_p curScp, tmpScp, nextScp;
    worklist_p curTSNode, orgTSNode, tmpTSNode, maxTSNode;
    worklist_p addrNode, prvNode;
    biv_p       inVar;

    visited = calloc(num_tcfg_loops,sizeof(int));
    i=0;
    strcpy(str,d_inst->deritree);
    /*** convert deritree into an affine function ***/
    /*expand induction var*/
    pos = 0;str[0]='\0';
    tmpStr = expandInVar(ib->regListIn,d_inst->deritree);
    if (tmpStr) {sprintf(str,"%s%",tmpStr);free(tmpStr);}
    else {fprintf(dbgAddr,"\nNULL deritree %s",d_inst->deritree);}
    if (dbg) {
        fprintf(dbgAddr,"\nExpanded infix str: %s",str);
        fflush(dbgAddr);
    }
    /*After expanding induction var, some regular access turn out to be
     *half regular access, because btse address is unknown*/
    if (strchr(str,'T')!=NULL) {
        analyze_half_regular_access(d_inst,ib,str);
        return;
    }

    /*convert str to prefix format*/
    convert2Prefix(str,des);

    pos = 0;
    if (0) fprintf(dbgAddr,"\nCompute Prefix");
    addrEq = computePrefix(str,&pos); 

    /*** enumerate regular equation ***/
    if (dbg) {
        fprintf(dbgAddr, "\n Affine array expr: ");
        printExpression(dbgAddr,addrEq);fflush(dbgAddr);
    }


    /*find corresponding loop for each V_i*/
    /*the order of access scope must corresponding to other of $i appear in Eq*/
    orgTSNode = NULL; curTSNode = NULL;
    prvEq = NULL;
    for (curEq = addrEq; curEq; curEq = curEq->next) {
        if (curEq->regName[0]=='1') { /*constant*/
            /*add dummy access scope*/
            tmpScp = malloc(sizeof(accScp_s));
            tmpScp->loop_id = -1;
            tmpScp->lw = 0; tmpScp->up = 0; tmpScp->flag = 0;
            addAfterNode(tmpScp,&curTSNode,&(orgTSNode));
        }
        else if (curEq->regName[0]=='T') { /*unpredictable value*/
            //just do nothing
        }
        else if (curEq->regName[0]=='$') { /*loop induction var*/
            int found;
            linkedlist_p item;
            found = 0;
            loop = getIbLoop(ib);
            //ib->loop_id is different from loop->id of the loop ib is in
            while (found==0 && loop) {
                if (dbg & 0) {
                    fprintf(dbgAddr,"\n BIV list of L%d ",loop->id);
                    for (item = loop->biv_list; item; item = item->next) {
                        biv_p biv = (biv_p)(item->data);
                        fprintf(dbgAddr," (%s,%s,%d) ",
                                biv->regName,biv->init,biv->stride);
                    }
                    fflush(dbgAddr);
                }
                inVar = findLpBIV(loop,curEq->regName);
                if (inVar==NULL) loop = loop->parent;
                else found=1;
            }
            if (found) {
                curEq->coef *= inVar->stride;
                if (dbg & 1) {
                    fprintf(dbgAddr,"\n%d:%s is in L%d, lb %d", curEq->coef,
                            curEq->regName, loop->id, loop->bound);
                    fprintf(dbgAddr,", (%s,%s,%d) ",
                            inVar->regName,inVar->init,inVar->stride);
                    fflush(dbgAddr);
                }
                /*sometimes, AddrExpr consists of several induction vars
                 *from the same loop -> add these induction var together*/
                found = 0;
#if 1
                tmpEq = addrEq;
                for (tmpTSNode=orgTSNode;tmpTSNode;tmpTSNode=tmpTSNode->next) {
                    tmpScp = (accScp_p)(tmpTSNode->data);
                    if (tmpScp->loop_id == loop->id) {
                        found = 1;
                        break;
                    }
                    tmpEq = tmpEq->next;
                }
#endif
                if (!found) {
                    tmpScp = malloc(sizeof(accScp_s));
                    tmpScp->loop_id = loop->id;
                    visited[loop->id] = 1;
                    tmpScp->lw = 0; tmpScp->up = 0; tmpScp->flag = 0;
                    addAfterNode(tmpScp,&curTSNode,&(orgTSNode));
                }
                else {
                    tmpEq->coef += curEq->coef*inVar->stride;
                    prvEq->next = curEq->next;//delete curEq,NOTE: mem.leak
                }
            }
            else {
                printf("\nCannot find loop for reg: %s",curEq->regName);
                fflush(stdout);exit(1);
            }

        }
        else {
            printf("\nError: bad regName: %s",curEq->regName);fflush(stdout);
            exit(1);
        }
        prvEq = curEq;
    }
    if (dbg & 0) {
        fprintf(dbgAddr,"\nAddress expression ");
        printExpression(dbgAddr,addrEq);
        fprintf(dbgAddr,"\nAccess scope ");
        printAccScpSet(dbgAddr,orgTSNode);
    }
    /*rearrange eq into decretsing coef, to gen incretsing addr while enum*/
    tmpEq=addrEq; tmpTSNode = orgTSNode;
    while (tmpEq) {
        maxEq = tmpEq;
        maxTSNode = tmpTSNode;
        curTSNode = tmpTSNode->next;

        for (curEq = tmpEq->next ; curEq; curEq = curEq->next) {
            if (dbg && 0) {
                tmpScp = (accScp_p)(curTSNode->data);
                fprintf(dbgAddr,"\n %d:%s , L%d ",
                        curEq->coef,curEq->regName, tmpScp->loop_id);
                fflush(dbgAddr);
            }
            if ( abs(curEq->coef) > abs(maxEq->coef) ) {
                maxEq = curEq;
                maxTSNode = curTSNode;
            }
            curTSNode = curTSNode->next;
        }
        if (0) fprintf(dbgAddr,"\nmaxEq: %x:%s",maxEq->coef, maxEq->regName);
        bufEq.coef = maxEq->coef;
        strcpy(bufEq.regName,maxEq->regName);
        maxEq->coef = tmpEq->coef;
        strcpy(maxEq->regName, tmpEq->regName);
        tmpEq->coef = bufEq.coef;
        strcpy(tmpEq->regName,bufEq.regName);

        tmpScp = (accScp_p)(maxTSNode->data);
        curScp = (accScp_p)(tmpTSNode->data);
        copy_accScp(tmpScp,&bufScp);
        copy_accScp(curScp,tmpScp);
        copy_accScp(&bufScp,curScp);

        tmpEq = tmpEq->next;
        tmpTSNode = tmpTSNode->next;
    }
    //remove dummy access scope
    prvNode = NULL;
    for (curTSNode = orgTSNode; curTSNode; curTSNode=curTSNode->next) {
        curScp = (accScp_p) (curTSNode->data);
        if (curScp->loop_id == -1) {
            if (prvNode) prvNode->next = curTSNode->next;
            else orgTSNode = orgTSNode->next;
        }
        prvNode = curTSNode;
    }
    if (dbg) {
        fprintf(dbgAddr,"\nRearranged expr: ");fflush(dbgAddr);
        printExpression(dbgAddr,addrEq);fflush(dbgAddr);
        fprintf(dbgAddr,"\nRearranged access scope ");
        printAccScpSet(dbgAddr,orgTSNode);
        fprintf(dbgAddr,"\nAddr expr property: ");fflush(dbgAddr);
    }

    /*identify if address equation satisfies certain properties*/
    monotone = 1; inorder = 1; inc = 1;
    curTSNode = orgTSNode;
    for (curEq = addrEq; curEq; curEq = curEq->next) {
        if (curEq->regName[0]!='$') continue;

        if (curEq->coef<0) inc = 0;
        //curEq->regName == "S..."
        curScp = (accScp_p)(curTSNode->data);
        curLp = loops[curScp->loop_id];

        //tmp___ = next___
        tmpTSNode = curTSNode->next; if (tmpTSNode==NULL) break;
        tmpEq = curEq->next; if (tmpEq==NULL) break;
        tmpScp = (accScp_p)(tmpTSNode->data); 
        tmpLp = loops[tmpScp->loop_id];

        /*Inorder 
          = curEq->coef > tmpEq->coef -> curLp outer lp of tmpLp
          = nextLp is inner loop of curLp
         */
        if (curLp->id <= tmpLp->id) inorder = 0;

        /*Monotone 
          = curEq->coef > (nextEq->coef * nextLp->bound)
          = inc/dec caused by curLp is unaffected by nextLp
         */
        if (abs(tmpEq->coef*tmpLp->bound) > abs(curEq->coef)) {
            if (dbg) fprintf(dbgAddr," |%d*%d|>%d Monotone:0",
                    tmpEq->coef,tmpLp->bound, curEq->coef);
            monotone = 0;
        }
        curTSNode = curTSNode->next;
    }
    /*Sequential access 
      = traverse array element sequentially according to row order
      = monotone & inorder
     */

    /*set flag for TS scope*/
    for (curTSNode = orgTSNode; curTSNode; curTSNode = curTSNode->next){
        curScp = (accScp_p)(curTSNode->data);
        curScp->flag = monotone && inorder;
    }

    /*add loop scope that is not considered in the affine eq */
    for (curTSNode=orgTSNode; curTSNode->next; curTSNode=curTSNode->next);
    for (loop = getIbLoop(ib); loop; loop=loop->parent) {
        if (visited[loop->id]==0) {
            tmpScp = malloc(sizeof(accScp_s));
            tmpScp->loop_id = loop->id;
            tmpScp->lw = 0; tmpScp->up = max(loop->bound-1,0); tmpScp->flag = 0;
            addAfterNode(tmpScp,&curTSNode,&(orgTSNode));
            visited[loop->id]=1;
        }
    }

    if (dbg) {
        fprintf(dbgAddr,"\nTS: ");
        printAccScpSet(dbgAddr,orgTSNode);
    }
    curTSNode = orgTSNode;
    tmpTSNode = NULL; 
    curEnumBlk = NULL;
    for (i=0; i<num_tcfg_loops; i++) iterValue[i]=loops[i]->bound;
    enumAffineEq(d_inst, addrEq,0,inc,monotone&inorder,orgTSNode,curTSNode,
            &(d_inst->addr_set),&tmpTSNode,ib);

    /*rearrange access scope follow LPHG order*/
    inc = inc && monotone;
    for (addrNode = d_inst->addr_set; addrNode; addrNode=addrNode->next) {
        arrangeTSfollowLPHG(addrNode->data);

    }
    if (0) {
        fprintf(dbgAddr,"\nAddress set after rearranging TS: ");
        printSAddrSet(dbgAddr,d_inst->addr_set,1);
    }
    //freeList(&orgTSNode); //wrong freelist
    return 0;
}
/*find memscp with the blkAddr in a addr_set*/
/*** enumerate AddrSet & access scope of affine array access, from 0 to LB-1***/
int  *iterValue; //iterValue[id] = current iteration value of loop id
void enumAffineEq(dat_inst_t *d_inst, poly_expr_t *eq, 
        int addr, int inc, int seq,
        worklist_p orgTS, worklist_p curTS, 
        worklist_p *setHead, worklist_p *setCur, inf_node_t *ib) {
    int dbg = 1;
    worklist_p  tmpNode;
    memscp_p    tmpBlk;

    int i,lb;
    loop_t      *lp;
    poly_expr_t *next;
    accScp_p    lpTS;
    biv_p biv;

    if (eq == NULL) { //finish enumerating -> get Addr
        if (dbg && 0) {
            fprintf(dbgAddr,"\n Mem: %x, TS:",GET_MEM(addr));
            printAccScpSet(dbgAddr,orgTS);
        }
        if (curEnumBlk && GET_MEM(addr)==curEnumBlk->blkAddr) {
            mergeTS(curEnumBlk->accScp,orgTS,-1);
        }
        else {
            if (*setCur) tmpBlk=(*setCur)->data;
            else tmpBlk = NULL;
            if (tmpBlk && GET_MEM(addr)>tmpBlk->blkAddr) {
                tmpNode = findAddrBlk(GET_MEM(addr),*setCur);
            }
            else {
                tmpNode = findAddrBlk(GET_MEM(addr),*setHead); 
            }
            if (tmpNode) {
                tmpBlk = (memscp_p)(tmpNode->data);
                if (tmpBlk->blkAddr==GET_MEM(addr)) {
                    mergeTS(tmpBlk->accScp, orgTS,-1);
                }
                else {
                    tmpBlk = createMemScp(GET_MEM(addr),
                            d_inst->inst->addr,seq, orgTS);
                    addAfterNode(tmpBlk, &tmpNode, setHead);
                    setCur = &tmpNode;
                }
            }
            else {//tmpNode = NULL -> prvNode=NULL -> setHead=NULL
                tmpBlk = createMemScp(GET_MEM(addr),d_inst->inst->addr,
                        seq, orgTS);
                addAfterNode(tmpBlk, &tmpNode, setHead);
                setCur = &tmpNode;
            }
            curEnumBlk = tmpBlk;

        }
    } //keep enumerating next lp
    else if (eq->regName[0]=='1') {
        enumAffineEq(d_inst,eq->next,addr+eq->coef, inc, seq,
                orgTS, curTS, setHead, setCur, ib);
    }
    else {// coef_i * $L_i
        lpTS = (accScp_p)(curTS->data);
        lp = loops[lpTS->loop_id];
        biv = findLpBIV(lp,eq->regName);
        if (lp->rbId==-1) lb = lp->bound;
        else {
            switch(lp->rbType) {
                case EQL_LB:
                    lb = iterValue[lp->rbId];
                break;
                case INV_LB:
                    lb = lp->rbBound - iterValue[lp->rbId];
                break;
                case FIX_LB:
                    lb = lp->rbBound;
                break;
                default:
                fprintf(dbgAddr,"\n Unknown relative bound type: %d",
                        lp->rbType);
                fflush(dbgAddr);exit(1);
                break;
            }
        }
        if (dbg && 0) {
            fprintf(dbgAddr,"\n Enum: %d*L%d ,LB: %d", 
                    eq->coef, lp->id, lb);
            fflush(dbgAddr);
        }
        for (i=0; i<lb; i++) {
            iterValue[lp->id] = i;
            lpTS->lw = i;
            lpTS->up = i;
            enumAffineEq(d_inst, eq->next,addr+eq->coef*i, inc, seq,
                    orgTS, curTS->next, setHead, setCur,ib);
        }
    }
}

/*arrange access scope order follow LPHG order
 * tsNext = ts->next --> tsNext->lp = ts->lp->parent
 * lp->id > lp1->id --> lp is outer loop of lp1, except lp->id==0: outermost lp*/
#endif
