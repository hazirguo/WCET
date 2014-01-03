#include "symexec.h"

FILE *dbgF;

/*** PARAMETER VALUE OPERATION ***/

void setNewPara(void *para) {
    static int paraCount = 0;
    char name[4];
    int dbg = 1;
    
    sprintf(name,"V%d",paraCount);
    paraCount++;
    para = calloc(strlen(name)+1,sizeof(char);
    strcpy(para,name);
    if (dbg) fprintf(dbgF,"\nNew para: %s",name);
}

/*** INDUCTION VALUE OPERATION ***/

int updateInitVal(biv_p biVar, char* initVal) {
    int dbg = 0;
}
int cmpBIV(biv_p inVar1, biv_p inVar2) {
    if (cmpReg(
    if (inVar1->stride != inVar2->stride) return 1;
    if (strcmp(inVar1->regName, inVar2->regName)!=0) return 1;
    return 0;
}
int cpyBIV(biv_p varDst, biv_p varSrc) {
    varDst->insn = varSrc->insn;
    setReg(varDst->initValue,varSrc->initValue.type,varSrc->initValue.value);
    strcpy(varDst->opr,varSrc->opr);
    varDst->stride = varSrc->stride;
}
void printBIV(FILE *fp, biv_p biVar) {
    fprintf(fp," BIV (%s,%s,%d) ",biVar->regName,biVar->init,biVar->stride);
    fflush(fp);
}

/*** EXPRESSION VALUE OPERATION ***/
void computeExpr(char *opr, expr_p *expr1, expr_p *expr2) {
    int dbg = 0;
    poly_expr_t *cur1, *cur2, *prev1, *prev2, *num;
    int i,flag, disFlag, fin;
    int tmp;
    /*Decide whether we need to follow distribution rule*/
    if (dbg) {
        //fprintf(dbgAddr,"\nFlag 0");fflush(dbgAddr);
        fprintf(dbgAddr,"\n E1: ");
        printExpression(dbgAddr,expr1);
        fprintf(dbgAddr," %s E2: ",opr);
        printExpression(dbgAddr,expr2);
    }
    if (opr[0]=='+' || opr[0]=='-') disFlag = 0;
    else disFlag = 1;
    /*1 expr -> not distributive opr*/
    for (cur1 = expr1; cur1; cur1 = cur1->next) cur1->added = 0;
    for (cur2 = expr2; cur2; cur2 = cur2->next) cur2->added = 0;
    cur1 = expr1;cur2 = expr2;
    if (cur1 && cur2 && (cur1->next==NULL && cur2->next==NULL) ) disFlag = 0;
    if (disFlag == 0) {/*Non distributive opr like +/-*/

        /*1st phase: find expr2's regName similar to expr1's regName
         *add their coefficient
         */
        while (cur1!=NULL) {
            cur2 = expr2;
            flag = 0;
            fin = 1;
            while (cur2!=NULL) { //find similar regName in cur2
                if (cur2->added!=0) { //already processed, somehow
                    cur2 = cur2->next; continue;
                }
                if (cur2->coef==0) {
                    cur2->added=2;//ignore 0:x
                    cur2 = cur2->next; continue;
                }
                fin = 0;
                if (strcmp(cur1->regName,cur2->regName)==0 
                        && cur2->regName[0]=='$' && cur1->regName[0]=='$'){
                    flag = 1; break;
                }
                else if (cur1->regName[0]=='1' && cur2->regName[0]=='1') {
                    flag = 2; break;
                }
                cur2 = cur2->next;
            }
            if (fin==1) break; //has finish searching all regName in expr2
            if (dbg) {
                //fprintf(dbgAddr,"\nFlag 0");fflush(dbgAddr);
                fprintf(dbgAddr,"\n C1: ");
                printExpression(dbgAddr,cur1);
                fprintf(dbgAddr," %s C2: ",opr);
                printExpression(dbgAddr,cur2);
            }
            switch (flag) {
                case 1: /*expr format: c1:$5 + c2:$5*/
                    /*two induction var: $1 + $1 */
                    cur1->added=0;
                    cur2->added=2;
                    if (strcmp(opr,"+")==0) {
                        cur1->coef += cur2->coef;
                    }
                    else if (strcmp(opr,"-")==0) {
                        cur1->coef -= cur2->coef;
                    }
                    else {
                        fprintf(dbgAddr,"\nUnknown opr 1: %s %s %s",
                                cur1->regName,opr,cur2->regName);fflush(stdout);
                    }
                    break;
                case 2:/*expr format: 4:1 + - * << / 10:1*/
                    cur1->added=0;
                    cur2->added=2;
                    /*two numerical value 1+1 */
                    if (strcmp(opr,"+")==0) {
                        cur1->coef += cur2->coef;
                    }
                    else if (strcmp(opr,"-")==0) {
                        cur1->coef -= cur2->coef;
                    }
                    else if (strcmp(opr,"*")==0) {
                        cur1->coef *= cur2->coef;
                    }
                    else if (strcmp(opr,"<<")==0) {
                        for (i=0;i<cur2->coef;i++) cur1->coef*=2;
                    }
                    else {
                        fprintf(dbgAddr,"\nUnknown opr 2: %s %s %s",
                                cur1->regName,opr,cur2->regName);fflush(stdout);
                    }
                    break;
                default:
                    break;
            }
            cur1 = cur1->next;
        }
        /*Phase 2: process regName in cur2 with no-equivalent regName in cur1*/
        for (cur1=expr1; cur1->next; cur1=cur1->next);//move to end of expr1
        while (1) {
            cur2 = expr2;
            while (cur2!=NULL && cur2->added!=0) cur2=cur2->next; 
            if (cur2) cur2->added = 1;
            else break;//finish, no unprocessed cur2


            if (strcmp(opr,"+")==0 || strcmp(opr,"-")==0) {
                //$1 +/- num -> do nothing
                cur2->added = 3;
                cur1->next = cur2;
                cur1 = cur2;
            }
            else {//opr = * , << 
                if (cur2->regName[0]=='$' || cur2->regName[0]=='T') {
                    tmp = cur1->coef;
                    cur1->coef = cur2->coef;
                    cur2->coef = tmp;
                    sprintf(cur1->regName,"%s",cur2->regName);
                }
                cur2->added = 2;
                if (strcmp(opr,"*")==0) {
                    cur1->coef *= cur2->coef;
                }
                else if (strcmp(opr,"<<")==0) {
                    for (i=0;i<cur2->coef;i++) cur1->coef*=2;
                }
                else {
                    fprintf(dbgAddr,"\nUnknown %d:%s %s %d:%s",cur1->coef,
                            cur1->regName,opr,cur2->coef,cur2->regName);
                }
            }
        }
    }
    else { /*distributive opr like * / <<: e.g. ($5 + 1) << 2 */
        if (expr2->next==NULL) {// ($5 + 1) << 2
            cur1 = expr1;
            cur2 = expr2;
            flag = 0;
        }
        else {// 2 << ($5 + 1), actually mean ($5 + 1) << 2
            cur2 = expr1;
            cur1 = expr2;
            if (dbg) {
                fprintf(dbgAddr,"\nReverse expr ");
                printExpression(dbgAddr,expr1);
                printExpression(dbgAddr,expr2);
            }
            flag = 1;
        }
        /*assume no opr like ($5+1) * ($4+3), they are not affine function*/
        while (cur2!=NULL) {
            if (cur2->regName[0]=='$') {
                printf("\nNot affine function");
                printExpression(stdout,expr1);
                printf(" %s ",opr);
                printExpression(stdout,expr2);
                fflush(stdout);
                exit(1);
            }
            cur2 = cur2->next;
        }
        //now cur1: ($5+1) << cur2: 2:1
        if (flag) cur2 = expr1;
        else cur2 = expr2;
        while (cur1!=NULL) {
            cur1->added = 2; cur2->added = 2;
            if (strcmp(opr,"*")==0) {
                cur1->coef *= cur2->coef;
            }
            else if (strcmp(opr,"/")==0) {
                cur1->coef /= cur2->coef;
            }
            else if (strcmp(opr,"<<")==0) {
                for (i=0;i<cur2->coef;i++) cur1->coef*=2;
            }
            else {
                fprintf(dbgAddr,"\nUnknown opr 5: %s %s %s",
                        cur1->regName,opr,cur2->regName);fflush(stdout);
            }
            cur1 = cur1->next;
        }
        if (flag) {// 2 << ($5+1) = 4$5 + 4, copy expr2 to expr1
            expr1->coef = expr2->coef;
            sprintf(expr1->regName,"%s",expr2->regName);
            expr1->added = 0;
            expr2->added = 2;
        }
    }

    /*Copy the remaining of expr2 to expr1*/
    cur1 = expr1;prev1 = NULL;
    while (cur1!=NULL) {prev1= cur1;cur1 = cur1->next;}//find end of cur1
    cur2 = expr2;
    while (cur2!=NULL) {
        if (cur2->added==3) {//copied to expr1
            //cannot free
            cur2 = cur2->next;
        }
        else if (cur2->added==2) {//merged to expr1
            /*already merge to expr1, free it*/
            prev2 = cur2;
            cur2 = cur2->next;
            if (prev2!=NULL) free(prev2); //NOTE: memory leak
        }
        else {//cur2->added==1 || cur2->added==0 -> copy forward
            cur1 = cur2;
            cur2 = cur2->next;
            if (prev1!=NULL) {
                prev1->next = cur1;
                prev1 = cur1;
                prev1->next = NULL;
            }
            else { //expr1 = NULL ???
                expr1 = cur2;
                prev1 = expr1; 
            }
        }
    }
    if (dbg) {
        fprintf(dbgAddr,"\n = ");
        printExpression(dbgAddr,expr1);
    }
}
void cpyExpr(expr_p exprDst, expr_p exprSrc) {
    expr_p srcNode, dstNode, prvNode;
    srcNode = exprSrc;
    dstNode = exprDst;
    while (dstNode && srcNode) {
       dstNode->coef = srcNode->coef;
       strcpy(dstNode->var,srcNode->var);

       prvNode = dstNode;
       dstNode = dstNode->next;
       srcNode = srcNode->next;
    }
    while (srcNode) {
        dstNode = malloc(sizeof(expr_s));
        dstNode->next = NULL;
        dstNode->coef = srcNode->coef;
        strcpy(dstNode->var,srcNode->var);

        prvNode->next = dstNode;
        prvNode = dstNode;
        srcNode = srcNode->next;
    }
    while (dstNode) {
        prvNode = dstNode;
        dstNode = dstNode->next;
        free(prvNode);
    }
}
void freeExpr(expr_p expr) {
    expr_p prvNode, curNode;
    prvNode = NULL;
    curNode = expr;
    while (curNode) {
        prvNode = curNode;
        curNode = curNode->next;
        if (prvNode) free(prvNode);
    }
}
