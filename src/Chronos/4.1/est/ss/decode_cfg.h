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
 * $Id: decode_cfg.h,v 1.1.1.1 2006/02/11 22:14:35 lixianfe Exp $
 *
 ******************************************************************************/


#ifndef DECODE_CFG_H
#define DECODE_CFG_H


/*
 * configure the instruction decode engine
 */

#define DNA			(0)

#if defined(TARGET_PISA)

/* general register dependence decoders */
#define DGPR(N)			(N)
#define DGPR_D(N)		((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)		(((N)+32)&~1)
#define DFPR_F(N)		(((N)+32)&~1)
#define DFPR_D(N)		(((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI			(0+32+32)
#define DLO			(1+32+32)
#define DFCC			(2+32+32)
#define DTMP			(3+32+32)

#elif defined(TARGET_ALPHA)

/* general register dependence decoders, $r31 maps to DNA (0) */
#define DGPR(N)			(31 - (N)) /* was: (((N) == 31) ? DNA : (N)) */

/* floating point register dependence decoders */
#define DFPR(N)			(((N) == 31) ? DNA : ((N)+32))

/* miscellaneous register dependence decoders */
#define DFPCR			(0+32+32)
#define DUNIQ			(1+32+32)
#define DTMP			(2+32+32)

#else
#error No ISA target defined...
#endif

#endif
