/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Symbolic Execution & Infeasible path detection on Chronos
 * Vivy Suhendra - Huynh Bach Khoa
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
 * 03/2007 reg.c
 *
 *  v4.1:
 *      _ completely rewrite the symbolic execution framework
 *          + intraprocedure execution, instead of intra basic block
 *          + memory modeling -> can trace value when saved to memory
 *          + richer value types: induction, expression, parameter
 *
 *  v4.0: as in Chronos-4.0 distribution
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symexec.h"
#include "address.h"

static FILE *dbgF;
void setDebugFile(char *fName) {
    dbgF = fopen(fName,"w");
}

/*** REGISTER OPERATIONS ***/
void initReg(reg_t *reg) {
    //NOTE: cause memory leaks if pointer used
    //use clrReg reinitialize without causing mem.leak
    reg->t      = VALUE_UNDEF;
    reg->val    = 0;
    reg->expr   = NULL;
    reg->biv    = NULL;
    reg->para[0]='\0';
    reg->name[0]='\0';
    reg->flag = 0;
}
void panicRegType(reg_t reg) {
    char *tmp;
    int dbg = 1;
    if (dbg) {
        fprintf(dbgF,"\nPanic: unknown reg value type: %d, reinit",reg.t);
        printf("\nPanic: unknown reg value type: %d, reinit",reg.t);
        fflush(dbgF);
        fflush(stdout);
    }
    exit(1);
}
void printReg(FILE *fp, reg_t reg) {
    //fprintf(fp,"{%d,",reg.t);
    fprintf(fp,"{");
    switch (reg.t) {
        case VALUE_UNDEF:
            fprintf(fp,"_");
            break;
        case VALUE_CONST:
            fprintf(fp,"%x",reg.val);
            break;
        case VALUE_EXPR:
            printExpr(fp,reg.expr);
            break;
        case VALUE_PARA:
            fprintf(fp,"%s",reg.para);
            break;
        case VALUE_BIV:
            printBIV(fp,reg.biv);
            break;
        default:
            panicRegType(reg);
    }
    fprintf(fp,"}");
}
void printRegList(FILE *fp, reg_t *regList) {
    int i;
    for (i=0; i<NO_REG; i++) {
        fprintf(fp,"\n%s -> ",regList[i].name);
        printReg(fp,regList[i]);
    }
    fflush(fp);
}
void setInt(reg_t *reg, int k) {
    int dbg = 0;
    if (reg->t !=VALUE_CONST) {clrReg(reg);reg->t = VALUE_CONST;}
    reg->val = k;
    if (dbg) {fprintf(dbgF,"\n Set int: ");printReg(dbgF,*reg);}
}

void clrReg(reg_t *reg) {
    //NOTE: avoid continually alloc/realloc
    switch (reg->t) {
        case VALUE_UNDEF: //undef
        case VALUE_CONST: //const
        case VALUE_PARA: //para
            break;
        case VALUE_BIV: //induction
            if (reg->biv) {
                clrReg( &((reg->biv)->initVal));
                free(reg->biv);
            }
            reg->biv = NULL;
            break;
        case VALUE_EXPR: //expr
            if (reg->expr) {
                clrExpr(reg->expr);
                free(reg->expr);
            }
            reg->expr = NULL;
            break;
        default: //unknown
            panicRegType(*reg);
            initReg(reg);
            break;
            
    }
    reg->t = VALUE_UNDEF;
}
void regUnknown(reg_t *reg) {
    clrReg(reg);
    reg->t = VALUE_UNPRED; 
    strcpy(reg->para,"T");
}
int regEq( reg_t reg1, reg_t reg2 ) {
    if (reg1.t != reg2.t) return 0; 
    switch (reg1.t) {
        case VALUE_UNDEF:
            return 1;
        case VALUE_CONST:
            return (reg1.val==reg2.val);
        case VALUE_PARA:
            return (strcmp(reg1.para,reg2.para)==0);
        case VALUE_EXPR:
            return exprEq(reg1.expr,reg2.expr);
        case VALUE_BIV:
            return bivEq(reg1.biv, reg2.biv);
        default:
            panicRegType(reg1);
            return 0;
    }
}
int cpyReg(reg_t *dst, reg_t src) {
    if (regEq(*dst,src)) return 0;//no change
    if (dst->t!= src.t) {
        if (dst->t!=VALUE_UNDEF) clrReg(dst);
        dst->t = src.t;
    }
    switch (src.t) {
        case VALUE_UNDEF:
            break;
        case VALUE_CONST:
            dst->val = src.val;
            break;
        case VALUE_EXPR: 
            if (dst->expr==NULL) dst->expr = calloc(1,sizeof(expr_s));
            cpyExpr( dst->expr,src.expr);
            break;
        case VALUE_PARA:
            strcpy(dst->para,src.para);
            break;
        case VALUE_BIV:
            if (dst->biv==NULL) dst->biv = calloc(1,sizeof(biv_s));
            cpyBIV(dst->biv,src.biv); 
            break;
        default:
            panicRegType(src);
            panicRegType(*dst);regUnknown(dst);
    }
    return 1;
}

