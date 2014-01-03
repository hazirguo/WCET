/*******************************************************************************
 *
 * Chronos: A Timing Analyzer for Embedded Software
 * =============================================================================
 * http://www.comp.nus.edu.sg/~rpembed/chronos/
 *
 * Copyright (C) 2005 Xianfeng Li
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
 *
 * $Id: common.c,v 1.2 2006/06/24 08:54:56 lixianfe Exp $
 *
 ******************************************************************************/

#include "common.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

int hexValue( char *hexStr ) {
    int val;
    sscanf( hexStr, "%x", &val );
    return val;
}

ticks getticks() {
  unsigned a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((ticks)a) | (((ticks)d) << 32);
}

/* binary search, if not match, return the entry where key will be inserted in front
 */
const void *
my_bsearch(const void *key, const void *base, size_t n, size_t size,
	int (*cmp)(const void *k, const void *datum))
{
    int		low, high, mid;
    int		c;

    if (n == 0)
	return base;

    low = 0; high = n - 1;
    while (low <= high) {
	mid = (low + high) / 2;
	if (cmp == NULL)
	    c = (*((int *) key) - *((int *) (base + mid * size)));
	else
	    c = cmp(key, (const void *)(base + mid * size));
	if (c < 0)
	    high = mid - 1;
	else if (c > 0)
	    low = mid + 1;
	else
	    return (const void *)(base + mid * size);

    }
    return  (const void *)(base + low * size);
}



/* insert x into base in front of y. nelem is the current number of elements in base
 * and size is the size of each element */
void
my_insert(const void *x, void *base, void *y, int *nelem, int size)
{
    int	    nbytes;
    void    *p;

    nbytes = (*nelem) * size - ((int)y - (int)base);
    assert(nbytes >= 0);
    memmove(y+size, y, nbytes);
    memcpy(y, x, size);
    (*nelem)++;
}



/* stack operations */

void
init_stack(Stack *stack, int elem_size)
{
    stack->base = (void *) malloc(STACK_ELEMS * elem_size);
    if (stack->base == NULL) {
	fprintf(stderr, "out of memory (__FILE__:__LINE__)\n");
	exit(1);
    }
    stack->esize = elem_size;
    stack->capt = STACK_ELEMS * elem_size;
    stack->top = stack->base;
}



void
free_stack(Stack *stack)
{
    if (stack->base != NULL) {
	free(stack->base);
	stack->base = NULL;
    }
    stack->capt = stack->esize = 0;
    stack->top = NULL;
}



void
stack_push(Stack *stack, void *x)
{
    if ((stack->top + stack->esize) > (stack->base + stack->capt)) {
	stack->capt *= 2;
	stack->base = (void *) realloc(stack->base, stack->capt);
	if (stack->base == NULL) {
	    fprintf(stderr, "out of memory (__FILE__:__LINE__)\n");
	    exit(1);
	}
    }
    memcpy(stack->top, x, stack->esize);
    stack->top += stack->esize;
}



int
stack_empty(Stack *stack)
{
    assert(stack->top >= stack->base);
    return (stack->top == stack->base) ? 1 : 0;
}



void *
stack_pop(Stack *stack)
{
    if (stack_empty(stack))
	return NULL;
    stack->top -= stack->esize;
    return stack->top;
}



/* clear elements to make stack empty, but don't free memory */
void
clear_stack(Stack *stack)
{
    stack->top = stack->base;
}



/* copy contents of stack x to stack y; y must have been initiated and not freed */
void
copy_stack(Stack *y, Stack *x)
{
    /* first free stack y */
    free_stack(y);
    
    y->base = (void *) malloc(x->capt);
    if (y->base == NULL) {
	fprintf(stderr, "out of memory (__FILE__:__LINE__)\n");
	exit(1);
    }
    memcpy(y->base, x->base, (x->top - x->base));
    y->esize = x->esize;
    y->capt = x->capt;
    y->top = y->base + (x->top - x->base);
}



/* queue operations */
void
init_queue(Queue *queue, int elem_size)
{
    queue->base = (void *) malloc(QUEUE_ELEMS * elem_size);
    if (queue->base == NULL) {
	fprintf(stderr, "out of memory (__FILE__:__LINE__)\n");
	exit(1);
    }
    queue->esize = elem_size;
    queue->capt = QUEUE_ELEMS * elem_size;
    queue->head = queue->tail = queue->base;
}



