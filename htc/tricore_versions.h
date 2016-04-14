/* tricore_versions.h -- Definitions for release number and tool versions.

   Copyright (C) 2003 HighTec EDV-Systeme GmbH.  All rights reserved.  */

#ifndef __TRICORE_VERSIONS_H
#define __TRICORE_VERSIONS_H

#define HTC_ARCH_NAME "tricore"

/* Define intl'd strings for copyright notice.  */
#define HTC_COPYRIGHT _("\
Copyright (C) %s Free Software Foundation, Inc.\n\
This program is supported by HighTec EDV-Systeme GmbH.\n")
#define HTC_COPYRIGHT_PERIOD "1998-2005"

/* Define intl'ed string for tool version.  */
#define TOOL_VERSION _("Tool Version")

/* Define the BFD build number.  Whenever it is changed, all binutils need
   to be re-built: rm gas/tc-tricore.o ld/ldver.o binutils/version.o; make.  */

#define BFD_BUILD_STRING "2005-06-06"

/* Define the current tool version numbers.
   If any of these numbers are changed, cd to the configuration/build
   directory, delete the files gas/tc-tricore.o (assembler), ld/ldver.o
   (linker) and/or binutils/version.o (all other binutils), then start
   make (or make install) to re-build (and install) the tools.  */

#define TOOL_VERSION_ADDR2LINE	"v1.0"
#define TOOL_VERSION_AR		"v1.2"
#define TOOL_VERSION_GAS	"v3.7"
#define TOOL_VERSION_LD		"v3.7"
#define TOOL_VERSION_NM		"v1.0"
#define TOOL_VERSION_OBJCOPY	"v1.0"
#define TOOL_VERSION_OBJDUMP	"v1.4"
#define TOOL_VERSION_RANLIB	"v1.1"
#define TOOL_VERSION_READELF	"v1.0"
#define TOOL_VERSION_SIZE	"v1.1"
#define TOOL_VERSION_STRINGS	"v1.0"
#define TOOL_VERSION_STRIP	"v1.0"

/* These aren't necessary for the TriCore.  */
#define TOOL_VERSION_COFFDUMP	"v1.0"
#define TOOL_VERSION_DLLTOOL	"v1.0"
#define TOOL_VERSION_DLLWRAP	"v1.0"
#define TOOL_VERSION_NLMCONV	"v1.0"
#define TOOL_VERSION_SRCONV	"v1.0"
#define TOOL_VERSION_SYSDUMP	"v1.0"
#define TOOL_VERSION_WINDRES	"v1.0"

#endif /* __TRICORE_VERSIONS_H  */
