# This shell script emits a C file. -*- C -*-
# Copyright (C) 2003 Free Software Foundation, Inc.
# Contributed by Michael Schumacher (mike@hightec-rt.com).
#
# This file is part of GLD, the Gnu Linker.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#

# This file is sourced from elf32.em.  It is used for the TriCore port
# to support multiple "small data areas" and to sort the input sections
# within these, such that byte- and short-aligned data can be addressed
# with a 10-bit offset to the respective sda base pointer.

#
# Overridden emulation functions.
#
LDEMUL_AFTER_PARSE=SDA_${EMULATION_NAME}_after_parse
LDEMUL_BEFORE_ALLOCATION=SDA_${EMULATION_NAME}_before_allocation
LDEMUL_PLACE_ORPHAN=SDA_${EMULATION_NAME}_place_orphan

#
# Additional options.
#
PARSE_AND_LIST_PROLOGUE='
#define OPTION_RELAX_BDATA		301
#define OPTION_RELAX_24REL		(OPTION_RELAX_BDATA + 1)
#define OPTION_DEBUG_RELAX		(OPTION_RELAX_24REL + 1)
#define OPTION_SORT_SDA			(OPTION_DEBUG_RELAX + 1)
#define OPTION_MULTI_SDAS		(OPTION_SORT_SDA + 1)
#define OPTION_WARN_ORPHAN		(OPTION_MULTI_SDAS + 1)
#define OPTION_EXT_MAP			(OPTION_WARN_ORPHAN + 1)
#define OPTION_PCP_MAP			(OPTION_EXT_MAP + 1)
#define OPTION_DEBUG_PCPMAP		(OPTION_PCP_MAP + 1)
'
# The options are repeated below so that no abbreviations are allowed.
# Otherwise -s matches sort-sda.
PARSE_AND_LIST_LONGOPTS='
  { "relax-bdata", no_argument, NULL, OPTION_RELAX_BDATA },
  { "relax-bdata", no_argument, NULL, OPTION_RELAX_BDATA },
  { "relax-24rel", no_argument, NULL, OPTION_RELAX_24REL },
  { "relax-24rel", no_argument, NULL, OPTION_RELAX_24REL },
  { "debug-relax", no_argument, NULL, OPTION_DEBUG_RELAX },
  { "debug-relax", no_argument, NULL, OPTION_DEBUG_RELAX },
#if 0
  { "sort-sda", no_argument, NULL, OPTION_SORT_SDA },
  { "sort-sda", no_argument, NULL, OPTION_SORT_SDA },
  { "multi-sdas", required_argument, NULL, OPTION_MULTI_SDAS },
  { "multi-sdas", required_argument, NULL, OPTION_MULTI_SDAS },
#endif
  { "warn-orphan", no_argument, NULL, OPTION_WARN_ORPHAN },
  { "warn-orphan", no_argument, NULL, OPTION_WARN_ORPHAN },
  { "extmap", required_argument, NULL, OPTION_EXT_MAP },
  { "extmap", required_argument, NULL, OPTION_EXT_MAP },
  { "pcpmap", optional_argument, NULL, OPTION_PCP_MAP },
  { "pcpmap", optional_argument, NULL, OPTION_PCP_MAP },
  { "debug-pcpmap", no_argument, NULL, OPTION_DEBUG_PCPMAP },
  { "debug-pcpmap", no_argument, NULL, OPTION_DEBUG_PCPMAP },
'
PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("\
  --relax-bdata         Compress bit objects contained in \".bdata\" input\n\
                          sections.  This option only takes effect in final\n\
			  (i.e., non-relocateable) link runs, and is also\n\
			  enabled implicitly by -relax.\n"
		   ));
  fprintf (file, _("\
  --relax-24rel         Relax call and jump instructions whose target address\n\
			  cannot be reached with a PC-relative offset, nor\n\
			  by switching to instructions using TriCore'\''s\n\
			  absolute addressing mode.  This option only takes\n\
			  effect in final (i.e., non-relocateable) link runs,\n\
			  and is also enabled implicitly by -relax.\n"
		   ));
#if 0
  fprintf (file, _("\
  --sort-sda            Sort the input sections of the \"small data area\"\n\
                          output section by alignment in order to minimize\n\
			  memory gaps and to give byte and short variables\n\
			  within these sections a better chance of being\n\
			  addressable via a 10-bit offset.\n"
		   ));
  fprintf (file, _("\
  --multi-sdas          Distribute the input sections of the \".sdata\"\n\
                          and \".sbss\" output sections to up to four\n\
			  such \"small data areas\" that must be present\n\
			  in the linker script.  This option also enables\n\
			  --sort-sda, which in this case performs sorting\n\
			  of all SDAs actually used.\n"
		   ));
