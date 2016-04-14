/* ldver.c -- Print version of GNU ld in case it is supported
	      by HighTec EDV-Systeme GmbH.  This file corresponds to
	      ld/ldver.c; to build the linker with this file,
	      "$topdir/htc" must come first in ld/Makefile's VPATH.

   Copyright (C) 2003 HighTec EDV-Systeme GmbH.  All rights reserved.  */

#include <stdio.h>
#include "bfd.h"
#include "sysdep.h"

#include "ld.h"
#include "ldver.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldfile.h"
#include "ldemul.h"
#include "ldmain.h"
#include "htc/htc_support.h"

void htc_print_ld_version PARAMS ((FILE *));

void
ldversion (noisy)
     int noisy;
{
  printf (_("GNU ld version %s (%s) using BFD version %s"),
  	  BFD_VERSION_STRING, HTC_ARCH_NAME, BFD_VERSION_STRING);
  printf (" (%s)\n", BFD_BUILD_STRING);
#ifdef TOOL_VERSION_LD
  printf ("%s %s\n", TOOL_VERSION, TOOL_VERSION_LD);
#endif

  if (noisy & 2)
    printf (HTC_COPYRIGHT, HTC_COPYRIGHT_PERIOD);

  if (noisy & 1)
    {
      ld_emulation_xfer_type **ptr = ld_emulations;

      printf (_("  Supported emulations:\n"));
      while (*ptr)
	{
	  printf ("   %s\n", (*ptr)->emulation_name);
	  ptr++;
	}
    }
}

void
htc_print_ld_version (out)
     FILE *out;
{
  fprintf (out, _("GNU ld version %s (%s) using BFD version %s"),
  	   BFD_VERSION_STRING, HTC_ARCH_NAME, BFD_VERSION_STRING);
  fprintf (out, " (%s)", BFD_BUILD_STRING);
#ifdef TOOL_VERSION_LD
  fprintf (out, ", %s %s", TOOL_VERSION, TOOL_VERSION_LD);
#endif
  fprintf (out, "\n");
}
