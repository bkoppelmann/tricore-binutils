/* gas_extlist.h -- Extended listing support for GNU AS.
	            Requires gas to be compiled with "-DHTC_SUPPORT",
		    "-I$(topdir)" and "#define EXT_LISTING 1" in tc-ARCH.h
	    	    (where `ARCH' is TRICORE, C16X, etc.).  This file is
		    included by gas/listing.c if HTC_SUPPORT is defined
		    and EXT_LISTING is non-zero.

   Copyright (C) 2005 HighTec EDV-Systeme GmbH.  All rights reserved.  */

#ifndef __HTC_GAS_EXTLIST_H
#define __HTC_GAS_EXTLIST_H

#ifdef HTC_TRICORE
#include "tc-tricore.h"
#else
#error Unknown or unsupported architecture
#endif
#include "frags.h"

/* Buffer to (temporarily) store notice messages.  */

char ext_listing_buf[EXT_LISTING_BUFLEN + 1];

/* Pointer to the frag to which the notice should be attached.  NULL
   means to use listing_tail->frag.  */

fragS *ext_listing_frag = (fragS *) NULL;

/* Prototypes and forward declarations.  */

void listing_notice PARAMS ((const char *));

/* Add NOTICE to the given FRAG's "tc_frag_data" member.  NOTICE should
   represent a string consisting of a single printable line (without the
   trailing newline) that will eventually appear in the assembler listing.
   This function may be called multiple times, in which case each passed
   string will be printed on a new line indented by strlen (_NOTICESTRING).  */

void
listing_notice (notice)
     const char *notice;
{
  size_t len;
  char *buf;
  int nls = 0;
  const char *cp;
  fragS *frag;

#define _NOTICESTRING _("****  Notice:")

  if ((frag = ext_listing_frag) == (fragS *) NULL)
    {
      if (listing_tail == (list_info_type *) NULL)
        return;

      if ((frag = listing_tail->frag) == (fragS *) NULL)
        return;  /* Shouldn't happen.  */
    }

  /* Count the number of newlines (+ 1) in NOTICE.  */
  for (cp = notice; cp; cp = strchr (cp, '\n'))
    ++nls;

  /* If this is the first notice, make a copy of NOTICE and assign it to
     the current frag's "notice" member.  Otherwise extend the "notice"
     member to include the current NOTICE.  */
  if (FRAG_NOTICE (frag) == (char *) NULL)
    {
      len = strlen (_NOTICESTRING) + strlen (notice) + 1;
      buf = xmalloc (len);
      sprintf (buf, "%s%s", _NOTICESTRING, notice);
    }
  else
    {
      len = (strlen (FRAG_NOTICE (frag))
      	     + strlen ("\n")
	     + strlen (_NOTICESTRING)
	     + strlen (notice) + 1);
      buf = xrealloc (FRAG_NOTICE (frag), len);
      sprintf (buf + strlen (FRAG_NOTICE (frag)), "\n%*s%s",
      	       strlen (_NOTICESTRING), " ", notice);
    }

  FRAG_NOTICE (frag) = buf;
  FRAG_NOTICE_LINES (frag) += nls;

#undef _NOTICESTRING
}

/* Output the notices attached to ELEM's frags to FILE and increment
   LINE_COUNTER by the number of lines contained in the notices.  */

#define PERFORM_EXT_LISTING(elem, file, line_counter)			\
  do									\
    {									\
      fragS *this_frag = ((elem)->frag);				\
      									\
      while (this_frag != (fragS *) NULL)				\
        {								\
          if (FRAG_NOTICE (this_frag))					\
            {								\
	      fprintf ((file), "%s\n", FRAG_NOTICE (this_frag));	\
	      (line_counter) += (FRAG_NOTICE_LINES (this_frag));	\
	      listing_page (0);						\
	    }								\
	  if (this_frag->line != (elem))				\
	    this_frag = this_frag->fr_next;				\
	  else								\
	    break;							\
	}								\
    }									\
  while (0)

#endif /* __HTC_GAS_EXTLIST_H  */