int initRegSet(reg_t *regList) {
    int i;
    //NOTE: cause memory leak if regList is being used
    for (i=0; i<NO_REG; i++) initReg(regList+i);

    strcpy( regList[0].name , "$0"  ); strcpy( regList[1].name , "$1"  ); 
    strcpy( regList[2].name , "$2"  ); strcpy( regList[3].name , "$3"  );	
    strcpy( regList[4].name , "$4"  ); strcpy( regList[5].name , "$5"  ); 
    strcpy( regList[6].name , "$6"  ); strcpy( regList[7].name , "$7"  );	
    strcpy( regList[8].name , "$8"  ); strcpy( regList[9].name , "$9"  ); 
    strcpy( regList[10].name, "$10" ); strcpy( regList[11].name, "$11" );	
    strcpy( regList[12].name, "$12" ); strcpy( regList[13].name, "$13" ); 
    strcpy( regList[14].name, "$14" ); strcpy( regList[15].name, "$15" );	
    strcpy( regList[16].name, "$16" ); strcpy( regList[17].name, "$17" ); 
    strcpy( regList[18].name, "$18" ); strcpy( regList[19].name, "$19" );	
    strcpy( regList[20].name, "$20" ); strcpy( regList[21].name, "$21" ); 
    strcpy( regList[22].name, "$22" ); strcpy( regList[23].name, "$23" );	
    strcpy( regList[24].name, "$24" ); strcpy( regList[25].name, "$25" ); 
    strcpy( regList[26].name, "$26" ); strcpy( regList[27].name, "$27" );	
    strcpy( regList[28].name, "$28" ); strcpy( regList[29].name, "$29" ); 
    strcpy( regList[30].name, "$30" ); strcpy( regList[31].name, "$31" );

#if 0
    /*HBK: ignore floating point register*/
    strcpy( regList[34].name, "$f0"  ); strcpy( regList[35].name, "$f1"  ); 
    strcpy( regList[36].name, "$f2"  ); strcpy( regList[37].name, "$f3"  );
    strcpy( regList[38].name, "$f4"  ); strcpy( regList[39].name, "$f5"  ); 
    strcpy( regList[40].name, "$f6"  ); strcpy( regList[41].name, "$f7"  );
    strcpy( regList[42].name, "$f8"  ); strcpy( regList[43].name, "$f9"  ); 
    strcpy( regList[44].name, "$f10" ); strcpy( regList[45].name, "$f11" );
    strcpy( regList[46].name, "$f12" ); strcpy( regList[47].name, "$f13" ); 
    strcpy( regList[48].name, "$f14" ); strcpy( regList[49].name, "$f15" );
    strcpy( regList[50].name, "$f16" ); strcpy( regList[51].name, "$f17" ); 
    strcpy( regList[52].name, "$f18" ); strcpy( regList[53].name, "$f19" );
    strcpy( regList[54].name, "$f20" ); strcpy( regList[55].name, "$f21" ); 
    strcpy( regList[56].name, "$f22" ); strcpy( regList[57].name, "$f23" );
    strcpy( regList[58].name, "$f24" ); strcpy( regList[59].name, "$f25" ); 
    strcpy( regList[60].name, "$f26" ); strcpy( regList[61].name, "$f27" );
    strcpy( regList[62].name, "$f28" ); strcpy( regList[63].name, "$f29" ); 
    strcpy( regList[64].name, "$f30" ); strcpy( regList[65].name, "$f31" );
    strcpy( regList[66].name, "$fcc" );
#endif

    setInt(regList+0,0);
    //ERR: should only initialize bb0 with this
    //setInt(regList+28,GLOBAL_START);
    //setInt(regList+29,STACK_START);
    //regUnknown(regList+30);
    return 0;
}


