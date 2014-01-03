/* loader.c - program loader routines */

/* SimpleScalar(TM) Tool Suite
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 * All Rights Reserved. 
 * 
 * THIS IS A LEGAL DOCUMENT, BY USING SIMPLESCALAR,
 * YOU ARE AGREEING TO THESE TERMS AND CONDITIONS.
 * 
 * No portion of this work may be used by any commercial entity, or for any
 * commercial purpose, without the prior, written permission of SimpleScalar,
 * LLC (info@simplescalar.com). Nonprofit and noncommercial use is permitted
 * as described below.
 * 
 * 1. SimpleScalar is provided AS IS, with no warranty of any kind, express
 * or implied. The user of the program accepts full responsibility for the
 * application of the program and the use of any results.
 * 
 * 2. Nonprofit and noncommercial use is encouraged. SimpleScalar may be
 * downloaded, compiled, executed, copied, and modified solely for nonprofit,
 * educational, noncommercial research, and noncommercial scholarship
 * purposes provided that this notice in its entirety accompanies all copies.
 * Copies of the modified software can be delivered to persons who use it
 * solely for nonprofit, educational, noncommercial research, and
 * noncommercial scholarship purposes provided that this notice in its
 * entirety accompanies all copies.
 * 
 * 3. ALL COMMERCIAL USE, AND ALL USE BY FOR PROFIT ENTITIES, IS EXPRESSLY
 * PROHIBITED WITHOUT A LICENSE FROM SIMPLESCALAR, LLC (info@simplescalar.com).
 * 
 * 4. No nonprofit user may place any restrictions on the use of this software,
 * including as modified by the user, by any other authorized user.
 * 
 * 5. Noncommercial and nonprofit users may distribute copies of SimpleScalar
 * in compiled or executable form as set forth in Section 2, provided that
 * either: (A) it is accompanied by the corresponding machine-readable source
 * code, or (B) it is accompanied by a written offer, with no time limit, to
 * give anyone a machine-readable copy of the corresponding source code in
 * return for reimbursement of the cost of distribution. This written offer
 * must permit verbatim duplication by anyone, or (C) it is distributed by
 * someone who received only the executable form, and is accompanied by a
 * copy of the written offer of source code.
 * 
 * 6. SimpleScalar was developed by Todd M. Austin, Ph.D. The tool suite is
 * currently maintained by SimpleScalar LLC (info@simplescalar.com). US Mail:
 * 2395 Timbercrest Court, Ann Arbor, MI 48105.
 * 
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 */


#include <stdio.h>
#include <stdlib.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "loader.h"

#include "ecoff.h"

/* amount of tail padding added to all loaded text segments */
#define TEXT_TAIL_PADDING 128

/* program text (code) segment base */
extern md_addr_t ld_text_base;

/* program text (code) size in bytes */
extern unsigned int ld_text_size;

/* program entry point (initial PC) */
md_addr_t ld_prog_entry = 0;



/* load program text and initialized data into simulated virtual memory
   space and initialize program segment range variables */
void
ld_load_prog(char *fname)		
{
    int i;
    FILE *fobj;
    long floc;
    struct ecoff_filehdr fhdr;
    struct ecoff_aouthdr ahdr;
    struct ecoff_scnhdr shdr;

    fobj = fopen(fname, "r");
    if (!fobj)
	fatal("cannot open executable `%s'", fname);

    if (fread(&fhdr, sizeof(struct ecoff_filehdr), 1, fobj) < 1)
	fatal("cannot read header from executable `%s'", fname);

    if (fread(&ahdr, sizeof(struct ecoff_aouthdr), 1, fobj) < 1)
	fatal("cannot read AOUT header from executable `%s'", fname);

    /* seek to the beginning of the first section header, the file header comes
       first, followed by the optional header (this is the aouthdr), the size
       of the aouthdr is given in Fdhr.f_opthdr */
    fseek(fobj, sizeof(struct ecoff_filehdr) + fhdr.f_opthdr, 0);

    debug("processing %d sections in `%s'...", fhdr.f_nscns, fname);

    /* loop through the section headers */
    floc = ftell(fobj);
    for (i = 0; i < fhdr.f_nscns; i++)
    {
	char *p;
	if (fseek(fobj, floc, 0) == -1)
	    fatal("could not reset location in executable");
	if (fread(&shdr, sizeof(struct ecoff_scnhdr), 1, fobj) < 1)
	    fatal("could not read section %d from executable", i);
	floc = ftell(fobj);

	switch (shdr.s_flags) {
	case ECOFF_STYP_TEXT:
	    ld_text_size = ((shdr.s_vaddr + shdr.s_size) - MD_TEXT_BASE) 
		+ TEXT_TAIL_PADDING;

	    p = calloc(shdr.s_size, sizeof(char));
	    if (!p)
		fatal("out of virtual memory");

	    if (fseek(fobj, shdr.s_scnptr, 0) == -1)
		fatal("could not read `.text' from executable", i);
	    if (fread(p, shdr.s_size, 1, fobj) < 1)
		fatal("could not read text section from executable");

	    /* release the section buffer */
	    free(p);
	    break;

	case ECOFF_STYP_RDATA:
	    /* The .rdata section is sometimes placed before the text
	     * section instead of being contiguous with the .data section.
	     */
	    /* fall through */
	case ECOFF_STYP_DATA:
	    /* fall through */
	case ECOFF_STYP_SDATA:
	    break;

	case ECOFF_STYP_BSS:
	    break;

	case ECOFF_STYP_SBSS:
	    break;
	default:
	    break;
	}
    }

    /* compute data segment size from data break point */
    ld_text_base = MD_TEXT_BASE;
    ld_prog_entry = ahdr.entry;

    /* done with the executable, close it */
    if (fclose(fobj))
	fatal("could not close executable `%s'", fname);

    /* perform sanity checks on segment ranges */
    if (!ld_text_base || !ld_text_size)
	fatal("executable is missing a `.text' section");
    if (!ld_prog_entry)
	fatal("program entry point not specified");
}