#endif
  fprintf (file, _("\
  --warn-orphan		Issue a warning message if there is no dedicated\n\
  			  mapping between an input section and an output\n\
			  section.\n"
		  ));
  fprintf (file, _("\
  --extmap=[hLlNnma]	Output additional information in the map file:\n\
			  h - print header (version, date, ...)\n\
			  L - list global symbols sorted by name\n\
			  l - list all symbols sorted by name\n\
			  N - list global symbols sorted by address\n\
			  n - list all symbols sorted by address\n\
			  m - list memory segments\n\
			  a - all of the above.\n"
		  ));
  fprintf (file, _("\
  --pcpmap[=type]	Enable PCP/TriCore address mapping according to type.\n\
			  The following values may be given for type:\n\
			  0 or tc1796 - use TC1796-specific mapping\n\
			  If type is omitted, it defaults to 0.\n"
		  ));
'
PARSE_AND_LIST_ARGS_CASES='
    case OPTION_RELAX_BDATA:
      tricore_elf32_relax_bdata = true;
      break;

    case OPTION_RELAX_24REL:
      tricore_elf32_relax_24rel = true;
      break;

    case OPTION_DEBUG_RELAX:
      tricore_elf32_debug_relax = true;
      break;

#if 0
    case OPTION_SORT_SDA:
      sort_sda = true;
      break;

    case OPTION_MULTI_SDAS:
      sort_sda = multi_sdas = true;
      break;
#endif

    case OPTION_WARN_ORPHAN:
      warn_orphan = true;
      break;

    case OPTION_EXT_MAP:
      parse_extmap_args (optarg);
      break;

    case OPTION_PCP_MAP:
      parse_pcpmap_args (optarg);
      break;

    case OPTION_DEBUG_PCPMAP:
      tricore_elf32_debug_pcpmap = true;
      break;
'

#
# The rest of this file implements the additional functionality in C.
#
cat >>e${EMULATION_NAME}.c <<EOF

/* Must be kept in sync with bfd/elf32-tricore.c.  */
typedef struct _memreg
{
  char *name;
  bfd_vma start;
  bfd_size_type length;
  bfd_size_type used;
} memreg_t;

char *tricore_elf32_get_scriptdir PARAMS ((char *));
static void parse_extmap_args PARAMS ((char *));
static void parse_pcpmap_args PARAMS ((char *));
static memreg_t *get_memregs PARAMS ((int *));
#ifndef HTC_SUPPORT
static void print_ld_version PARAMS ((FILE *));
#endif
static void SDA_${EMULATION_NAME}_after_parse PARAMS ((void));
static void SDA_${EMULATION_NAME}_before_allocation PARAMS ((void));
static boolean SDA_${EMULATION_NAME}_place_orphan
  PARAMS ((lang_input_statement_type *, asection *));
#if 0
static void tricore_elf32_sort_sdata PARAMS ((void));
static void tricore_elf32_shuffle_sdata PARAMS ((void));
static void tricore_elf32_find_sdas PARAMS ((lang_statement_union_type *));

static boolean sort_sda = false;
static boolean multi_sdas = false;
#endif
static boolean warn_orphan = false;

extern boolean tricore_elf32_relax_bdata;
extern boolean tricore_elf32_relax_24rel;
extern boolean tricore_elf32_debug_relax;
extern FILE *tricore_elf32_map_file;
extern char *tricore_elf32_map_filename;
extern boolean tricore_elf32_extmap_enabled;
extern boolean tricore_elf32_extmap_header;
extern boolean tricore_elf32_extmap_memory_segments;
extern int tricore_elf32_extmap_syms_by_name;
extern int tricore_elf32_extmap_syms_by_addr;
extern char *tricore_elf32_extmap_ld_name;
extern void (*tricore_elf32_extmap_ld_version) PARAMS ((FILE *));
extern int tricore_elf32_pcpmap;
extern boolean tricore_elf32_debug_pcpmap;

/* This parses the sub-options to the --extmap option.  */