int clearRegList(reg_t *regList) {
    int i;
    for( i = 1; i < NO_REG; i++ ) {
        //if( i == 28 || i == 29 ) continue;// || i == 30 
        clrReg(regList+i);  
    }
    return 0;
}

int findReg(reg_t *regList, char regName[] ) {
    int i;
    for( i = 0; i < NO_REG; i++ ) { 
        if( strcmp( regName, regList[i].name )==0) return i; 
    }
    return -1;
}

/*** PARAMETER VALUE OPERATION ***/
void setNewPara(char *para) {
    int dbg = 0;
    //ERR: not implemented
    //sprintf(name,"V%d",paraCount);
    sprintf(para,"T");
    if (dbg) fprintf(dbgF,"\nNew para: %s",para);
}

/*** INDUCTION VALUE OPERATION ***/

int updateInitVal(biv_p biVar, reg_t initVal) {
    return mergeReg( &(biVar->initVal),initVal,0);
}
void freeBIV(biv_p *biv) {
    clrReg( &( (*biv)->initVal) ); 
    free(*biv);
    *biv=NULL;
}
int bivEq(biv_p inVar1, biv_p inVar2) {
    if (strcmp(inVar1->regName,inVar2->regName)!=0) return 0;
    if (regEq(inVar1->initVal, inVar2->initVal )==0) return 0;
    if (inVar1->stride != inVar2->stride) return 0;
    //if (strcmp(inVar1->regName, inVar2->regName)!=0) return 1;
    return 1;
}
int cpyBIV(biv_p varDst, biv_p varSrc) {
    varDst->insn = varSrc->insn;
    cpyReg( &(varDst->initVal), varSrc->initVal );
    strcpy(varDst->opr,varSrc->opr);
    strcpy(varDst->regName,varSrc->regName);
    varDst->stride = varSrc->stride;
}
void printBIV(FILE *fp, biv_p biVar) {
    fprintf(fp," [%s,",biVar->regName);
    printReg(fp, biVar->initVal);
    fprintf(fp,",%d",biVar->stride);//, biVar->opr;
    fprintf(fp,"] ");fflush(fp);
}

/*** EXPRESSION VALUE OPERATION ***/
void printExpr(FILE* fp, expr_p expr) {
    int i;
    fprintf(fp,"\"");
    for (i=0; i<expr->varNum; i++) {
        fprintf(fp,"%d:",expr->coef[i]); 
        printReg(fp, expr->value[i]);
        fprintf(fp,"+");
    }
    fprintf(fp," %x ",expr->k);
    fprintf(fp,"\"");fflush(fp);
}

