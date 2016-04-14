/* gas.h -- Support routines for GNU AS that will be compiled in if this
	    version of the tools is going to be delivered to customers
	    that have a valid support contract with HighTec EDV-Systeme GmbH.
	    Requires gas to be compiled with "-DHTC_SUPPORT -DHTC_ARCH \
	    -I$topdir" (where `ARCH' is TRICORE, C16X, etc.).

   Copyright (C) 2003-2005 HighTec EDV-Systeme GmbH.  All rights reserved.  */

#ifndef __HTC_GAS_H
#define __HTC_GAS_H

void htc_check_gas_version_flags PARAMS ((int *, char ***));
void htc_print_version_id PARAMS ((int));

/* *AC and *AV are pointers to the assembler's argc and argv variables
   as passed to its main function.  Filter out any flags that cause the
   assembler's version to be printed (and handle them here).  Build and
   return a new instance of *{AC,AV} that doesn't contain these flags.  */

void
htc_check_gas_version_flags (ac, av)
     int *ac;
     char ***av;
{
  int i, j;
  char **nargv = NULL;

  for (i = 1; i < *ac; ++i)
    {
      if (!strcmp ((*av)[i], "-v") || !strcmp ((*av)[i], "-V"))
        {
	  htc_print_version_id (0);
rebuild_ac_av:
	  if (nargv == NULL)
	    {
	      xmalloc_set_program_name ((*av)[0]);
	      nargv = (char **) xmalloc (sizeof (char *) * (*ac));
	      for (j = 0; j < i; ++j)
	        nargv[j] = (*av)[j];
	      for (j = i; j < (*ac - 1); ++j)
	        nargv[j] = (*av)[j + 1];
	      nargv[*ac - 1] = NULL;
	      *av = nargv;
	    }
	  else
	    {
	      for (j = i; j < *ac; ++j)
	        nargv[j] = nargv[j + 1];
	    }
	  *ac = *ac - 1;
	  --i;
	}
      else if ((*(*av)[i] == '-') && (strlen ((*av)[i]) > 5)
      	       && !strncmp ((*av)[i], "--version", strlen ((*av)[i])))
	{
	  htc_print_version_id (1);
	  goto rebuild_ac_av;
	}
    }
}

/* Print the assembler's release and version numbers; if VERBOSE is non-zero,
   also tell that this particular version of the assembler is supported.  */

void
htc_print_version_id (verbose)
     int verbose;
{
  static int printed = 0;
  FILE *stream = verbose ? stdout : stderr;

  if (printed && !verbose)
    return;

  printed = 1;

  fprintf (stream, _("GNU assembler version %s (%s) using BFD version %s"),
  	   HTC_GAS_VERSION, TARGET_ALIAS, BFD_VERSION_STRING);
  fprintf (stream, " (%s)", BFD_BUILD_STRING);
# ifdef TOOL_VERSION_GAS
  fprintf (stream, "\n%s %s", TOOL_VERSION, TOOL_VERSION_GAS);
# endif
  fprintf (stream, "\n");
  if (!verbose)
    return;

  fprintf (stream, HTC_COPYRIGHT, HTC_COPYRIGHT_PERIOD);
  fprintf (stream, _("This assembler was configured for a target of `%s'.\n"),
  	   TARGET_ALIAS);
  exit (0);
}

#endif /* __HTC_GAS_H  */
