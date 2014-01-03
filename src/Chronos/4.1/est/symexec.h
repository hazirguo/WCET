/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Symbolic execution of binary code
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
 * 03/2007 symexec.h
 *
 *  v4.1:
 *      _ completely rewrite the symbolic execution framework
 *          + intraprocedure execution, instead of intra basic block
 *          + memory modeling -> can trace value when saved to memory
 *          + richer value types: induction, expression, parameter
 *      _ temporary disable conflict pair detection
 *
 *  v4.0: as in Chronos-4.0 distribution
 *
 *
 ******************************************************************************/
#ifndef SYM_EXEC_H
#define SYM_EXEC_H
#include "common.h"

#define NO_REG 32

/*** REGISTER MODELING FRAMEWORK ***/
#define DERI_LEN 128   // length of register derivation tree
#define VALUE_UNDEF     0
#define VALUE_CONST     1
#define VALUE_PARA      2   /*unknown parameter*/
#define VALUE_EXPR      3
#define VALUE_BIV       4
#define VALUE_UNPRED    VALUE_PARA /*unpredictable value*/

typedef struct linear_expr *   expr_p;
typedef struct BIV *         biv_p;

//NOTE: avoid using dynamic allocation, the source of all errors
typedef struct {
  char      name[4];
  char      t;       // value type: expression, const, induction, parameter
  int       val;
  expr_p    expr; 
  biv_p     biv;
  char      para[4];
  int       flag;
} reg_t;

void    setInt(reg_t *reg, int k);
void    initReg(reg_t *reg);
void    clrReg(reg_t *reg);
void    regUnknown(reg_t *reg);
int     regEq( reg_t reg1, reg_t reg2 ); 
int     cpyReg( reg_t *dst, reg_t src);

void    printReg(FILE *fp, reg_t reg);
int     findReg(reg_t *regList, char regName[] );

int     initRegSet(reg_t *regList);
int     clearRegList(reg_t *regList);

/*** ABSTRACT INTERPRETATION OF REG_VALUE ***/
int     mergeReg(reg_t *dstReg,reg_t srcReg,int isBackEdge);//merge abs.reg value
int     regOpr(char *op, reg_t *rD, reg_t r1, reg_t r2); //abs.opr on r.values

/*** PARAMETER VALUE TYPE ***
 *  Parameter value is dynamic value, statically unpredictable
 *  Denote as a string V<id>, same para -> same string
 *  For now, all parameters are T (unpredictable), no interpretation
 */
void setNewPara(char *para);

/*** INDUCTION VALUE TYPE ***
 *  Induction value is a type of register value
 *  Basic induction value (BIV):  (name, init, opr, stride)
 *  $i = $i opr k, opr: +,-,*,>>
 *  General induction value is an expression of basic induction value
 */
#define BIV_SAVED 0x1
struct BIV {
    void    *insn;          //instruction performs inductive operation 
    //int     lpId;           //loop where induction
    reg_t   initVal;
    char    opr[8];         //inductive operation, e.g + - * / >>
    int     stride;
    char    regName[10];   
    int     flag;
};
typedef struct BIV  biv_s;
//typedef struct BIV* biv_p;

int     updateInitVal(biv_p biVar, reg_t initVal);
int     bivEq(biv_p inVar1, biv_p inVar2);
int     cpyBIV(biv_p varDst, biv_p varSrc);
void    printBIV(FILE *fp, biv_p biVar);


/*** EXPRESSION VALUE OPERATION ***
 *  Expression value is a polynomial expression C*v
 *  C = coefficient constant, v = parameter / constant
 */
#define MAX_EXPR_LEN 3 /* max length: c1:v1 + .. + c4:v4 + K:1 */
struct linear_expr {
    reg_t   value[MAX_EXPR_LEN];  //restrict to CONST / INDUCTION / PARA
    int     coef[MAX_EXPR_LEN];
    int     added[MAX_EXPR_LEN];  //added=0: not processed, added=1: added
    int     k;       
    int     varNum;
};

typedef struct linear_expr    expr_s;
//typedef struct poly_expr*   expr_p;

void    printExpr(FILE* fp, expr_p expr);
int     computeExpr(char *op, expr_p exprD, expr_p expr1, expr_p expr2);
int     cpyExpr(expr_p exprDst, expr_p exprSrc);
void    reg2expr(reg_t *r);
void    setExpr(expr_p expr, reg_t r);
void    clrExpr(expr_p expr);
int     exprEq(expr_p expr1, expr_p expr2);
void    initExpr(expr_p expr);


/*** MEMORY MODELING FRAMEWORK ***/
/* Model memory as a list of memory nodes 
 * Each node is a tuple < instAddr,writeAddr,writeValue >
 */
struct sym_memory_model {
    long    instAddr;               /*inst assigns value for this entry*/
    reg_t   writeAddr;              /*addr value of this entry*/
    reg_t   regValue;               /*reg. value saved in this entry*/
};
typedef struct sym_memory_model mem_s;
typedef struct sym_memory_model* mem_p;

/*** DATA CACHE INSTRUCTION TYPE ***/
typedef struct {
    void        *insn;      
    expr_s      addrExpr;   /*Address expression */
    worklist_p  addr_set;   /*ScopeMem accessed in increasing order*/
    int         max_miss;   /*Maximum estimate cache misses*/
    int         max_exec;   /*Maximum number of executions*/
    int         resideLpId; /*Loop where this data reference reside*/
} dat_inst_t;

/*Return loopId where biv resides in*/
int     biv2LoopId(biv_p biv);
/*Expand address expression from biv format to normal expression format*/
void    expandAddrExpr(expr_p exp, reg_t *addrExpr);
void    printDataRef(FILE *fp, dat_inst_t* datInst);
int     getAddrD(dat_inst_t* datInst);
#endif