int computeExpr(char *op, expr_p exprD, expr_p expr1, expr_p expr2) {
    int dbg = 1;
    int i,j,k;
    expr_p  opr1, opr2, tailD, tmpNode;
    //reg_t   rD, r1, r2;

    int dis;    //distribution opr, e.g. * >>
    int inv;    //inversion: (expr1 - expr2) --> (expr2 - expr1) 
    int sll;    //shift left

    //initReg(&rD);initReg(&r1);initReg(&r2);
    dbg = dbg && strcmp(op,"<<")==0;
    if (dbg) {
        fprintf(dbgF,"\nCompute "); printExpr(dbgF,expr1);
        fprintf(dbgF," %s ",op); printExpr(dbgF,expr2);fflush(dbgF);
    }

    //check if operation requires distribution
    if (strcmp(op,"*")==0 || strcmp(op,">>")==0 || strcmp(op,"<<")==0) {
        if ( (expr1->varNum != 0 && expr2->varNum !=0) ) {
            // not support opr like (5:a + 7:1) * (2:b + 15:1) -> a*b
            printf("\nPanic, not support expr1 * expr2 for complex expr");
            if (dbg) {
                fprintf(stdout,"\nCompute "); printExpr(stdout,expr1);
                fprintf(stdout," %s ",op);printExpr(stdout,expr2);fflush(stdout);
            }
            return 0; //cannot compute
        }
        dis = 1;
    }
    else dis = 0;// + -
    for (i=0; i<expr1->varNum; i++) expr1->added[i]=0;
    for (i=0; i<expr2->varNum; i++) expr2->added[i]=0;
        
    inv = 0;
    if (dis) {
        if (expr1->varNum==0) { opr1 = expr2; opr2 = expr1; inv = 1; }
        else if (expr2->varNum==0) {opr1 = expr1; opr2 = expr2; }
        else {
            printf("\nPanic, not implement for 1:a * 2:b, must 1:a * k:1 ");
            return 0; //cannot compute
        }
    }
    else { 
        opr1 = expr1; opr2 = expr2;
    }

    cpyExpr(exprD, opr1);
    if (0) {
        fprintf(dbgF,"\neD = e1 =   "); printExpr(dbgF,exprD);fflush(dbgF);
    }
    if (dis) {//opr likes * , >> , << requires distribution
        if (strcmp(op,"<<")==0 && !inv ) sll = 1;
        else if (strcmp(op,"<<")==0 && inv ) sll = -1;
        else if (strcmp(op,">>")==0 && !inv ) sll = -1;
        else if (strcmp(op,">>")==0 && inv ) sll = 1;
        else sll = 0;//not >>  << 
        for (i=0; i<exprD->varNum; i++) {
            if (strcmp(op,"*")==0) exprD->coef[i] *= opr2->k;
            else if (strcmp(op,">>")==0 || strcmp(op,"<<")==0) {
                if (sll==1) 
                    for (k=0; k< opr2->k; k++) exprD->coef[i] *= 2;
                else if (sll==-1) 
                    for (k=0; k< opr2->k; k++) exprD->coef[i] /= 2;
                else {printf("\nPanic: ????");exit(1);}
            }
        }
        if (strcmp(op,"*")==0) exprD->k = exprD->k * opr2->k;
        else if (strcmp(op,"<<")==0 || strcmp(op,">>")==0) {
            if (sll==1) 
                for (k=0; k< opr2->k; k++) exprD->coef[i] *= 2;
            else if (sll==-1) 
                for (k=0; k< opr2->k; k++) exprD->coef[i] /= 2;
            else {printf("\nPanic: ????");exit(1);}
        }
        if (dbg){fprintf(dbgF,"\neD %s %d = ",op,opr2->k);printExpr(dbgF,exprD);}
        goto COMPUTE_EXPR_FIN;
    }
    //else opr likes + - 
    for (i=0; i<exprD->varNum; i++) {
        for (j=0; j<opr2->varNum; j++) {
            if ( regEq( exprD->value[i], opr2->value[j] )) {
                opr2->added[j] = 1;
                if (strcmp(op,"+")==0) 
                    exprD->coef[i] = exprD->coef[i] + opr2->coef[j];
                if (strcmp(op,"-")==0) 
                    exprD->coef[i] = exprD->coef[i] - opr2->coef[j];
                break;
            }
        }
    }
    if (0) {
        fprintf(dbgF,"\n eD after added elements in both E1 & E2:  "); 
        printExpr(dbgF,exprD);fflush(dbgF);
    }
    for (j=0; j<opr2->varNum; j++) {
        if (opr2->added[j]==0) {
            int varNum = exprD->varNum;
            if (varNum==MAX_EXPR_LEN) return 0;//too long, cannot compute
            exprD->coef[varNum] = opr2->coef[j];
            cpyReg(exprD->value+varNum, opr2->value[j]);
            exprD->varNum++;
        }
    }
    if (strcmp(op,"+")==0) exprD->k += opr2->k;
    else if (strcmp(op,"-")==0) exprD->k -= opr2->k;
    COMPUTE_EXPR_FIN:
    if (dbg) {fprintf(dbgF,"\n ==>  "); printExpr(dbgF,exprD);fflush(dbgF);}
    return 1;
}

