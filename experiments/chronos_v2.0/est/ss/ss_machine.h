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
 * $Id: ss_machine.h,v 1.2 2006/06/24 08:55:06 lixianfe Exp $
 *
 ****************************************************************************/

#ifndef SS_MACHINE_H
#define SS_MaCHINE_H

#include "../common.h"


enum { STAGE_IF, STAGE_ID, STAGE_EX, STAGE_WB, STAGE_CM };

/* phisical function unit classes: multiple fu map to the same phisical fu */
enum ss_pfu_class {
    P_FUClass_NA = 0,
    P_IntALU,
    P_Int_Mult_Div,
    P_Mem_Port,
    P_FP_Adder,
    P_FP_Mult_Div,
    NUM_PFU_CLASSES
};



#endif
