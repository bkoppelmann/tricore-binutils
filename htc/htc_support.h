/* htc_support.h -- Generic header for HighTec support routines for binutils.
		    Tested with binutils-2.13.

   Copyright (C) 2003 HighTec EDV-Systeme GmbH.  All rights reserved.  */

#ifndef __HTC_SUPPORT_H
#define __HTC_SUPPORT_H

#ifdef HTC_TRICORE
#include "htc/tricore_versions.h"
#else
#error "Unknown architecture in \"htc_support.h\"."
#endif

#ifdef HTC_GAS_VERSION
#include "htc/gas.h"
#endif

#endif /* __HTC_SUPPORT_H  */