int cpyExpr(expr_p dst, expr_p src) {
    int dbg = 0;
    int i;

    if (exprEq(dst,src)) return 0;//same expr -> no need to copy
    dst->varNum = src->varNum;
    dst->k = src->k;
    for (i=0; i<src->varNum; i++) {
        dst->coef[i] = src->coef[i];
        cpyReg(dst->value+i, src->value[i]);
    }
    if (dbg) {
        fprintf(dbgF,"\nCopy expr: Dst ");printExpr(dbgF,dst);
        fprintf(dbgF," <- Src ");printExpr(dbgF,src);
    }
    return 1;
}
int exprEq(expr_p expr1, expr_p expr2) {
    int i;
    if (expr1->varNum!=expr2->varNum) return 0;
    if (expr1->k != expr2->k) return 0;
    for (i=0; i<expr1->varNum; i++) {
        if (expr1->coef[i] != expr2->coef[i]) return 0;
        if (!regEq(expr1->value[i], expr2->value[i])) return 0;
    }
    return 1;
}
void clrExpr(expr_p expr) {
    int dbg = 1;
    int i;
    for (i=0; i<expr->varNum; i++) clrReg(expr->value+i);
    expr->varNum=0;
    expr->k = 0;
}
//convert reg to expr type
void initExpr(expr_p expr) {
    int i=0;
    for (i=0; i<MAX_EXPR_LEN; i++) initReg(expr->value+i);
    expr->varNum = 0;
}
expr_p createExpr() {
    expr_p expr;
    expr = calloc(1,sizeof(expr_s));
    initExpr(expr);
    return expr;
}
void reg2expr(reg_t *r) {
    expr_p expr;
    if (r->t == VALUE_EXPR) return;
    if (r->t==VALUE_CONST) {
        if (r->expr==NULL) r->expr = createExpr();
        expr = r->expr;
        expr->k = r->val;
        expr->varNum=0;
    }
    else {
        if (r->expr==NULL) r->expr = createExpr();
        expr = r->expr;
        cpyReg( &(expr->value[0]),*r);
        expr->k = 0;
        expr->varNum = 1;
        expr->coef[0] = 1;
    }
    r->t = VALUE_EXPR;
}
void setExpr(expr_p expr, reg_t r) {
    clrExpr(expr);
    if (r.t==VALUE_CONST) {
        expr->k = r.val;
        expr->varNum = 0;
    }
    else {
        expr->k = 0;
        cpyReg(expr->value+0,r);
        expr->varNum = 1;
        expr->coef[0] = 1;
    }
}

/*** ABSTRACT INTERPRETATION OPERATIONS ON REGISTER VALUE ***/

//compute representative value for reg.values from different paths
int mergeReg(reg_t *dstReg, reg_t srcReg, int isBackEdge) {
    int dbg = 0;
    int flag, changed;
    reg_t tmpReg;

    if ( regEq(srcReg,*dstReg)  ) return 0;     //Same value
    if ( dstReg->t==VALUE_UNPRED ) return 0;    //Already top
    if ( srcReg.t==VALUE_UNDEF ) return 0;      //Src is undefined

    if (dbg) {
        fprintf(dbgF,"\nMerge: ");
        printReg( dbgF, *dstReg ); fprintf(dbgF," <- ");
        printReg( dbgF, srcReg ); fprintf(dbgF," ");fflush(dbgF);
    }

    flag = 0;
    changed = 0;

    //scenarios where srcReg's value decides dstReg's value
    if ( srcReg.t==VALUE_UNPRED ) {
        regUnknown(dstReg); 
        if (0) fprintf(dbgF," src==T -> dst=T");
        changed = 1;goto FIN;
    }
    else if (dstReg->t!=VALUE_BIV && srcReg.t == VALUE_BIV) { //&& isBackEdge
        //initReg(&tmpReg);
        //cpyReg(&tmpReg,*dstReg);
        cpyReg(dstReg,srcReg);
        //updateInitVal(dstReg->biv,tmpReg);
        changed = 1;goto FIN;
    }

    switch (dstReg->t) {
        case VALUE_UNDEF:
            cpyReg(dstReg,srcReg);
            changed = 1;goto FIN;
            break;
        case VALUE_BIV:
            if (srcReg.t == VALUE_BIV) {
                //both dstReg & srcReg are both induction values
                if (regEq(srcReg.biv->initVal, *dstReg)) {
                    cpyReg(dstReg,srcReg);
                }
                else goto NO_SPECIAL;
            }
            else {
                /*when enter a loop, srcReg = initial constant*/
                flag = updateInitVal(dstReg->biv, srcReg);
                if (dbg) fprintf(dbgF," dstBIV->initVal = src"); 
                if (flag) changed = 1;
                goto FIN;
            }
            break;
        default: 
            NO_SPECIAL://no special relationship, not equal -> unpredictable
            if (!regEq(*dstReg,srcReg)) {
                regUnknown(dstReg);
                if (dbg) fprintf(dbgF," dst != src -> dst=T"); 
                changed = 1;goto FIN;
            }
            //else dstReg==srcReg -> do nothing
            return 0;
    }
    FIN:
    if (dbg) {fprintf(dbgF," ==> ");printReg(dbgF,*dstReg);}
    return changed;
}
    