static void
parse_extmap_args (args)
     char *args;
{
  char *cp;

  if ((args == NULL) || (*args == '\0'))
    einfo (_("%P%F: error: missing sub-option(s) for extmap\n"));

  for (cp = args; *cp; ++cp)
    if (*cp == 'a')
      {
        tricore_elf32_extmap_header = true;
	tricore_elf32_extmap_syms_by_name = 2;
	tricore_elf32_extmap_syms_by_addr = 2;
	tricore_elf32_extmap_memory_segments = true;
      }
    else if (*cp == 'h')
      tricore_elf32_extmap_header = true;
    else if (*cp == 'L')
      tricore_elf32_extmap_syms_by_name = 1;
    else if (*cp == 'l')
      tricore_elf32_extmap_syms_by_name = 2;
    else if (*cp == 'N')
      tricore_elf32_extmap_syms_by_addr = 1;
    else if (*cp == 'n')
      tricore_elf32_extmap_syms_by_addr = 2;
    else if (*cp == 'm')
      tricore_elf32_extmap_memory_segments = true;
    else
      {
        cp[1] = '\0';
        einfo (_("%P%F: error: unrecognized extmap sub-option \`%s'\n"), cp);
      }

  tricore_elf32_extmap_enabled = true;
  tricore_elf32_extmap_ld_name = program_name;
}

/* This parses the sub-options to the --pcpmap option.  */

static void
parse_pcpmap_args (args)
     char *args;
{
  if (args == NULL)
    {
      tricore_elf32_pcpmap = 0;
      return;
    }

  if (!strcmp (args, "0") || !strcasecmp (args, "tc1796"))
    {
      tricore_elf32_pcpmap = 0;
      return;
    }

  einfo (_("%P%F: error: invalid type specifier for pcpmap\n"));
}

/* Create and return a list of all defined memory regions and their
   use; also return the number of such regions in *NREGIONS.  */

static memreg_t *
get_memregs (nregions)
     int *nregions;
{
  int i = 0;
  lang_memory_region_type *p, *p2;
  memreg_t *memregs;

  p = lang_memory_region_lookup ("<getmemreglist>");
  if (p == NULL)
    return (memreg_t *) NULL;

  for (i = 0, p2 = p; p2; ++i, p2 = p2->next)
    /* Empty.  */ ;
  *nregions = i;

  memregs = (memreg_t *) xmalloc (i * sizeof (memreg_t));
  if (memregs == NULL)
    return memregs;

  for (i = 0; p; ++i, p = p->next)
    {
      memregs[i].name = p->name;
      memregs[i].start = p->origin;
      memregs[i].length = p->length;
      memregs[i].used = p->current - p->origin;
    }

  return memregs;
}

#ifndef HTC_SUPPORT
/* Print the version of the linker to file OUT.  */

static void
print_ld_version (out)
     FILE *out;
{
  fprintf (out, "GNU ld version %s\n", BFD_VERSION_STRING);
}
#endif

/* This is called after the linker script has been parsed.  Currently
   unused.  */

static void
SDA_${EMULATION_NAME}_after_parse ()
{
}

/* This is called before sections are allocated.  */

static void
SDA_${EMULATION_NAME}_before_allocation ()
{
  /* Call main function; we're just extending it.  */
  gld${EMULATION_NAME}_before_allocation ();

#if 0
  if (multi_sdas)
    tricore_elf32_shuffle_sdata ();
  if (sort_sda)
    tricore_elf32_sort_sdata ();

  if (multi_sdas || sort_sda)
    lang_for_each_statement (tricore_elf32_find_sdas);
#endif

  if (command_line.relax)
    tricore_elf32_relax_bdata = tricore_elf32_relax_24rel = true;
  else if (tricore_elf32_relax_bdata
  	   || tricore_elf32_relax_24rel)
    command_line.relax = true;

  if (config.map_file)
    {
      tricore_elf32_map_file = config.map_file;
      if (tricore_elf32_extmap_enabled)
        {
	  extern memreg_t *(*tricore_elf32_extmap_get_memregs) PARAMS ((int *));
#ifdef HTC_SUPPORT
	  extern void htc_print_ld_version PARAMS ((FILE *));

	  tricore_elf32_extmap_ld_version = htc_print_ld_version;
#else
	  tricore_elf32_extmap_ld_version = print_ld_version;
#endif
	  tricore_elf32_extmap_get_memregs = get_memregs;
          tricore_elf32_map_filename = config.map_filename;
	}
    }
  else if (tricore_elf32_extmap_enabled)
    {
      einfo (_("%P: warning: ignoring --extmap option due to missing "
      	       "-M/-Map option\n"));
      tricore_elf32_extmap_enabled = false;
    }
}

