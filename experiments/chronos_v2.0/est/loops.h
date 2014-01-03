
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
 * $Id: loops.h,v 1.2 2006/06/24 08:54:57 lixianfe Exp $
 *
 ******************************************************************************/


#ifndef LOOPS_H
#define LOOPS_H


#include "tcfg.h"


#define MAX_LOOP_NEST	32

#define LOOP_ENTRY	0
#define LOOP_EXIT	1


// data structure for loop information
typedef struct loop_t	loop_t;

struct loop_t {
    int		    id;
    tcfg_node_t	    *head;	// [head, tail]
    tcfg_node_t	    *tail;
    loop_t	    *parent;
    tcfg_elink_t    *exits;
    int		    flags;
};



#endif