int regOpr(char *op,reg_t *rD, reg_t r1, reg_t r2) {
    int dbg = 0;
    int flag;
    FILE *dbgF = stdout;
    if (r1.t==VALUE_UNDEF || r2.t==VALUE_UNDEF) {clrReg(rD); return 1;}
    //if (r1.t==VALUE_UNPRED|| r2.t==VALUE_UNPRED) {regUnknown(rD); return 1;}
    if( dbg ) { 
        fprintf(dbgF,"\nOpr "); printReg(dbgF,r1);fprintf(dbgF," %s",op);
        fprintf(dbgF," "); printReg(dbgF,r2);fflush(dbgF);
    } 

    if (r1.t==VALUE_CONST && r1.t==r2.t) {
        if (strcmp(op,"+")==0)      setInt(rD, r1.val + r2.val);
        else if (strcmp(op,"-")==0) setInt(rD, r1.val - r2.val);
        else if (strcmp(op,"*")==0) setInt(rD, r1.val * r2.val );
        else if (strcmp(op,"|")==0) setInt(rD, r1.val | r2.val);
        else if (strcmp(op,"^")==0) setInt(rD, r1.val ^ r2.val);
        else if (strcmp(op,"&")==0) setInt(rD, r1.val & r2.val);
        else if (strcmp(op,"<<")==0)setInt(rD, r1.val << r2.val);
        else if (strcmp(op,">>")==0)setInt(rD, r1.val >> r2.val);
        else {if (1) printf("\nNot implemented opr 1 %s",op);regUnknown(rD);}
    }
    else if  (r1.t==VALUE_CONST && r1.val==0) { cpyReg(rD,r2); }
    else if  (r2.t==VALUE_CONST && r2.val==0) { cpyReg(rD,r1); }
    else {
        expr_p opr1, opr2;
        expr_s expr1, expr2;
        if (r1.t!=VALUE_EXPR) {
            initExpr(&expr1);
            setExpr(&expr1,r1);
            opr1=&expr1;
        }
        else {
            opr1 = r1.expr; 
        }
        if (r2.t!=VALUE_EXPR) {
            initExpr(&expr2);setExpr(&expr2,r2);
            opr2=&expr2;
        }
        else {
            opr2 = r2.expr;
        }
        if (rD->expr==NULL) rD->expr = createExpr();
        if (dbg) {fprintf(dbgF,"\nE1: "); printExpr(dbgF,opr1);fflush(dbgF);}
        if (dbg) {fprintf(dbgF,"\nE2: "); printExpr(dbgF,opr2);fflush(dbgF);}
       
        if (strcmp(op,"+")==0 || strcmp(op,"-")==0
            || strcmp(op,"*")==0 || strcmp(op,">>")==0 || strcmp(op,"<<")==0 ){
            rD->t = VALUE_EXPR;
            flag = computeExpr(op, rD->expr, opr1, opr2);
            if (flag == 0) regUnknown(rD);
        }
        else {
            if (1) printf("\nNot implemented opr 2 %s",op); 
            regUnknown(rD);
        }
        if (r1.t!=VALUE_EXPR) clrExpr(&expr1);
        if (r2.t!=VALUE_EXPR) clrExpr(&expr2);
    }
    OPR_FIN:
    if (dbg) {fprintf(dbgF,"==> "); printReg(dbgF,*rD);fprintf(dbgF,"\n");}
    return 1;
}
