/* version.c -- Print version of GNU binutils in case they are supported
		by HighTec EDV-Systeme GmbH.  This file corresponds to
		binutils/version.c; to build the binutils with this file,
		"$topdir/htc" must come first in binutils/Makefile's VPATH.

   Copyright (C) 2003 HighTec EDV-Systeme GmbH.  All rights reserved.  */

#include <stdio.h>
#include "bfd.h"
#include "bucomm.h"
#include "htc/htc_support.h"

/* Print the version number and copyright information for binutil NAME, and
   exit.  This implements the --version option for the various programs.  */

void
print_version (name)
     const char *name;
{
  char *toolversion = NULL;

  printf (_("GNU %s version %s (%s) using BFD version %s"),
  	  name, BFD_VERSION_STRING, HTC_ARCH_NAME, BFD_VERSION_STRING);
  printf (" (%s)\n", BFD_BUILD_STRING);
  if ((*name == 'a') && !strcmp (name, "addr2line"))
    toolversion = TOOL_VERSION_ADDR2LINE;
  else if ((*name == 'a') && !strcmp (name, "ar"))
    toolversion = TOOL_VERSION_AR;
  else if ((*name == 'n') && !strcmp (name, "nm"))
    toolversion = TOOL_VERSION_NM;
  else if ((*name == 'o') && !strcmp (name, "objcopy"))
    toolversion = TOOL_VERSION_OBJCOPY;
  else if ((*name == 'o') && !strcmp (name, "objdump"))
    toolversion = TOOL_VERSION_OBJDUMP;
  else if ((*name == 'r') && !strcmp (name, "ranlib"))
    toolversion = TOOL_VERSION_RANLIB;
  else if ((*name == 'r') && !strcmp (name, "readelf"))
    toolversion = TOOL_VERSION_READELF;
  else if ((*name == 's') && !strcmp (name, "size"))
    toolversion = TOOL_VERSION_SIZE;
  else if ((*name == 's') && !strcmp (name, "strings"))
    toolversion = TOOL_VERSION_STRINGS;
  else if ((*name == 's') && !strcmp (name, "strip"))
    toolversion = TOOL_VERSION_STRIP;

  if (toolversion)
    printf ("%s %s\n", TOOL_VERSION, toolversion);
  printf (HTC_COPYRIGHT, HTC_COPYRIGHT_PERIOD);

  exit (0);
}