void
free_queue(Queue *queue)
{
    if (queue->base != NULL) {
	free(queue->base);
	queue->base = NULL;
    }
    queue->capt = queue->esize = 0;
    queue->head = queue->tail = NULL;
}



void
enqueue(Queue *queue, void *x)
{
    void    *newbase;
    int	    n;

    memcpy(queue->tail, x, queue->esize);
    queue->tail += queue->esize;
    if (queue->tail == (queue->base + queue->capt))
	queue->tail = queue->base;
    if (queue->tail != queue->head) /* queue not full, done */
	return;

    /* queue is full, allocate more memory and move data */
    newbase = (void *) malloc(queue->capt * 2);
    if (newbase == NULL) {
	fprintf(stderr, "out of memory (__FILE__:__LINE__)\n");
	exit(1);
    }
    n = queue->base + queue->capt - queue->head;
    memcpy(newbase, queue->head, n);
    if (queue->tail > queue->base)
	memcpy(newbase + n, queue->base, queue->tail - queue->base);
    free(queue->base);
    queue->base = newbase;
    queue->head = queue->base;
    queue->tail = queue->base + queue->capt;
    queue->capt *= 2;
}



int
queue_empty(Queue *queue)
{
    return (queue->head == queue->tail) ? 1 : 0;
}



void *
dequeue(Queue *queue)
{
    void    *p;
    if (queue_empty(queue))
	return NULL;
    p = queue->head;
    queue->head += queue->esize;
    if (queue->head == (queue->base + queue->capt))
	queue->head = queue->base;
    return p;
}



/* just clear elements to queue empty, but don't free memory */
void
clear_queue(Queue *queue)
{
    queue->head = queue->tail = queue->base;
}



int
bits(unsigned x)
{
    int	    i = 0;

    if (x == 0)
	return 0;
    while (x > 0) {
	i++;
	x >>= 1;
    }
    return i;
}



int
range_isect(range_t *x, range_t *y)
{
    if (x->lo < y->lo)
	x->lo = y->lo;
    if (x->hi > y->hi)
	x->hi = y->hi;
    if (x->lo > x->hi)
	return BAD_RANGE;
    else
	return GOOD_RANGE;
}



// union operation
void
range_union(range_t *x, range_t *y)
{
    if (x->lo > y->lo)
	x->lo = y->lo;
    if (x->hi < y->hi)
	x->hi = y->hi;
}

//priority queue impl
void p_enqueue(P_Queue **headList, void *newItem, int key) {
    P_Queue *cur, *prev, *qElem;
    qElem = (P_Queue*) malloc(sizeof(P_Queue));
    qElem->next = NULL;
    qElem->key = key;
    qElem->elem = newItem;

    cur = *headList; prev = NULL;
    while ( cur!=NULL && (cur->key < key) ) {
        //printf(" (%d<%d) ",cur->key, key);fflush(stdout);
        prev = cur;
        cur = cur->next;
    }
    if (prev!=NULL) {
        prev->next = qElem;
        qElem->next = cur;
    }
    else {//prev == NULL
        qElem->next = (*headList);
        (*headList) = qElem;
    }
}
void* p_dequeue(P_Queue **headList) {
    void *result;
    P_Queue* item;
    if ( (*headList)==NULL ) {
        printf("\nQueue empty");fflush(stdout);
        return NULL;
    }
    item = *headList;
    *headList = (*headList)->next;
    result = item->elem;
    free(item);
    return result;
}
int p_queue_empty(P_Queue **headList) {
    return ( (*headList)==NULL );
}

/* Remove one item from the worklist */
void* removeOneFromWorkList(worklist_p* Wlist)
{
	worklist_p temp = *Wlist;
	*Wlist = (*Wlist)->next;
	
	return temp->data;
}

/* Add one item to the worklist */
void addToWorkList(worklist_p* Wlist, void* data)
{
	worklist_p temp = (worklist_p)malloc(sizeof(worklist_s));
    temp->next = NULL;
	temp->data = data;
	temp->next = *Wlist;
	*Wlist = temp;
}