static boolean
SDA_${EMULATION_NAME}_place_orphan (file, sec)
     lang_input_statement_type *file;
     asection *sec;
{
  if (warn_orphan)
    {
      const char *secname = bfd_get_section_name (sec->owner, sec);

      einfo (_("%P: warning: placing orphan section \"%s(%s)\"\n"),
      	     bfd_archive_filename (sec->owner), secname);
    }

  /* Now call the original function to do the actual work.  */
  return gld${EMULATION_NAME}_place_orphan (file, sec);
}

#if 0
static boolean
tricore_elf32_check_sda (name)
     const char *name;
{
  lang_output_section_statement_type *os;
  lang_statement_union_type *u;

  if ((os = lang_output_section_find (name)) == NULL)
    return false;

  for (u = os->children.head; u != NULL; u = u->header.next)
    {
      lang_statement_union_type *u2;
      lang_input_statement_type *is;

      if (u->header.type == lang_input_section_enum)
	{
	  is = &u2->input_statement;
	}
    }
}

static void
tricore_elf32_sort_sdata ()
{
}

static void
tricore_elf32_shuffle_sdata ()
{
}

static void
tricore_elf32_find_sdas (s)
     lang_statement_union_type *s;
{
  static boolean first = true;

  if (first)
    {
      lang_output_section_statement_type *os;
      lang_statement_union_type *u;

      for (u = lang_output_section_statement.head; u != NULL; u = os->next)
        {
	  lang_statement_union_type *u2;
	  lang_input_statement_type *is;

	  os = &u->output_section_statement;
	  printf ("***%p %s\n", os, os->name);
	  if (!strcmp (os->name, ".sdata"))
	    for (u2 = os->children.head; u2; u2 = u2->header.next)
	      {
	        if (u2->header.type == lang_input_section_enum)
	          {
		    is = &u2->input_statement;
		    printf ("%p\n", is);
		  }
	        printf ("%d\n", u2->header.type);
	      }
	}
      first = false;
    }

  if (s->header.type == lang_output_section_statement_enum)
    {
      lang_output_section_statement_type *os;

      os = &s->output_section_statement;
#if 0
      if (!strcmp (os->name, ".sdata1") && !os->bfd_section)
        init_os (os);
#endif
      printf ("%p output section name = <%s>, bfd_section = %p\n",
              os, os->name, os->bfd_section);
    }
}
#endif
EOF

if test x"${EMULATION_NAME}" = xelf32tricore; then
cat >>e${EMULATION_NAME}.c <<EOF

/* When configuring the binutils, the macro SCRIPTDIR will be set to
   \$tooldir/lib; the linker expects this to be the directory where
   the 'ldscript' directory resides.  This is okay as long as the
   tools are actually installed in the directory passed to (or
   defaulted by) the configure script, but may cause trouble otherwise.
   For example, if SCRIPTDIR points to a drive/directory that is only
   temporarily available, access() may either block or cause a considerable
   delay.  We therefore use the function below to determine the library
   directory path at runtime, starting with the path of the executable.  */

#include "filenames.h"

#if defined(HAVE_DOS_BASED_FILE_SYSTEM) && !defined(__CYGWIN__)
#define DOSFS
#define SEPSTR "\\\\"
#define realpath(rel, abs) _fullpath ((abs), (rel), MAXPATHLEN)
#else
#define SEPSTR "/"
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 2048
#endif

static char abs_progname[MAXPATHLEN + 1];

char *
tricore_elf32_get_scriptdir (progname)
     char *progname;
{
  char *beg, *cp, *cp2, *basedir, *libpath;
  struct stat statbuf;

  if (!realpath (progname, abs_progname))
    return (".");

  cp = basedir = abs_progname;
  cp2 = NULL;
#ifdef DOSFS
  if (ISALPHA (cp[0]) && (cp[1] == ':'))
    cp += 2;
#endif
  for (beg = cp; *cp; ++cp)
    if (IS_DIR_SEPARATOR (*cp))
      cp2 = cp;
  if (cp2 != NULL)
    *cp2 = '\0';
  else
    *beg = '\0';

  libpath = concat (basedir, SEPSTR, ".."SEPSTR"tricore"SEPSTR"lib", NULL);
  if ((stat (libpath, &statbuf) != 0) || !S_ISDIR (statbuf.st_mode))
    {
      free (libpath);
      libpath = concat (basedir, SEPSTR, ".."SEPSTR"lib", NULL);
    }
  strcpy (abs_progname, libpath);
  free (libpath);

  return abs_progname;
}
EOF
fi

