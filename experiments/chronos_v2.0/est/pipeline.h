/*****************************************************************************
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
 * $Id: pipeline.h,v 1.2 2006/06/24 08:54:57 lixianfe Exp $
 *
 ****************************************************************************/

#ifndef PIPE_LINE_H
#define PIPE_LINE_H

#include "common.h"


#define BODY_CODE   0
#define PLOG_CODE   1
#define ELOG_CODE   2

#define MAX_SSCALAR 4


// microarchitecture-state annotated instruction type
typedef struct {
    de_inst_t	*inst;
    short	bbi_id;
    short	mblk_id;
    short	bp_flag;
    short	ic_flag;
} mas_inst_t;


typedef struct code_link_t code_link_t;

struct code_link_t {
    mas_inst_t	*code;
    int		num_inst;
    code_link_t	*next;
};


static void
dump_xlogs();
void
dump_units_times();
void
dump_mp_times();
void
dump_plog_stats();
void
dump_elog_stats();
void
dump_context_stats();
void
dump_elog_len();
void
dump_mlat_mpinst();


#endif
