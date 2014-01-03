/*
 * ecoff.h - SimpleScalar ECOFF definitions
 *
 * This file is a part of the SimpleScalar tool suite written by
 * Todd M. Austin as a part of the Multiscalar Research Project.
 *  
 * The tool suite is currently maintained by Doug Burger and Todd M. Austin.
 * 
 * Copyright (C) 1994, 1995, 1996, 1997 by Todd M. Austin
 *
 * This source file is distributed "as is" in the hope that it will be
 * useful.  The tool set comes with no warranty, and no author or
 * distributor accepts any responsibility for the consequences of its
 * use. 
 * 
 * Everyone is granted permission to copy, modify and redistribute
 * this tool set under the following conditions:
 * 
 *    This source code is distributed for non-commercial use only. 
 *    Please contact the maintainer for restrictions applying to 
 *    commercial use.
 *
 *    Permission is granted to anyone to make or distribute copies
 *    of this source code, either as received or modified, in any
 *    medium, provided that all copyright notices, permission and
 *    nonwarranty notices are preserved, and that the distributor
 *    grants the recipient permission for further redistribution as
 *    permitted by this document.
 *
 *    Permission is granted to distribute this file in compiled
 *    or executable form under the same conditions that apply for
 *    source code, provided that either:
 *
 *    A. it is accompanied by the corresponding machine-readable
 *       source code,
 *    B. it is accompanied by a written offer, with no time limit,
 *       to give anyone a machine-readable copy of the corresponding
 *       source code in return for reimbursement of the cost of
 *       distribution.  This written offer must permit verbatim
 *       duplication by anyone, or
 *    C. it is distributed by someone who received only the
 *       executable form, and is accompanied by a copy of the
 *       written offer of source code that they received concurrently.
 *
 * In other words, you are welcome to use, share and improve this
 * source file.  You are forbidden to forbid anyone else to use, share
 * and improve what you give them.
 *
 * INTERNET: dburger@cs.wisc.edu
 * US Mail:  1210 W. Dayton Street, Madison, WI 53706
 *
 * $Id: ecoff.h,v 1.1.1.1 2006/06/17 00:58:23 lixianfe Exp $
 *
 * $Log: ecoff.h,v $
 * Revision 1.1.1.1  2006/06/17 00:58:23  lixianfe
 * chronos 2.0
 *
 * Revision 1.1.1.1  2006/02/11 22:14:35  lixianfe
 * 2006/02/13
 *
 * Revision 1.1.1.1  2005/08/26 02:45:21  lixianfe
 * import of wcet est
 *
 * Revision 1.1.1.1  2005/07/03 17:41:37  lixianfe
 * thesis experiments
 *
 * Revision 1.1.1.1  2005/06/04 15:00:14  lixianfe
 * PhD thesis experiments
 *
 * Revision 1.1.1.1  2004/02/05 07:20:16  lixianfe
 * Initial version, simulate/esitmate straigh-line code
 *
 * Revision 1.1  1997/04/16  22:13:35  taustin
 * Initial revision
 *
 *
 */

/* SimpleScalar ECOFF definitions */

/* ECOFF contents
 *
 * +-------------------+
 * | file header       |
 * +-------------------+
 * | a.out header      |
 * +-------------------+
 * | section headers   |
 * +-------------------+
 * | raw data sections |
 * +-------------------+
 * | relocations       |
 * +-------------------+
 * | symbol table      |
 * +-------------------+
 * | comment section   |
 * +-------------------+

*/

#ifndef ECOFF_H
#define ECOFF_H

#define  ECOFF_EB_MAGIC     0x0160
#define  ECOFF_EL_MAGIC     0x0162

struct ecoff_filehdr {
  unsigned short f_magic;
  unsigned short f_nscns;
  int f_timdat;
  int f_symptr;
  int f_nsyms;
  unsigned short f_opthdr;
  unsigned short f_flags;
};

struct ecoff_aouthdr {
  short magic;
  short vstamp;
  int tsize;
  int dsize;
  int bsize;
  int entry;
  int text_start;
  int data_start;
  int bss_start;
  int gprmask;
  int cprmask[4];
  int gp_value;
};

struct ecoff_scnhdr {
  char s_name[8];
  int s_paddr;
  int s_vaddr;
  int s_size;
  int s_scnptr;
  int s_relptr;
  int s_lnnoptr;
  unsigned short s_nreloc;
  unsigned short s_nlnno;
  int s_flags;
};

typedef struct ecoff_symhdr_t {
  short magic;
  short vstamp;
  int ilineMax;
  int cbLine;
  int cbLineOffset;
  int idnMax;
  int cbDnOffset;
  int ipdMax;
  int cbPdOffset;
  int isymMax;
  int cbSymOffset;
  int ioptMax;
  int cbOptOffset;
  int iauxMax;
  int cbAuxOffset;
  int issMax;
  int cbSsOffset;
  int issExtMax;
  int cbSsExtOffset;
  int ifdMax;
  int cbFdOffset;
  int crfd;
  int cbRfdOffset;
  int iextMax;
  int cbExtOffset;
} ecoff_HDRR;

#define ECOFF_magicSym 0x7009

typedef struct ecoff_fdr {
  unsigned int adr;
  int rss;
  int issBase;
  int cbSs;
  int isymBase;
  int csym;
  int ilineBase;
  int cline;
  int ioptBase;
  int copt;
  unsigned short ipdFirst;
  unsigned short cpd;
  int iauxBase;
  int caux;
  int rfdBase;
  int crfd;
  unsigned lang :5;
  unsigned fMerge :1;
  unsigned fReadin :1;
  unsigned fBigendian :1;
  unsigned reserved :24;
  int cbLineOffset;
  int cbLine;
} ecoff_FDR;

typedef struct ecoff_pdr {
  unsigned int adr;
  int isym;
  int iline;
  int regmask;
  int regoffset;
  int iopt;
  int fregmask;
  int fregoffset;
  int frameoffset;
  short framereg;
  short pcreg;
  int lnLow;
  int lnHigh;
  int cbLineOffset;
} ecoff_PDR;

typedef struct ecoff_SYMR {
  int iss;
  int value;
  unsigned st :6;
  unsigned sc :5;
  unsigned reserved :1;
  unsigned index :20;
} ecoff_SYMR;

typedef struct ecoff_EXTR {
  short reserved;
  short ifd;
  ecoff_SYMR asym;
} ecoff_EXTR;

#define ECOFF_R_SN_TEXT		1
#define ECOFF_R_SN_RDATA	2
#define ECOFF_R_SN_DATA		3
#define ECOFF_R_SN_SDATA	4
#define ECOFF_R_SN_SBSS		5
#define ECOFF_R_SN_BSS		6

#define ECOFF_STYP_TEXT		0x0020
#define ECOFF_STYP_RDATA	0x0100
#define ECOFF_STYP_DATA		0x0040
#define ECOFF_STYP_SDATA	0x0200
#define ECOFF_STYP_SBSS		0x0400
#define ECOFF_STYP_BSS		0x0080

#define ECOFF_stNil		0
#define ECOFF_stGlobal		1
#define ECOFF_stStatic		2
#define ECOFF_stParam		3
#define ECOFF_stLocal		4
#define ECOFF_stLabel		5
#define ECOFF_stProc		6
#define ECOFF_stBlock		7
#define ECOFF_stEnd		8
#define ECOFF_stMember		9
#define ECOFF_stTypedef		10
#define ECOFF_stFile		11
#define ECOFF_stRegReloc	12
#define ECOFF_stForward		13
#define ECOFF_stStaticProc	14
#define ECOFF_stConstant	15

#endif /* ECOFF_H */