/* Check for empty worklist */
int isEmpty(worklist_p Wlist)
{
	return (Wlist == NULL);
}
void addAfterNode(void *data, worklist_p *prvNode, worklist_p *headNode) {
    worklist_p newNode;
    newNode = (worklist_p) malloc(sizeof(worklist_s));
    newNode->data = data;
    newNode->next = NULL;
    if (*prvNode) {
        newNode->next = (*prvNode)->next;
        (*prvNode)->next = newNode;
        (*prvNode) = newNode;
    }
    else {//*prvNode == NULL
        newNode->next = *headNode;
        *headNode = newNode;
        *prvNode = newNode;
    }
}
void remAfterNode(worklist_p *prvNode, worklist_p *headNode) {
    worklist_p tmpNode;
    if ((*prvNode)!=NULL) {
        tmpNode = (*prvNode)->next;
        if (tmpNode) {
            (*prvNode)->next = tmpNode->next;
            free(tmpNode->data);/*WARNING: memory leak*/
            free(tmpNode);
        }
        else {
            //do nothing, prvNode->next is already NULL
        }
    }
    else {//*prvNode==NULL-> remove headNode
        tmpNode = (*headNode);
        if (tmpNode) {
            (*headNode) = (*headNode)->next;
            free(tmpNode->data);/*WARNING: memory leak*/
            free(tmpNode);
        }
        else {
            //do nothing, headNode is already NULL
        }
    }
}
void freeList(worklist_p *Wlist) {
    worklist_p sNode, prvNode;
    prvNode = NULL;
    sNode = *Wlist;
    while (sNode) {
        prvNode = sNode;
        sNode = sNode->next;
        free(prvNode);
    }
    (*Wlist) = NULL;
}


int isDecNum(char c) {
    return ('0'<=c && c<='9');
}
int isHexNum(char c) {
    return ( ('0'<=c && c<='9') || (c=='x')
                || ('a'<=c && c<='f') || ('A'<=c && c<='F') );
}
int getNextElem(char *str, int *pos, char *token) {
    int i;
    token[0] = '\0';
    i=0;
    while (str[*pos]!='\0') {
        /*bracelet*/
        if (str[*pos]=='(') {
            *pos = *pos+1;
            strcpy(token,"(");
            return 1;
        }
        else if (str[*pos]==')') {
            *pos = *pos+1;
            strcpy(token,")");
            return 1;
        }
        /*induction register*/
        else if (str[*pos]=='$') {
            token[i++]=str[*pos];
            *pos = *pos+1;
            while (isDecNum(str[*pos])) {
                token[i++]=str[*pos];
                *pos = *pos+1;
                if (str[*pos]=='\0') break;
            }
            token[i]='\0';
            return 1;
        }
        /*numerical value, 0x1234abcdef*/
        else if ( isHexNum(str[*pos]) ) {
            while( isHexNum(str[*pos]) ) {
                token[i++]=str[*pos];
                *pos = *pos+1;
                if (str[*pos]=='\0') break;
            }
            token[i]='\0';
            return 1;
        }
        else if (str[*pos]=='T') {//unknown var
            token[i++]=str[*pos];
            token[i]='\0';
            *pos = *pos+1;
            return 1; 
        }
        /*oprator, hopefully*/
        else if (str[*pos]!=' ') {
            while (str[*pos]!='$' && !isHexNum(str[*pos]) && str[*pos]!='T' 
                    && str[*pos]!='(' && str[*pos]!=')' && str[*pos]!=' ') {
                token[i++]=str[*pos];
                *pos = *pos+1;
                if (str[*pos]=='\0') break;
            }
            token[i]='\0';
            return 1;
        }
        //blank space
        else *pos = *pos + 1;
    }
    return 0;
}

int getNextToken(char *token, char *str, char *pos, char *delim) {
    char *pch; 
    int len, found;
    token[0]='\0';
    len = 0;
    found = 0;
    while (str[*pos]!='\0') {
        if (strchr(delim,str[*pos])!=NULL) {
            token[len++]='\0';
            while (strchr(delim,str[*pos])!=NULL) {
                *pos = *pos + 1;
            }
            return found;
        }
        else {
            token[len] = str[*pos];
            len++;
            found = 1;
            *pos = *pos + 1;
        }
    }
    token[len++]='\0';
    return found;
}
