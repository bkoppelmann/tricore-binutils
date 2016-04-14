/* TriCore-specific support for 32-bit ELF.
   Copyright (C) 1998-2003 Free Software Foundation, Inc.
   Contributed by Michael Schumacher (mike@hightec-rt.com).

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "bfd.h"
#include "sysdep.h"
#include "libbfd.h"
#include "bfdlink.h"
#include "genlink.h"
#include "elf-bfd.h"
#include "elf/tricore.h"

/* The full name of the default instruction set architecture.  */

#define DEFAULT_ISA "TriCore:Rider-B"

/* The full name of the dynamic interpreter; put in the .interp section.  */

#define ELF_DYNAMIC_INTERPRETER "/lib/ld-tricore.so.1"

/* The number of reserved entries at the beginning of the PLT.  */

#define PLT_RESERVED_SLOTS 2

/* The size (in bytes) of a PLT entry.  */

#define PLT_ENTRY_SIZE 12

/* Section flag for PCP sections.  */

#define PCP_SEG SEC_ARCH_BIT_0

/* Section flags for dynamic relocation sections.  */

#define RELGOTSECFLAGS	(SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS \
			 | SEC_IN_MEMORY | SEC_LINKER_CREATED | SEC_READONLY)
#define DYNOBJSECFLAGS  (SEC_HAS_CONTENTS | SEC_IN_MEMORY \
			 | SEC_LINKER_CREATED | SEC_READONLY)

/* Will references to this symbol always reference the symbol in this obj?  */

#define SYMBOL_REFERENCES_LOCAL(INFO, H)				\
  ((!INFO->shared							\
    || INFO->symbolic							\
    || (H->dynindx == -1)						\
    || (ELF_ST_VISIBILITY (H->other) == STV_INTERNAL)			\
    || (ELF_ST_VISIBILITY (H->other) == STV_HIDDEN))			\
   && ((H->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) != 0))

/* Will _calls_ to this symbol always call the version in this object?  */

#define SYMBOL_CALLS_LOCAL(INFO, H)					\
  ((!INFO->shared							\
    || INFO->symbolic							\
    || (H->dynindx == -1)						\
    || (ELF_ST_VISIBILITY (H->other) != STV_DEFAULT))			\
   && ((H->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) != 0))

/* Describe "short addressable" memory areas (SDAs, Small Data Areas).  */

typedef struct _sda_t
{
  /* Name of this SDA's short addressable data section.  */
  const char *data_section_name;

  /* Name of this SDA's short addressable BSS section.  */
  const char *bss_section_name;

  /* Name of the symbol that contains the base address of this SDA.  */
  const char *sda_symbol_name;

  /* Pointers to the BFD representations of the above data/BSS sections.  */
  asection *data_section;
  asection *bss_section;

  /* The base address of this SDA; usually points to the middle of a 64k
     memory area to provide full access via a 16-bit signed offset; this
     can, however, be overridden by the user (via "--defsym" on the command
     line, or in the linker script with an assignment statement such as
     "_SMALL_DATA_ = ABSOLUTE (.);" in output section ".sbss" or ".sdata").  */
  bfd_vma gp_value;

  /* The number of the address register that contains the base address
     of this SDA.  This is 0 for .sdata/.sbss (or 12 if this is a dynamic
     executable, because in this case the SDA follows immediately after the
     GOT, and both are accessed via the GOT pointer), 1 for .sdata2/.sbss2,
     8 for .sdata3/.sbss3, and 9 for .sdata4/.sbss4.  */
  int areg;

  /* True if this SDA has been specified as an output section in the
     linker script; it suffices if either the data or BSS section of
     this SDA has been specified (e.g., just ".sbss2", but not ".sdata2"
     for SDA1, or just ".sdata", but not ".sbss" for SDA0).  */
  boolean valid;
} sda_t;

/* We allow up to four independent SDAs in executables.  For instance,
   if you need 128k of initialized short addressable data, and 128k of
   uninitialized short addressable data, you could specify .sdata, .sdata2,
   .sbss3, and .sbss4 as output sections in your linker script.  Note,
   however, that according to the EABI only the first SDA must be supported,
   while support for the second SDA (called "literal section") is optional.
   The other two SDAs are GNU extensions and can only be used in standalone
   applications, or if an underlying OS doesn't use %a8 and %a9 for its own
   purposes.  Also note that shared objects may only use the first SDA,
   which will be addressed via the GOT pointer (%a12), so it can't exceed
   32k, and may only use it for static variables.  That's because if a
   program references a global variable defined in a shared object, the
   linker reserves space for it in the program's ".dynbss" section and emits
   a COPY reloc that will be resolved by the dynamic linker.  If, however,
   the variable would be defined in the SDA of a SO, then this would lead
   to different accesses to this variable, as the program expects it to live
   in its ".dynbss" section, while the SO was compiled to access it in its
   SDA -- clearly a situation that must be avoided.  */

#define NR_SDAS 4

sda_t small_data_areas[NR_SDAS] =
{
  { ".sdata",
    ".sbss",
    "_SMALL_DATA_",
    (asection *) NULL,
    (asection *) NULL,
    0,
    0,
    false
  },

  { ".sdata2",
    ".sbss2",
    "_SMALL_DATA2_",
    (asection *) NULL,
    (asection *) NULL,
    0,
    1,
    false
  },

  { ".sdata3",
    ".sbss3",
    "_SMALL_DATA3_",
    (asection *) NULL,
    (asection *) NULL,
    0,
    8,
    false
  },

  { ".sdata4",
    ".sbss4",
    "_SMALL_DATA4_",
    (asection *) NULL,
    (asection *) NULL,
    0,
    9,
    false
  }
};

/* If the user requested an extended map file, we might need to keep a list
   of global (and possibly static) symbols.  */

typedef struct _symbol_t
{
  /* Name of symbol/variable.  */
  const char *name;

  /* Memory location of this variable, or value if it's an absolute symbol.  */
  bfd_vma address;

  /* Alignment of this variable (in output section).  */
  int align;

  /* Name of memory region this variable lives in.  */
  char *region_name;

  /* True if this is a bit variable.  */
  boolean is_bit;

  /* Bit position if this is a bit variable.  */
  int bitpos;

  /* True if this is a static variable.  */
  boolean is_static;

  /* Size of this variable.  */
  bfd_vma size;

  /* Pointer to the section in which this symbol is defined.  */
  asection *section;

  /* Name of module in which this symbol is defined.  */
  const char *module_name;
} symbol_t;

/* Symbols to be listed are stored in a dynamically allocated array.  */

static symbol_t *symbol_list;
static int symbol_list_idx = -1;
static int symbol_list_max = 512;

/* This describes memory regions defined by the user; must be kept in
   sync with ld/emultempl/tricoreelf.em.  */

typedef struct _memreg
{
  /* Name of region.  */
  char *name;

  /* Start of region.  */
  bfd_vma start;

  /* Length of region.  */
  bfd_size_type length;

  /* Number of allocated (used) bytes.  */
  bfd_size_type used;
} memreg_t;

/* This array describes TriCore relocations.  */

static reloc_howto_type tricore_elf32_howto_table[] =
{
  /* No relocation (ignored).  */
  HOWTO (R_TRICORE_NONE,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_NONE",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 false),		/* pcrel_offset */

  /* 32 bit PC-relative relocation.  */
  HOWTO (R_TRICORE_32REL,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 true,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_32REL",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* 32 bit absolute relocation.  */
  HOWTO (R_TRICORE_32ABS,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_32ABS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* relB 25 bit PC-relative relocation.  */
  HOWTO (R_TRICORE_24REL,	/* type */
	 1,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 25,			/* bitsize */
	 true,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_TRICORE_24REL",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffff00,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* absB 24 bit absolute address relocation.  */
  HOWTO (R_TRICORE_24ABS,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_24ABS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffff00,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* bolC 16 bit small data section relocation.  */
  HOWTO (R_TRICORE_16SM,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_16SM",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* RLC High 16 bits of symbol value, adjusted.  */
  HOWTO (R_TRICORE_HIADJ,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_HIADJ",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* RLC Low 16 bits of symbol value.  */
  HOWTO (R_TRICORE_LO,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_LO",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* BOL Low 16 bits of symbol value.  */
  HOWTO (R_TRICORE_LO2,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_LO2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* ABS 18 bit absolute address relocation.  */
  HOWTO (R_TRICORE_18ABS,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 18,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_TRICORE_18ABS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf3fff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* BO 10 bit relative small data relocation.  */
  HOWTO (R_TRICORE_10SM,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 10,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_TRICORE_10SM",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf03f0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* BR 15 bit PC-relative relocation.  */
  HOWTO (R_TRICORE_15REL,	/* type */
	 1,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 true,			/* pc_relative */
	 16,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_15REL",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x7fff0000,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* RLC High 16 bits of symbol value.  */
  HOWTO (R_TRICORE_HI,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_TRICORE_HI",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit signed constant.  */
  HOWTO (R_TRICORE_16CONST,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_16CONST",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rcC2 9 bit unsigned constant.  */
  HOWTO (R_TRICORE_9ZCONST,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 9,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_9ZCONST",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x001ff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* RcC 9 bit signed constant.  */
  HOWTO (R_TRICORE_9SCONST,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 9,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_9SCONST",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x001ff000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* sbD 9 bit PC-relative displacement.  */
  HOWTO (R_TRICORE_8REL,	/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 9,			/* bitsize */
	 true,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_8REL",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff00,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* scC 8 bit unsigned constant.  */
  HOWTO (R_TRICORE_8CONST,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 false,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_8CONST",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff00,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* BO 10 bit data offset.  */
  HOWTO (R_TRICORE_10OFF,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 10,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_10OFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf03f0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* bolC 16 bit data offset.  */
  HOWTO (R_TRICORE_16OFF,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_16OFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* 8 bit absolute data relocation.  */
  HOWTO (R_TRICORE_8ABS,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_TRICORE_8ABS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 false),		/* pcrel_offset */

  /* 16 bit absolute data relocation.  */
  HOWTO (R_TRICORE_16ABS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_16ABS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* absBb 1 bit relocation.  */
  HOWTO (R_TRICORE_1BIT,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 1,			/* bitsize */
	 false,			/* pc_relative */
	 11,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_1BIT",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x00000800,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* absBp 3 bit bit position.  */
  HOWTO (R_TRICORE_3POS,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 3,			/* bitsize */
	 false,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_3POS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x00000700,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* bitP1 5 bit bit position.  */
  HOWTO (R_TRICORE_5POS,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 false,			/* pc_relative */
	 16,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_5POS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x001f0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* PCP HI relocation.  */
  HOWTO (R_TRICORE_PCPHI,	/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCPHI",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* PCP LO relocation.  */
  HOWTO (R_TRICORE_PCPLO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCPLO",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* PCP PAGE relocation.  */
  HOWTO (R_TRICORE_PCPPAGE,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCPPAGE",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff00,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* PCP OFF relocation.  */
  HOWTO (R_TRICORE_PCPOFF,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCPOFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x003f,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* PCP TEXT relocation.  */
  HOWTO (R_TRICORE_PCPTEXT,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCPTEXT",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* bitP2 5 bit bit position.  */
  HOWTO (R_TRICORE_5POS2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 false,			/* pc_relative */
	 23,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_5POS2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f800000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* brcC 4 bit signed offset.  */
  HOWTO (R_TRICORE_BRCC,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_BRCC",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000f000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* brcC2 4 bit unsigned offset.  */
  HOWTO (R_TRICORE_BRCZ,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_BRCZ",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000f000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* brnN 5 bit bit position.  */
  HOWTO (R_TRICORE_BRNN,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_BRNN",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000f080,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rrN 2 bit unsigned constant.  */
  HOWTO (R_TRICORE_RRN,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 2,			/* bitsize */
	 false,			/* pc_relative */
	 16,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_RRN",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x00030000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* sbcC 4 bit signed constant.  */
  HOWTO (R_TRICORE_4CONST,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4CONST",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* sbcD/sbrD 5 bit PC-relative, zero-extended displacement.  */
  HOWTO (R_TRICORE_4REL,	/* type */
	 1,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 true,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4REL",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f00,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* sbrD 5 bit PC-relative, one-extended displacement.  */
  HOWTO (R_TRICORE_4REL2,	/* type */
	 1,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 true,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4REL2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f00,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* sbrN 5 bit bit position.  */
  HOWTO (R_TRICORE_5POS3,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_5POS3",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf080,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* slroO 4 bit zero-extended offset.  */
  HOWTO (R_TRICORE_4OFF,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4OFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* slroO2 5 bit zero-extended offset.  */
  HOWTO (R_TRICORE_4OFF2,	/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4OFF2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* slroO4 6 bit zero-extended offset.  */
  HOWTO (R_TRICORE_4OFF4,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 6,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4OFF4",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* sroO 4 bit zero-extended offset.  */
  HOWTO (R_TRICORE_42OFF,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 false,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_42OFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f00,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* sroO2 5 bit zero-extended offset.  */
  HOWTO (R_TRICORE_42OFF2,	/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 false,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_42OFF2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f00,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* slroO4 6 bit zero-extended offset.  */
  HOWTO (R_TRICORE_42OFF4,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 6,			/* bitsize */
	 false,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_42OFF4",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f00,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* srrsN 2 bit zero-extended constant.  */
  HOWTO (R_TRICORE_2OFF,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 2,			/* bitsize */
	 false,			/* pc_relative */
	 6,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_2OFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x00c0,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* scC 8 bit zero-extended offset.  */
  HOWTO (R_TRICORE_8CONST2,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 false,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_8CONST2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff00,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* sbrnN 4 bit zero-extended constant.  */
  HOWTO (R_TRICORE_4POS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_4POS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xf000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* rlcC 16 bit small data section relocation.  */
  HOWTO (R_TRICORE_16SM2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_16SM2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* sbcD/sbrD 6 bit PC-relative, zero-extended displacement.  */
  HOWTO (R_TRICORE_5REL,	/* type */
	 1,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 true,			/* pc_relative */
	 8,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_5REL",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f00,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* Special reloc for optimizing virtual tables.  */
  HOWTO (R_TRICORE_GNU_VTENTRY,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GNU_VTENTRY",/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 false), 		/* pcrel_offset */

  /* Special reloc for optimizing virtual tables.  */
  HOWTO (R_TRICORE_GNU_VTINHERIT,/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GNU_VTINHERIT",/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 false),  		/* pcrel_offset */

  /* 16 bit PC-relative relocation.  */
  HOWTO (R_TRICORE_PCREL16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 true,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCREL16",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* 8 bit PC-relative relocation.  */
  HOWTO (R_TRICORE_PCREL8,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 true,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_PCREL8",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 true),			/* pcrel_offset */

  /* rlcC 16 bit GOT symbol entry.  */
  HOWTO (R_TRICORE_GOT,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOT",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* bolC 16 bit GOT symbol entry.  */
  HOWTO (R_TRICORE_GOT2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOT2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit GOTHI symbol entry.  */
  HOWTO (R_TRICORE_GOTHI,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTHI",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* rlcC 16 bit GOTLO symbol entry.  */
  HOWTO (R_TRICORE_GOTLO,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTLO",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* bolC 16 bit GOTLO symbol entry.  */
  HOWTO (R_TRICORE_GOTLO2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTLO2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit GOTUP symbol entry.  */
  HOWTO (R_TRICORE_GOTUP,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTUP",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* rlcC 16 bit GOTOFF symbol entry.  */
  HOWTO (R_TRICORE_GOTOFF,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTOFF",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* bolC 16 bit GOTOFF symbol entry.  */
  HOWTO (R_TRICORE_GOTOFF2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTOFF2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit GOTOFFHI symbol entry.  */
  HOWTO (R_TRICORE_GOTOFFHI,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTOFFHI",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* rlcC 16 bit GOTOFFLO symbol entry.  */
  HOWTO (R_TRICORE_GOTOFFLO,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTOFFLO",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* bolC 16 bit GOTOFFLO symbol entry.  */
  HOWTO (R_TRICORE_GOTOFFLO2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTOFFLO2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit GOTOFFUP symbol entry.  */
  HOWTO (R_TRICORE_GOTOFFUP,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTOFFUP",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* rlcC 16 bit GOTPC symbol entry.  */
  HOWTO (R_TRICORE_GOTPC,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTPC",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* bolC 16 bit GOTPC symbol entry.  */
  HOWTO (R_TRICORE_GOTPC2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTPC2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit GOTPCHI symbol entry.  */
  HOWTO (R_TRICORE_GOTPCHI,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTPCHI",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* rlcC 16 bit GOTPCLO symbol entry.  */
  HOWTO (R_TRICORE_GOTPCLO,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTPCLO",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* bolC 16 bit GOTPCLO symbol entry.  */
  HOWTO (R_TRICORE_GOTPCLO2,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTPCLO2",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff0000,		/* dst_mask */
	 false),		/* pcrel_offset */

  /* rlcC 16 bit GOTPCUP symbol entry.  */
  HOWTO (R_TRICORE_GOTPCUP,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 false,			/* pc_relative */
	 12,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GOTPCUP",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0ffff000,		/* dst_mask */
	 false), 		/* pcrel_offset */

  /* relB PLT entry.  */
  HOWTO (R_TRICORE_PLT,		/* type */
	 1,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 25,			/* bitsize */
	 true,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_TRICORE_PLT",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffff00,		/* dst_mask */
	 true),			/* pcrel_offset */

  /* COPY.  */
  HOWTO (R_TRICORE_COPY,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_COPY",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 true), 		/* pcrel_offset */

  /* GLOB_DAT.  */
  HOWTO (R_TRICORE_GLOB_DAT,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_GLOB_DAT",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 true), 		/* pcrel_offset */

  /* JMP_SLOT.  */
  HOWTO (R_TRICORE_JMP_SLOT,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_JMP_SLOT",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 true), 		/* pcrel_offset */

  /* RELATIVE.  */
  HOWTO (R_TRICORE_RELATIVE,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_RELATIVE",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 true),	 		/* pcrel_offset */

  /* BITPOS.  */
  HOWTO (R_TRICORE_BITPOS,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 false,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_TRICORE_BITPOS",	/* name */
	 false,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 false)	 		/* pcrel_offset */
};

/* Describe the mapping between BFD and TriCore relocs.  */

struct elf_reloc_map {
  bfd_reloc_code_real_type bfd_reloc_val;
  enum elf32_tricore_reloc_type tricore_val;
};

static CONST struct elf_reloc_map tricore_reloc_map[] =
{
  {BFD_RELOC_NONE,              R_TRICORE_NONE},
  {BFD_RELOC_TRICORE_32REL,     R_TRICORE_32REL},
  {BFD_RELOC_TRICORE_32ABS,     R_TRICORE_32ABS},
  {BFD_RELOC_TRICORE_24REL,     R_TRICORE_24REL},
  {BFD_RELOC_TRICORE_24ABS,     R_TRICORE_24ABS},
  {BFD_RELOC_TRICORE_16SM,      R_TRICORE_16SM},
  {BFD_RELOC_TRICORE_HIADJ,     R_TRICORE_HIADJ},
  {BFD_RELOC_TRICORE_LO,        R_TRICORE_LO},
  {BFD_RELOC_TRICORE_LO2,       R_TRICORE_LO2},
  {BFD_RELOC_TRICORE_18ABS,     R_TRICORE_18ABS},
  {BFD_RELOC_TRICORE_10SM,      R_TRICORE_10SM},
  {BFD_RELOC_TRICORE_15REL,     R_TRICORE_15REL},
  {BFD_RELOC_TRICORE_HI,        R_TRICORE_HI},
  {BFD_RELOC_TRICORE_16CONST,   R_TRICORE_16CONST},
  {BFD_RELOC_TRICORE_9ZCONST,   R_TRICORE_9ZCONST},
  {BFD_RELOC_TRICORE_9SCONST,   R_TRICORE_9SCONST},
  {BFD_RELOC_TRICORE_8REL,      R_TRICORE_8REL},
  {BFD_RELOC_TRICORE_8CONST,    R_TRICORE_8CONST},
  {BFD_RELOC_TRICORE_10OFF,     R_TRICORE_10OFF},
  {BFD_RELOC_TRICORE_16OFF,     R_TRICORE_16OFF},
  {BFD_RELOC_TRICORE_8ABS,      R_TRICORE_8ABS},
  {BFD_RELOC_TRICORE_16ABS,     R_TRICORE_16ABS},
  {BFD_RELOC_TRICORE_1BIT,      R_TRICORE_1BIT},
  {BFD_RELOC_TRICORE_3POS,      R_TRICORE_3POS},
  {BFD_RELOC_TRICORE_5POS,      R_TRICORE_5POS},
  {BFD_RELOC_TRICORE_PCPHI,     R_TRICORE_PCPHI},
  {BFD_RELOC_TRICORE_PCPLO,     R_TRICORE_PCPLO},
  {BFD_RELOC_TRICORE_PCPPAGE,   R_TRICORE_PCPPAGE},
  {BFD_RELOC_TRICORE_PCPOFF,    R_TRICORE_PCPOFF},
  {BFD_RELOC_TRICORE_PCPTEXT,   R_TRICORE_PCPTEXT},
  {BFD_RELOC_TRICORE_5POS2,     R_TRICORE_5POS2},
  {BFD_RELOC_TRICORE_BRCC,      R_TRICORE_BRCC},
  {BFD_RELOC_TRICORE_BRCZ,      R_TRICORE_BRCZ},
  {BFD_RELOC_TRICORE_BRNN,      R_TRICORE_BRNN},
  {BFD_RELOC_TRICORE_RRN,       R_TRICORE_RRN},
  {BFD_RELOC_TRICORE_4CONST,    R_TRICORE_4CONST},
  {BFD_RELOC_TRICORE_4REL,      R_TRICORE_4REL},
  {BFD_RELOC_TRICORE_4REL2,     R_TRICORE_4REL2},
  {BFD_RELOC_TRICORE_5POS3,     R_TRICORE_5POS3},
  {BFD_RELOC_TRICORE_4OFF,      R_TRICORE_4OFF},
  {BFD_RELOC_TRICORE_4OFF2,     R_TRICORE_4OFF2},
  {BFD_RELOC_TRICORE_4OFF4,     R_TRICORE_4OFF4},
  {BFD_RELOC_TRICORE_42OFF,     R_TRICORE_42OFF},
  {BFD_RELOC_TRICORE_42OFF2,    R_TRICORE_42OFF2},
  {BFD_RELOC_TRICORE_42OFF4,    R_TRICORE_42OFF4},
  {BFD_RELOC_TRICORE_2OFF,      R_TRICORE_2OFF},
  {BFD_RELOC_TRICORE_8CONST2,   R_TRICORE_8CONST2},
  {BFD_RELOC_TRICORE_4POS,      R_TRICORE_4POS},
  {BFD_RELOC_TRICORE_16SM2,     R_TRICORE_16SM2},
  {BFD_RELOC_TRICORE_5REL,      R_TRICORE_5REL},
  {BFD_RELOC_VTABLE_ENTRY,      R_TRICORE_GNU_VTENTRY},
  {BFD_RELOC_VTABLE_INHERIT,    R_TRICORE_GNU_VTINHERIT},
  {BFD_RELOC_TRICORE_PCREL16,	R_TRICORE_PCREL16},
  {BFD_RELOC_TRICORE_PCREL8,	R_TRICORE_PCREL8},
  {BFD_RELOC_TRICORE_GOT,       R_TRICORE_GOT},
  {BFD_RELOC_TRICORE_GOT2,      R_TRICORE_GOT2},
  {BFD_RELOC_TRICORE_GOTHI,     R_TRICORE_GOTHI},
  {BFD_RELOC_TRICORE_GOTLO,     R_TRICORE_GOTLO},
  {BFD_RELOC_TRICORE_GOTLO2,    R_TRICORE_GOTLO2},
  {BFD_RELOC_TRICORE_GOTUP,     R_TRICORE_GOTUP},
  {BFD_RELOC_TRICORE_GOTOFF,    R_TRICORE_GOTOFF},
  {BFD_RELOC_TRICORE_GOTOFF2,   R_TRICORE_GOTOFF2},
  {BFD_RELOC_TRICORE_GOTOFFHI,  R_TRICORE_GOTOFFHI},
  {BFD_RELOC_TRICORE_GOTOFFLO,  R_TRICORE_GOTOFFLO},
  {BFD_RELOC_TRICORE_GOTOFFLO2, R_TRICORE_GOTOFFLO2},
  {BFD_RELOC_TRICORE_GOTOFFUP,  R_TRICORE_GOTOFFUP},
  {BFD_RELOC_TRICORE_GOTPC,     R_TRICORE_GOTPC},
  {BFD_RELOC_TRICORE_GOTPC2,    R_TRICORE_GOTPC2},
  {BFD_RELOC_TRICORE_GOTPCHI,   R_TRICORE_GOTPCHI},
  {BFD_RELOC_TRICORE_GOTPCLO,   R_TRICORE_GOTPCLO},
  {BFD_RELOC_TRICORE_GOTPCLO2,  R_TRICORE_GOTPCLO2},
  {BFD_RELOC_TRICORE_GOTPCUP,   R_TRICORE_GOTPCUP},
  {BFD_RELOC_TRICORE_PLT,       R_TRICORE_PLT},
  {BFD_RELOC_TRICORE_COPY,      R_TRICORE_COPY},
  {BFD_RELOC_TRICORE_GLOB_DAT,  R_TRICORE_GLOB_DAT},
  {BFD_RELOC_TRICORE_JMP_SLOT,  R_TRICORE_JMP_SLOT},
  {BFD_RELOC_TRICORE_RELATIVE,  R_TRICORE_RELATIVE},
  {BFD_RELOC_TRICORE_BITPOS,    R_TRICORE_BITPOS}
};

static int nr_maps = sizeof tricore_reloc_map / sizeof tricore_reloc_map[0];

/* True if we should compress bit objects during the relaxation pass.  */

boolean tricore_elf32_relax_bdata = false;

/* True if we should relax call and jump instructions whose target
   addresses are out of reach.  */

boolean tricore_elf32_relax_24rel = false;

/* True if we should output diagnostic messages when relaxing sections.  */

boolean tricore_elf32_debug_relax = false;

/* If the linker was invoked with -M or -Map, we save the pointer to
   the map file in this variable; used to list allocated bit objects
   and other fancy extensions.  */

FILE *tricore_elf32_map_file = (FILE *) NULL;

/* If the linker was invoked with --extmap in addition to -M/-Map, we
   also save the filename of the map file (NULL means stdout).  */

char *tricore_elf32_map_filename = (char *) NULL;

/* True if an extended map file should be produced.  */

boolean tricore_elf32_extmap_enabled = false;

/* True if the map file should include the version of the linker, the
   date of the link run, and the name of the map file.  */

boolean tricore_elf32_extmap_header = false;

/* True if the map file should contain an augmented memory segment map.  */

boolean tricore_elf32_extmap_memory_segments = false;

/* 1 if global symbols should be listed in the map file, 2 if all symbols
   should be listed; symbols are sorted by name.  */

int tricore_elf32_extmap_syms_by_name = 0;

/* 1 if global symbols should be listed in the map file, 2 if all symbols
   should be listed; symbols are sorted by address.  */

int tricore_elf32_extmap_syms_by_addr = 0;

/* Name of the linker; only valid if tricore_elf32_extmap_enabled.  */

char *tricore_elf32_extmap_ld_name = (char *) NULL;

/* Pointer to a function that prints the linker version to a file;
   only valid if tricore_elf32_extmap_enabled.  */

void (*tricore_elf32_extmap_ld_version) PARAMS ((FILE *));

/* Pointer to a function that returns a list of defined memory regions;
   only valid if tricore_elf32_extmap_enabled.  */

memreg_t *(*tricore_elf32_extmap_get_memregs) PARAMS ((int *)) = NULL;

/* If >= 0, describes the address mapping scheme for PCP sections.  */

int tricore_elf32_pcpmap = -1;

/* True if PCP address mappings should be printed (for debugging only).  */

boolean tricore_elf32_debug_pcpmap = false;


/* Forward declarations.  */

static reloc_howto_type *tricore_elf32_reloc_type_lookup
     PARAMS ((bfd *, bfd_reloc_code_real_type));

static void tricore_elf32_info_to_howto
     PARAMS ((bfd *, arelent *, Elf_Internal_Rela *));

static void tricore_elf32_final_sda_bases
     PARAMS ((bfd *, struct bfd_link_info *));

static bfd_reloc_status_type tricore_elf32_final_sda_base
     PARAMS ((asection *, bfd_vma *, int *));

static boolean tricore_elf32_merge_private_bfd_data PARAMS ((bfd *, bfd *));

static boolean tricore_elf32_copy_private_bfd_data PARAMS ((bfd *, bfd *));

const bfd_target *tricore_elf32_object_p PARAMS ((bfd *));

static boolean tricore_elf32_fake_sections
     PARAMS ((bfd *, Elf32_Internal_Shdr *, asection *));

static boolean tricore_elf32_set_private_flags PARAMS ((bfd *, flagword));

static boolean tricore_elf32_section_flags
     PARAMS ((flagword *, Elf32_Internal_Shdr *));

static boolean tricore_elf32_final_gp
     PARAMS ((bfd *, struct bfd_link_info *));

static boolean tricore_elf32_relocate_section
     PARAMS ((bfd *, struct bfd_link_info *, bfd *, asection *, bfd_byte *,
              Elf_Internal_Rela *, Elf_Internal_Sym *, asection **));

static void tricore_elf32_set_arch_mach
     PARAMS ((bfd *, enum bfd_architecture));

static boolean tricore_elf32_size_dynamic_sections
     PARAMS ((bfd *, struct bfd_link_info *));

static boolean tricore_elf32_adjust_dynamic_symbol
     PARAMS ((struct bfd_link_info *, struct elf_link_hash_entry *));

static elf_linker_section_t *tricore_elf32_create_linker_section
     PARAMS ((bfd *, struct bfd_link_info *, enum elf_linker_section_enum));

static boolean tricore_elf32_check_relocs
     PARAMS ((bfd *, struct bfd_link_info *, asection *,
     	     const Elf_Internal_Rela *));

static unsigned long tricore_elf32_get_bitpos
     PARAMS ((bfd *, struct bfd_link_info *, Elf_Internal_Rela *,
             Elf_Internal_Shdr *, Elf_Internal_Sym *,
             struct elf_link_hash_entry **, asection *, boolean *));

static boolean tricore_elf32_adjust_bit_relocs
     PARAMS ((bfd *, struct bfd_link_info *, unsigned long,
             bfd_vma, bfd_vma, int, unsigned int));

static boolean tricore_elf32_relax_section
     PARAMS ((bfd *, asection *, struct bfd_link_info *, boolean *));

static void tricore_elf32_list_bit_objects PARAMS ((struct bfd_link_info *));

static symbol_t *tricore_elf32_new_symentry PARAMS ((void));

static void tricore_elf32_do_extmap PARAMS ((struct bfd_link_info *));

static int tricore_elf32_extmap_sort_addr PARAMS ((const void *, const void *));

static int tricore_elf32_extmap_sort_name PARAMS ((const void *, const void *));

static int tricore_elf32_extmap_sort_memregs
     PARAMS ((const void *, const void *));

static boolean tricore_elf32_extmap_add_sym
     PARAMS ((struct bfd_link_hash_entry *, PTR));

static boolean tricore_elf32_finish_dynamic_symbol
     PARAMS ((bfd *, struct bfd_link_info *,
             struct elf_link_hash_entry *, Elf_Internal_Sym *sym));

static boolean tricore_elf32_finish_dynamic_sections
     PARAMS ((bfd *, struct bfd_link_info *));

static enum elf_reloc_type_class tricore_elf32_reloc_type_class
     PARAMS ((const Elf_Internal_Rela *));

static asection *tricore_elf32_gc_mark_hook
     PARAMS ((asection *, struct bfd_link_info *, Elf_Internal_Rela *,
             struct elf_link_hash_entry *, Elf_Internal_Sym *));

static boolean tricore_elf32_gc_sweep_hook
     PARAMS ((bfd *, struct bfd_link_info *, asection *,
             const Elf_Internal_Rela *));


/* Given a BFD reloc type CODE, return the corresponding howto structure.  */

static reloc_howto_type *
tricore_elf32_reloc_type_lookup (abfd, code)
     bfd *abfd ATTRIBUTE_UNUSED;
     bfd_reloc_code_real_type code;
{
  int i;

  if (code == BFD_RELOC_8)
    code = BFD_RELOC_TRICORE_8ABS;
  else if (code == BFD_RELOC_16)
    code = BFD_RELOC_TRICORE_16ABS;
  else if (code == BFD_RELOC_32)
    code = BFD_RELOC_TRICORE_32ABS;
  else if (code == BFD_RELOC_32_PCREL)
    code = BFD_RELOC_TRICORE_32REL;
  else if (code == BFD_RELOC_16_PCREL)
    code = BFD_RELOC_TRICORE_PCREL16;
  else if (code == BFD_RELOC_8_PCREL)
    code = BFD_RELOC_TRICORE_PCREL8;
		   
  for (i = 0; i < nr_maps; ++i)
    if (tricore_reloc_map[i].bfd_reloc_val == code)
      return &tricore_elf32_howto_table[tricore_reloc_map[i].tricore_val];

  bfd_set_error (bfd_error_bad_value);
  return (reloc_howto_type *) 0;
}

/* Set CACHE_PTR->howto to the howto entry for the relocation DST.  */

static void
tricore_elf32_info_to_howto (abfd, cache_ptr, dst)
     bfd *abfd ATTRIBUTE_UNUSED;
     arelent *cache_ptr;
     Elf_Internal_Rela *dst;
{
  BFD_ASSERT (ELF32_R_TYPE (dst->r_info) < (unsigned int) R_TRICORE_max);
  cache_ptr->howto = &tricore_elf32_howto_table[ELF32_R_TYPE (dst->r_info)];
}

/* Return the class a relocation belongs to.  */

static enum elf_reloc_type_class
tricore_elf32_reloc_type_class (rela)
     const Elf_Internal_Rela *rela;
{
  switch ((int) ELF32_R_TYPE (rela->r_info))
    {
    case R_TRICORE_RELATIVE:
      return reloc_class_relative;

    case R_TRICORE_JMP_SLOT:
      return reloc_class_plt;

    case R_TRICORE_COPY:
      return reloc_class_copy;

    default:
      return reloc_class_normal;
    }
}

/* Create a special linker section (".sdata"/".sbss", or ".zdata"/".zbss",
   depending on the value of WHICH).  */

static elf_linker_section_t *
tricore_elf32_create_linker_section (abfd, info, which)
     bfd *abfd;
     struct bfd_link_info *info;
     enum elf_linker_section_enum which;
{
  bfd *dynobj = elf_hash_table (info)->dynobj;
  elf_linker_section_t *lsect;

  /* Record the first BFD that requested the special section.  */
  if (dynobj == NULL)
    dynobj = elf_hash_table (info)->dynobj = abfd;

  /* If this is the first time we're called, create the section.  */
  lsect = elf_linker_section (dynobj, which);
  if (lsect == NULL)
    {
      elf_linker_section_t defaults;
      static elf_linker_section_t zero_section;

      defaults = zero_section;
      defaults.which = which;
      defaults.hole_written_p = false;
      defaults.alignment = 3;
      defaults.flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS
      			| SEC_IN_MEMORY);
      switch (which)
        {
	default:
	  (*_bfd_error_handler)
	   (_("%s: Unknown special linker section type %d"),
	    bfd_get_filename (abfd), (int) which);
	  bfd_set_error (bfd_error_bad_value);
	  return (elf_linker_section_t *) 0;

	case LINKER_SECTION_SDATA:	/* .sdata/.sbss sections.  */
	  defaults.name	 = ".sdata";
	  defaults.rel_name = ".rela.sdata";
	  defaults.bss_name = ".sbss";
	  defaults.sym_name = "_SMALL_DATA_";
	  defaults.sym_offset = 32768;
	  break;

	case LINKER_SECTION_SDATA2:	/* .zdata/.zbss sections.  */
	  defaults.name	 = ".zdata";
	  defaults.rel_name = ".rela.zdata";
	  defaults.bss_name = ".zbss";
	  defaults.sym_name = "_ZERO_DATA_";
	  defaults.sym_offset = 0;
	  break;
	}

      lsect = _bfd_elf_create_linker_section (abfd, info, which, &defaults);
    }

  return lsect;
}

/* Look through the RELOCS for input section SEC (part of the input object
   pointed to by ABFD) during the first phase, and allocate space in the
   global offset and procedure linkage tables in case of dedicated GOT or
   PLT relocs; mark global symbols referenced by non-GOT/PLT relocs, as
   they might require allocating space in the executable's ".dynbss" section
   and associated COPY/JMP_SLOT relocs (only if this is a final link, and
   the referenced symbols turn out to be defined in a shared object; this
   will be checked later in tricore_elf32_adjust_dynamic_symbol, when we've
   seen all input sections).  Also, create the .sdata/.sbss sections and the
   _SMALL_DATA_ symbol, as required by the TriCore EABI.  All of this is not
   necessary if this is just a relocateable link run, which is indicated by
   the INFO->relocateable flag.  */

static boolean
tricore_elf32_check_relocs (abfd, info, sec, relocs)
     bfd *abfd;
     struct bfd_link_info *info;
     asection *sec;
     const Elf_Internal_Rela *relocs;
{
  bfd *dynobj;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_signed_vma *local_got_refcounts;
  const Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *rel_end;
  elf_linker_section_t *sdata/*, *zdata*/;
  asection *sgot;
  asection *srelgot;
  asection *sreloc;

  if (info->relocateable)
    return true;

  /* Create the ".sdata"/".sbss" sections all the time so that these
     sections and the special "_SMALL_DATA_" symbol are available.  */
  if ((sdata = elf_linker_section (abfd, LINKER_SECTION_SDATA)) == NULL)
    {
      sdata = tricore_elf32_create_linker_section (abfd, info,
      						   LINKER_SECTION_SDATA);
      if (sdata == NULL)
        return false;
    }

#if 0
  /* If this is a static link, do also create the .zbss/.zdata sections.  */
  /* Probably not a good idea; leave it up to the user to actually create
     absolute addressable sections.  */
  if (!info->shared
      && (zdata = elf_linker_section (abfd, LINKER_SECTION_SDATA2) == NULL))
    {
      zdata = tricore_elf32_create_linker_section (abfd, info,
      						   LINKER_SECTION_SDATA2);
      if (zdata == NULL)
        return false;
    }
#endif

  dynobj = elf_hash_table (info)->dynobj;
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  local_got_refcounts = elf_local_got_refcounts (abfd);

  sgot = NULL;
  srelgot = NULL;
  sreloc = NULL;

  rel_end = relocs + sec->reloc_count;
  for (rel = relocs; rel < rel_end; rel++)
    {
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;

      r_symndx = ELF32_R_SYM (rel->r_info);
      if (r_symndx < symtab_hdr->sh_info)
        h = NULL;
      else if (r_symndx < NUM_SHDR_ENTRIES (symtab_hdr))
        h = sym_hashes[r_symndx - symtab_hdr->sh_info];
      else
        {
          (*_bfd_error_handler) (_("%s: bad symbol index (%d)"),
			         bfd_archive_filename (abfd), r_symndx);
	  return false;
	}  

      switch (ELF32_R_TYPE (rel->r_info))
        {
        case R_TRICORE_GOTOFF:
	case R_TRICORE_GOTOFF2:
	case R_TRICORE_GOTOFFLO:
	case R_TRICORE_GOTOFFLO2:
	case R_TRICORE_GOTOFFHI:
	case R_TRICORE_GOTOFFUP:
        case R_TRICORE_GOTPC:
	case R_TRICORE_GOTPC2:
	case R_TRICORE_GOTPCLO:
	case R_TRICORE_GOTPCLO2:
	case R_TRICORE_GOTPCHI:
	case R_TRICORE_GOTPCUP:
	  /* These relocations require a GOT, but no GOT entry.  */
	  if (dynobj == NULL)
	    {
	      /* Remember the first BFD that requested a GOT and
	         create the ".got" section.  */
	      elf_hash_table (info)->dynobj = dynobj = abfd;
	      if (!_bfd_elf_create_got_section (dynobj, info))
	        return false;
	    }
	  break;  

        case R_TRICORE_GOT:
	case R_TRICORE_GOT2:
	case R_TRICORE_GOTLO:
	case R_TRICORE_GOTLO2:
	case R_TRICORE_GOTHI:
	case R_TRICORE_GOTUP:
	  /* These relocations actually require a GOT entry.  */
	  if (dynobj == NULL)
	    {
	      /* Remember the first BFD that requested a GOT and
	         create the ".got" section.  */
	      elf_hash_table (info)->dynobj = dynobj = abfd;
	      if (!_bfd_elf_create_got_section (dynobj, info))
	        return false;
	    }

	  /* Make sure there's a ".got" section.  */
	  if (sgot == NULL)
	    {
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      if (sgot == NULL)
	        {
		  if (!_bfd_elf_create_got_section (dynobj, info))
	            return false;

	          sgot = bfd_get_section_by_name (dynobj, ".got");
		}
	      BFD_ASSERT (sgot != NULL);
	    }

	  /* Make sure there's also a ".rela.got" section, so that
	     we can emit the necessary relocations for GOT entries.  */
	  if ((srelgot == NULL) && ((h != NULL) || info->shared)) 
	    {
	      srelgot = bfd_get_section_by_name (dynobj, ".rela.got");
	      if (srelgot == NULL)
	        {
		  srelgot = bfd_make_section (dynobj, ".rela.got");
		  if (srelgot == NULL)
	            return false;

	          if (!bfd_set_section_flags (dynobj, srelgot, RELGOTSECFLAGS))
	            return false;

	          if (!bfd_set_section_alignment (dynobj, srelgot, 2))
	            return false;
		}    
	    }

	  if (h != NULL)
	    {
	      /* This is a GOT entry for a global symbol.  */
	      if (h->got.refcount == 0)
	        {
		  /* Make sure this symbol is output as a dynamic symbol.  */
	          if (h->dynindx == -1)
	            if (!bfd_elf32_link_record_dynamic_symbol (info, h))
		      return false;

	          /* Allocate space in .got/.rela.got.  */
	          sgot->_raw_size += 4;
	          srelgot->_raw_size += sizeof (Elf32_External_Rela);
		}
	      h->got.refcount++;
	    }
	  else
	    {
	      /* This is a GOT entry for a local symbol.  */
	      if (local_got_refcounts == NULL)
	        {
		  bfd_size_type size;

		  size = symtab_hdr->sh_info * sizeof (bfd_signed_vma);
		  local_got_refcounts = ((bfd_signed_vma *)
		  			 bfd_zalloc (abfd, size));
		  if (local_got_refcounts == NULL)
		    return false;

		  elf_local_got_refcounts (abfd) = local_got_refcounts;
		}

	      if (local_got_refcounts[r_symndx] == 0)
	        {
		  /* Allocate space in .got.  */
	          sgot->_raw_size += 4;

	          /* If we are generating a shared object, we need to output
		     an R_TRICORE_RELATIVE reloc so that the dynamic linker
		     can adjust this GOT entry.  */
                  if (info->shared)
	            srelgot->_raw_size += sizeof (Elf32_External_Rela);
		}
	      local_got_refcounts[r_symndx]++;
	    }
	  break;

	case R_TRICORE_PLT:
	case R_TRICORE_24REL:
          /* This symbol might require a procedure linkage table entry.  We
             actually build the entry in tricore_elf32_adjust_dynamic_symbol,
             because this might be a case of linking PIC code without linking
	     in any dynamic objects, in which case we don't need to generate
	     a procedure linkage table after all.  */
	  if (h == NULL)
	    {
	      /* PLT entries for local (static) functions don't make
	         much sense in this shlib model, so we'll resolve this
		 reloc directly in tricore_elf32_relocate_section.  */
              break;
	    }
	  else
	    {
	      /* Make sure this symbol is output as a dynamic symbol.  */
	      if (h->dynindx == -1)
	        if (!bfd_elf32_link_record_dynamic_symbol (info, h))
		  return false;

	      h->elf_link_hash_flags |= ELF_LINK_HASH_NEEDS_PLT;
	      h->plt.refcount++;
	    }
	  break;

	case R_TRICORE_GNU_VTINHERIT:
	  /* This relocation describes a C++ object vtable hierarchy.
	     Record it for later use during GC.  */
	  if (!_bfd_elf32_gc_record_vtinherit (abfd, sec, h, rel->r_addend))
	    return false;

	  break;  

	case R_TRICORE_GNU_VTENTRY:
	  /* This relocation describes which C++ vtable entries are
	     actually used.  Record it for later use during GC.  */
	  if (!_bfd_elf32_gc_record_vtentry (abfd, sec, h, rel->r_addend))
	    return false;

	  break;

	case R_TRICORE_HIADJ:
	case R_TRICORE_HI:
	case R_TRICORE_LO:
	case R_TRICORE_LO2:
	  if ((h != NULL) && !info->shared)
	    {
	      /* If this symbol is defined in a SO, we might need a
	         copy reloc.  We haven't seen all input sections yet,
		 so we tentatively set the flag for now, and correct
		 it in tricore_elf32_adjust_dynamic_symbol.  */
              h->elf_link_hash_flags |= ELF_LINK_NON_GOT_REF;

	      /* The symbol may as well turn out to be a function defined
	         in some SO, in which case we need to create a PLT entry.  */
	      h->plt.refcount++;
	    }
	  break;

	case R_TRICORE_32ABS:
	case R_TRICORE_32REL:
	  if ((h != NULL) && !info->shared)
	    {
	      /* If this reloc is in a read-only section, we might
	         need a copy reloc.  We can't check reliably at this
		 stage whether the section is read-only, as input
		 sections have not yet been mapped to output sections.
		 Tentatively set the flag for now, and correct it in
		 tricore_elf32_adjust_dynamic_symbol.  */
              h->elf_link_hash_flags |= ELF_LINK_NON_GOT_REF;

	      /* We may need a PLT entry if this reloc refers to
	         a function in a shared library.  */
	      h->plt.refcount++;
	    }

	  /* When creating a shared object, we might need to copy these
	     relocs into the output file, so we have to create a reloc
	     section in dynobj and make room for the reloc.  */
	  if (!info->shared
	      || !(sec->flags & SEC_ALLOC)
	      || ((ELF32_R_TYPE (rel->r_info) == R_TRICORE_32REL)
	          && ((h == NULL)
		      || (info->symbolic
		      	  && (h->elf_link_hash_flags
			      & ELF_LINK_HASH_DEF_REGULAR)))))
	    break;  

	  if (sreloc == NULL)
	    {
	      const char *name;

	      name = (bfd_elf_string_from_elf_section
	      	      (abfd,
		      elf_elfheader (abfd)->e_shstrndx,
		      elf_section_data (sec)->rel_hdr.sh_name));
	      if (name == NULL)
	        return false;

	      BFD_ASSERT (!strncmp (name, ".rela", 5)
	      		  && !strcmp (bfd_get_section_name (abfd, sec),
			  	      name + 5));
	      sreloc = bfd_get_section_by_name (dynobj, name);
	      if (sreloc == NULL)
	        {
		  flagword flags;
		  
		  sreloc = bfd_make_section (dynobj, name);
		  flags = DYNOBJSECFLAGS;
		  if (sec->flags & SEC_ALLOC)
		    flags |= (SEC_ALLOC | SEC_LOAD);
		  if ((sreloc == NULL)
		      || !bfd_set_section_flags (dynobj, sreloc, flags)
		      || !bfd_set_section_alignment (dynobj, sreloc, 2))
		    return false;  
		}
	      if (sec->flags & SEC_READONLY)
	        info->flags |= DF_TEXTREL;
	    }

	  sreloc->_raw_size += sizeof (Elf32_External_Rela);
	  break;

	default:
	  break;
	}
    }

  return true;
}

/* Return the section that should be marked against GC for a given
   relocation.  */

static asection *
tricore_elf32_gc_mark_hook (sec, info, rel, h, sym)
     asection *sec;
     struct bfd_link_info *info ATTRIBUTE_UNUSED;
     Elf_Internal_Rela *rel;
     struct elf_link_hash_entry *h;
     Elf_Internal_Sym *sym;
{
  if (h != NULL)
    {
      switch (ELF32_R_TYPE (rel->r_info))
        {
        case R_TRICORE_GNU_VTINHERIT:
        case R_TRICORE_GNU_VTENTRY:
          break;

        default:
          switch (h->root.type)
            {
            case bfd_link_hash_defined:
            case bfd_link_hash_defweak:
              return h->root.u.def.section;

            case bfd_link_hash_common:
              return h->root.u.c.p->section;

            default:
              break;
            }
         }
    }
  else
    return bfd_section_from_elf_index (sec->owner, sym->st_shndx);

  return NULL;
}

/* Update the GOT/PLT entry reference counts for the section being removed
   (because the garbage collection pass has tagged it as disposable).  */

static boolean
tricore_elf32_gc_sweep_hook (abfd, info, sec, relocs)
     bfd *abfd;
     struct bfd_link_info *info ATTRIBUTE_UNUSED;
     asection *sec;
     const Elf_Internal_Rela *relocs;
{
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_signed_vma *local_got_refcounts;
  const Elf_Internal_Rela *rel, *relend;
  unsigned long r_symndx;
  struct elf_link_hash_entry *h;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  local_got_refcounts = elf_local_got_refcounts (abfd);

  relend = relocs + sec->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    switch (ELF32_R_TYPE (rel->r_info))
      {
      case R_TRICORE_GOT:
      case R_TRICORE_GOT2:
      case R_TRICORE_GOTLO:
      case R_TRICORE_GOTLO2:
      case R_TRICORE_GOTHI:
      case R_TRICORE_GOTUP:
        r_symndx = ELF32_R_SYM (rel->r_info);
        if (r_symndx >= symtab_hdr->sh_info)
          {
            h = sym_hashes[r_symndx - symtab_hdr->sh_info];
            if (h->got.refcount > 0)
              h->got.refcount--;
          }
        else if (local_got_refcounts != NULL)
          {
            if (local_got_refcounts[r_symndx] > 0)
              local_got_refcounts[r_symndx]--;
          }
        break;

      case R_TRICORE_PLT:
      case R_TRICORE_24REL:
        r_symndx = ELF32_R_SYM (rel->r_info);
        if (r_symndx >= symtab_hdr->sh_info)
          {
            h = sym_hashes[r_symndx - symtab_hdr->sh_info];
            if (h->plt.refcount > 0)
              h->plt.refcount--;
          }
        break;

      default:
        break;
      }

  return true;
}

/* Adjust a symbol defined by a dynamic object and referenced by a
   regular object.  The current definition is in some section of the
   dynamic object, but we're not including those sections.  We have to
   change the definition to something the rest of the link can understand.  */

static boolean
tricore_elf32_adjust_dynamic_symbol (info, h)
     struct bfd_link_info *info;
     struct elf_link_hash_entry *h;
{
  bfd *dynobj;
  asection *s;
  unsigned int power_of_two;

  dynobj = elf_hash_table (info)->dynobj;

  /* Make sure we know what is going on here.  */
  BFD_ASSERT ((dynobj != NULL)
              && ((h->elf_link_hash_flags & ELF_LINK_HASH_NEEDS_PLT)
                  || (h->weakdef != NULL)
                  || (((h->elf_link_hash_flags
                        & ELF_LINK_HASH_DEF_DYNAMIC) != 0)
                      && ((h->elf_link_hash_flags
                           & ELF_LINK_HASH_REF_REGULAR) != 0)
                      && ((h->elf_link_hash_flags
                           & ELF_LINK_HASH_DEF_REGULAR) == 0))));

  /* If this is a function, put it in the procedure linkage table.  We
     will fill in the contents of the procedure linkage table later,
     when we know the address of the .got section.  */

  if ((h->type == STT_FUNC)
      || (h->elf_link_hash_flags & ELF_LINK_HASH_NEEDS_PLT))
    {
      if (! elf_hash_table (info)->dynamic_sections_created
          || SYMBOL_CALLS_LOCAL (info, h)
          || (info->shared && (h->plt.refcount <= 0)))
        {
          /* A PLT entry is not required/allowed when:

             1. We are not using "ld.so"; because then the PLT entry
             can't be set up, so we can't use one.

             2. We know for certain that a call to this symbol
             will go to this object.

             3. GC has rendered the entry unused.
             Note, however, that in an executable all references to the
             symbol go to the PLT, so we can't turn it off in that case.
             ??? The correct thing to do here is to reference count
             all uses of the symbol, not just those to the GOT or PLT.  */
          h->plt.offset = (bfd_vma) -1;
          h->elf_link_hash_flags &= ~ELF_LINK_HASH_NEEDS_PLT;
          return true;
        }

      /* Make sure this symbol is output as a dynamic symbol.  */
      if (h->dynindx == -1)
        {
          if (!bfd_elf32_link_record_dynamic_symbol (info, h))
            return false;
        }
      BFD_ASSERT (h->dynindx != -1);

      s = bfd_get_section_by_name (dynobj, ".plt");
      BFD_ASSERT (s != NULL);

      /* If this is the first PLT entry, make room for the special
         first entries.  */
      if (s->_raw_size == 0)
        s->_raw_size = PLT_RESERVED_SLOTS * PLT_ENTRY_SIZE;

      /* There's a hw limit for the size of a PLT due to the 24-bit
         offset of jump/call instructions; it might well be reached
	 before the ".plt" section hits the 16 MB upper limit, as
	 PLT entries are called from code sections, but this will
	 be checked later in tricore_elf32_relocate_section.  */
      if (s->_raw_size >= 0x01000000)
        {
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	}  

      /* If this symbol is not defined in a regular file, and we are
         not generating a shared library, then set the symbol to this
         location in the .plt.  This is required to make function
         pointers compare as equal between the normal executable and
         the shared library.  */
      if (!info->shared
          && ((h->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) == 0))
        {
          h->root.u.def.section = s;
          h->root.u.def.value = s->_raw_size;
        }
      h->plt.offset = s->_raw_size;

      /* Make room for this entry.  */
      s->_raw_size += PLT_ENTRY_SIZE;

      /* We also need to make an entry in the ".rela.plt" section.  */
      s = bfd_get_section_by_name (dynobj, ".rela.plt");
      BFD_ASSERT (s != NULL);
      s->_raw_size += sizeof (Elf32_External_Rela);

      return true;
    }
  else
    h->plt.offset = (bfd_vma) -1;

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.  */
  if (h->weakdef != NULL)
    {
      BFD_ASSERT (h->weakdef->root.type == bfd_link_hash_defined
                  || h->weakdef->root.type == bfd_link_hash_defweak);
      h->root.u.def.section = h->weakdef->root.u.def.section;
      h->root.u.def.value = h->weakdef->root.u.def.value;
      return true;
    }

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function (i.e., a data object).  If we are creating a shared
     library, we must presume that the only references to the symbol are
     via the global offset table.  For such cases we need not do anything
     here; the relocations will be handled correctly by the function
     tricore_elf32_relocate_section.  */
  if (info->shared)
    return true;

  /* If there are no references to this symbol that do not use the
     GOT, we don't need to generate a copy reloc.  */
  if ((h->elf_link_hash_flags & ELF_LINK_NON_GOT_REF) == 0)
    return true;

  /* If "-z nocopyreloc" was given, we won't generate them either.  */
  if (info->nocopyreloc)
    {
      h->elf_link_hash_flags &= ~ELF_LINK_NON_GOT_REF;
      return true;
    }

  /* We must allocate the symbol in our ".dynbss" section, which will
     become part of the ".bss" section of the executable.  There will be
     an entry for this symbol in the ".dynsym" section.  The dynamic
     object will contain position independent code, so all references
     from the dynamic object to this symbol will go through the global
     offset table.  The dynamic linker will use the ".dynsym" entry to
     determine the address it must put in the global offset table, so
     both the dynamic object and the regular object will refer to the
     same memory location for the variable.  */
  s = bfd_get_section_by_name (dynobj, ".dynbss");
  BFD_ASSERT (s != NULL);

  /* We must generate a R_TRICORE_COPY reloc to tell the dynamic linker
     to copy the initial value out of the dynamic object and into the
     runtime process image.  We need to remember the offset into the
     ".rel.bss" section we are going to use.  */
  if ((h->root.u.def.section->flags & SEC_ALLOC) != 0)
    {
      asection *srel;

      srel = bfd_get_section_by_name (dynobj, ".rela.bss");
      BFD_ASSERT (srel != NULL);
      srel->_raw_size += sizeof (Elf32_External_Rela);
      h->elf_link_hash_flags |= ELF_LINK_HASH_NEEDS_COPY;
    }

  /* We need to figure out the alignment required for this symbol.  I
     have no idea how other ELF linkers handle this, but I guess an
     alignment of 8 bytes should be sufficient.  */
  power_of_two = bfd_log2 (h->size);
  if (power_of_two > 3)
    power_of_two = 3;

  /* Apply the required alignment.  */
  s->_raw_size = BFD_ALIGN (s->_raw_size,
                            (bfd_size_type) (1 << power_of_two));
  if (power_of_two > bfd_get_section_alignment (dynobj, s))
    if (! bfd_set_section_alignment (dynobj, s, power_of_two))
      return false;

  /* Define the symbol as being at this point in the section.  */
  h->root.u.def.section = s;
  h->root.u.def.value = s->_raw_size;

  /* Increment the section size to make room for the symbol.  */
  s->_raw_size += h->size;

  return true;
}

/* Set the sizes of the dynamic sections.  */

static boolean
tricore_elf32_size_dynamic_sections (output_bfd, info)
     bfd *output_bfd ATTRIBUTE_UNUSED;
     struct bfd_link_info *info;
{
  bfd *dynobj;
  asection *s;
  boolean plt, relocs;

  dynobj = elf_hash_table (info)->dynobj;
  BFD_ASSERT (dynobj != NULL);

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Set the contents of the ".interp" section to the interpreter.  */
      if (!info->shared)
        {
          s = bfd_get_section_by_name (dynobj, ".interp");
          BFD_ASSERT (s != NULL);
          s->_raw_size = sizeof ELF_DYNAMIC_INTERPRETER;
          s->contents = (unsigned char *) ELF_DYNAMIC_INTERPRETER;
        }
    }
  else
    {
      /* We may have created entries in the ".rela.got" section.
         However, if we are not creating the dynamic sections, we will
         not actually use these entries.  Reset the size of ".rela.got",
         which will cause it to get stripped from the output file below.  */
      s = bfd_get_section_by_name (dynobj, ".rela.got");
      if (s != NULL)
        s->_raw_size = 0;
    }

  /* The tricore_elf32_check_relocs and tricore_elf32_adjust_dynamic_symbol
     functions have determined the sizes of the various dynamic sections;
     now allocate memory for them.  */
  plt = relocs = false;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      const char *name;
      boolean strip;

      if ((s->flags & SEC_LINKER_CREATED) == 0)
        continue;

      /* It's OK to base decisions on the section name, because none
         of the dynobj section names depend upon the input files.  */
      name = bfd_get_section_name (dynobj, s);

      strip = false;

      if (!strcmp (name, ".plt"))
        {
          if (s->_raw_size == 0)
            {
              /* Strip this section if we don't need it; see the
                 comment below.  */
              strip = true;
            }
          else
            {
              /* Remember whether there is a PLT.  */
              plt = true;
            }
        }
      else if (!strncmp (name, ".rela", 5))
        {
          if (s->_raw_size == 0)
            {
              /* If we don't need this section, strip it from the
                 output file.  This is mostly to handle ".rela.bss"
		 and ".rela.plt".  We must create both sections in
                 elf_backend_create_dynamic_sections, because they
		 must be created before the linker maps input sections
		 to output sections.  The linker does that before
                 tricore_elf32_adjust_dynamic_symbol is called, and it
		 is that function which decides whether anything needs
		 to go into these sections.  */
              strip = true;
            }
          else
            {
              /* Remember whether there are any relocation sections.  */
              relocs = true;

              /* We use the reloc_count field as a counter if we need
                 to copy relocs into the output file.  */
              s->reloc_count = 0;
            }
        }
      else if (strcmp (name, ".got") != 0)
        {
          /* It's not one of our sections, so don't allocate space.  */
          continue;
        }

      if (strip)
        {
          _bfd_strip_section_from_output (info, s);
          continue;
        }

      /* Allocate memory for the section contents.  */
      s->contents = (bfd_byte *) bfd_zalloc (dynobj, s->_raw_size);
      if ((s->contents == NULL) && (s->_raw_size != 0))
        return false;
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Add some entries to the ".dynamic" section.  We fill in the
         values later, in tricore_elf32_finish_dynamic_sections, but we
         must add the entries now so that we get the correct size for
         the ".dynamic" section.  The DT_DEBUG entry is filled in by the
         dynamic linker and used by the debugger.  */
#define add_dynamic_entry(TAG, VAL) \
  bfd_elf32_add_dynamic_entry (info, (bfd_vma) (TAG), (bfd_vma) (VAL))

      if (!info->shared)
        {
          if (!add_dynamic_entry (DT_DEBUG, 0))
            return false;
        }

      if (plt)
        {
          if (!add_dynamic_entry (DT_PLTGOT, 0)
              || !add_dynamic_entry (DT_PLTRELSZ, 0)
              || !add_dynamic_entry (DT_PLTREL, DT_RELA)
              || !add_dynamic_entry (DT_JMPREL, 0))
            return false;
        }

      if (relocs)
        {
          if (!add_dynamic_entry (DT_RELA, 0)
              || !add_dynamic_entry (DT_RELASZ, 0)
              || !add_dynamic_entry (DT_RELAENT, sizeof (Elf32_External_Rela)))
            return false;
        }
      if (info->flags & DF_TEXTREL)
        if (!add_dynamic_entry (DT_TEXTREL, 0))
          return false;
    }
#undef add_dynamic_entry

  return true;
}

/* Finish up dynamic symbol handling.  We set the contents of various
   dynamic sections here.  */

static boolean
tricore_elf32_finish_dynamic_symbol (output_bfd, info, h, sym)
     bfd *output_bfd;
     struct bfd_link_info *info;
     struct elf_link_hash_entry *h;
     Elf_Internal_Sym *sym;
{
  bfd *dynobj;

  dynobj = elf_hash_table (info)->dynobj;
  BFD_ASSERT (dynobj != NULL);

  if (h->plt.offset != (bfd_vma) -1)
    {
      asection *splt;
      asection *srela;
      Elf_Internal_Rela rela;
      bfd_vma insn, diff;

      /* This symbol has an entry in the PLT.  Set it up.  */
      BFD_ASSERT (h->dynindx != -1);
      splt = bfd_get_section_by_name (dynobj, ".plt");
      srela = bfd_get_section_by_name (dynobj, ".rela.plt");
      BFD_ASSERT ((splt != NULL) && (srela != NULL));

      /* The first word is a call to the beginning of the PLT.  */
      insn = 0x0000006d;
      diff = -(h->plt.offset) >> 1;
      insn |= ((diff & 0xffff) << 16);
      insn |= ((diff & 0xff0000) >> 8);
      bfd_put_32 (output_bfd, insn, splt->contents + h->plt.offset);
      /* The second word is "lea %a2,[%a2]lo:0".  */
      bfd_put_32 (output_bfd, 0x000022d9, splt->contents + h->plt.offset + 4);
      /* The third word is "nop; ji %a2".  */
      bfd_put_32 (output_bfd, 0x02dc0000, splt->contents + h->plt.offset + 8);

      /* Fill in the entry in the ".rela.plt" section.  */
      rela.r_offset = (splt->output_section->vma
                       + splt->output_offset
                       + h->plt.offset);
      rela.r_info = ELF32_R_INFO (h->dynindx, R_TRICORE_JMP_SLOT);
      rela.r_addend = 0;
      bfd_elf32_swap_reloca_out (output_bfd, &rela,
                                 ((Elf32_External_Rela *) srela->contents
                                  + h->plt.offset / PLT_ENTRY_SIZE
				  - PLT_RESERVED_SLOTS));

      if ((h->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) == 0)
        {
          /* Mark the symbol as undefined, rather than as defined in
             the ".plt" section.  Leave the value alone.  */
          sym->st_shndx = SHN_UNDEF;

          /* If the symbol is weak, we do need to clear the value.
             Otherwise, the PLT entry would provide a definition for
             the symbol even if the symbol wasn't defined anywhere,
             and so the symbol would never be NULL.  */
          if (!(h->elf_link_hash_flags & ELF_LINK_HASH_REF_REGULAR_NONWEAK))
            sym->st_value = 0;
        }
    }

  if (h->got.offset != (bfd_vma) -1)
    {
      asection *sgot;
      asection *srela;
      Elf_Internal_Rela rela;

      /* This symbol has an entry in the GOT.  Set it up.  */
      sgot = bfd_get_section_by_name (dynobj, ".got");
      srela = bfd_get_section_by_name (dynobj, ".rela.got");
      BFD_ASSERT (sgot != NULL && srela != NULL);

      rela.r_offset = (sgot->output_section->vma
                       + sgot->output_offset
                       + (h->got.offset &~ (bfd_vma) 1));

      /* If this is a "-Bsymbolic" link, and the symbol is defined
         locally, we just want to emit a RELATIVE reloc.  Likewise if
         the symbol was forced to be local because of a version file.
         The entry in the global offset table will already have been
         initialized in the tricore_elf32_relocate_section function.  */
      if (info->shared && SYMBOL_REFERENCES_LOCAL (info, h))
        {
          rela.r_info = ELF32_R_INFO (0, R_TRICORE_RELATIVE);
          rela.r_addend = (h->root.u.def.value
                           + h->root.u.def.section->output_section->vma
                           + h->root.u.def.section->output_offset);
        }
      else
        {
	  BFD_ASSERT ((h->got.offset & 1) == 0);
          bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents + h->got.offset);
          rela.r_info = ELF32_R_INFO (h->dynindx, R_TRICORE_GLOB_DAT);
	  rela.r_addend = 0;
        }

      bfd_elf32_swap_reloca_out (output_bfd, &rela,
                                 ((Elf32_External_Rela *) srela->contents
                                  + srela->reloc_count));
      ++srela->reloc_count;
    }

  if ((h->elf_link_hash_flags & ELF_LINK_HASH_NEEDS_COPY) != 0)
    {
      asection *s;
      Elf_Internal_Rela rela;

      /* This symbols needs a copy reloc.  Set it up.  */
      BFD_ASSERT (h->dynindx != -1);

      s = bfd_get_section_by_name (h->root.u.def.section->owner,
                                   ".rela.bss");
      BFD_ASSERT (s != NULL);
      rela.r_offset = (h->root.u.def.value
                       + h->root.u.def.section->output_section->vma
                       + h->root.u.def.section->output_offset);
      rela.r_info = ELF32_R_INFO (h->dynindx, R_TRICORE_COPY);
      rela.r_addend = 0;
      bfd_elf32_swap_reloca_out (output_bfd, &rela,
                                 ((Elf32_External_Rela *) s->contents
                                  + s->reloc_count));
      ++s->reloc_count;
    }

  /* Mark some specially defined symbols as absolute.  */
  if (!strcmp (h->root.root.string, "_DYNAMIC")
      || !strcmp (h->root.root.string, "_GLOBAL_OFFSET_TABLE_")
      || !strcmp (h->root.root.string, "_PROCEDURE_LINKAGE_TABLE_")
      || !strcmp (h->root.root.string, "_SMALL_DATA_")
      || !strcmp (h->root.root.string, "_SMALL_DATA2_")
      || !strcmp (h->root.root.string, "_SMALL_DATA3_")
      || !strcmp (h->root.root.string, "_SMALL_DATA4_")
      || !strcmp (h->root.root.string, "_ZERO_DATA_"))
    sym->st_shndx = SHN_ABS;

  return true;
}

/* Finish up the dynamic sections; this is called pretty late in the
   link process, after all relocations have been performed.  */

static boolean
tricore_elf32_finish_dynamic_sections (output_bfd, info)
     bfd *output_bfd;
     struct bfd_link_info *info;
{
  bfd *dynobj = elf_hash_table (info)->dynobj;
  asection *sdyn, *sgot;

  BFD_ASSERT (dynobj != NULL);
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");
  sgot = bfd_get_section_by_name (dynobj, ".got");

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      asection *splt;
      Elf32_External_Dyn *dyncon, *dynconend;

      splt = bfd_get_section_by_name (dynobj, ".plt");
      BFD_ASSERT ((splt != NULL) && (sdyn != NULL));

      dyncon = (Elf32_External_Dyn *) sdyn->contents;
      dynconend = (Elf32_External_Dyn *) (sdyn->contents + sdyn->_raw_size);
      for (/* Empty.  */; dyncon < dynconend; dyncon++)
        {
          Elf_Internal_Dyn dyn;
          const char *name;
          boolean size;

          bfd_elf32_swap_dyn_in (dynobj, dyncon, &dyn);

          switch (dyn.d_tag)
            {
            case DT_PLTGOT:   name = ".plt";      size = false; break;
            case DT_PLTRELSZ: name = ".rela.plt"; size = true;  break;
            case DT_JMPREL:   name = ".rela.plt"; size = false; break;
            default:          name = NULL;        size = false; break;
            }

          if (name != NULL)
            {
              asection *s;

              s = bfd_get_section_by_name (output_bfd, name);
              if (s == NULL)
                dyn.d_un.d_val = 0;
              else
                {
                  if (size)
                    {
		      /* Not sure if anyone has actually set up the cooked
		         size, but just in case we're assuming it's valid.  */
                      if (s->_cooked_size != 0)
                        dyn.d_un.d_val = s->_cooked_size;
                      else
                        dyn.d_un.d_val = s->_raw_size;
                    }
                  else
                    dyn.d_un.d_ptr = s->vma;
                }
              bfd_elf32_swap_dyn_out (output_bfd, &dyn, dyncon);
            }
        }

      /* Clear the beginning of the PLT.  */
      if (splt->_raw_size > 0)
        memset (splt->contents, 0, PLT_RESERVED_SLOTS * PLT_ENTRY_SIZE);

      /* Set PLT entry size (i.e., the number of bytes a PLT entry takes).  */
      elf_section_data (splt->output_section)->this_hdr.sh_entsize =
        PLT_ENTRY_SIZE;
    }

  /* Set the entry at "_GLOBAL_OFFSET_TABLE_[0]" to the address of
     the dynamic section.  */
  if (sgot && (sgot->_raw_size > 0))
    {
      if (sdyn == NULL)
        bfd_put_32 (output_bfd, (bfd_vma) 0,
		    sgot->contents
		    + elf_hash_table (info)->hgot->root.u.def.value);
      else
        bfd_put_32 (output_bfd,
                    sdyn->output_section->vma + sdyn->output_offset,
                    sgot->contents
		    + elf_hash_table (info)->hgot->root.u.def.value);

      /* Set entry size for ".got" (4 bytes per entry).  */
      elf_section_data (sgot->output_section)->this_hdr.sh_entsize = 4;
    }

  return true;
}

/* This is called once from tricore_elf32_relocate_section, before any
   relocation has been performed.  We need to determine the final offset
   for the GOT pointer, which is currently zero, meaning that the symbol
   "_GLOBAL_OFFSET_TABLE_" will point to the beginning of the ".got"
   section.  There are, however, two cases in which this is undesirable:

      1. If the GOT contains more than 8192 entries, single TriCore
	 instructions can't address the excessive entries with their
	 16-bit signed offset.  Of course, that's only a problem
	 when there are modules compiled with "-fpic".

      2. In a shared object, the GOT pointer is also used to address
         variables in SDA0, so the combined size of the ".got", ".sbss"
	 and ".sdata" sections must not exceed 32k (well, of course the
	 combined size of these sections can be 64k, but not if the
	 GOT pointer offset is zero).

   To address these problems, we use the following algorithm to determine
   the final GOT offset:

      1. If the combined size of the ".got", ".sbss" and ".sdata"
	 sections is <= 32k, we'll keep the zero offset.

      2. If the GOT contains more than 8192 entries, we'll
	 set the offset to 0x8000, unless doing that would
	 render any SDA entries unaccessible.

      3. In all other cases, we'll set the offset to the size
	 of the ".got" section minus 4 (because of the _DYNAMIC
	 entry at _GLOBAL_OFFSET_TABLE_[0]).
	 
   In any case, if either ".sdata" or ".sbss" is non-empty, we're adjusting
   the symbol "_SMALL_DATA_" to have the same value as "_GLOBAL_OFFSET_TABLE_",
   as both the GOT and the SDA are addressed via the same register (%a12).
   Note that the algorithm described above won't guarantee that all GOT
   and SDA entries are reachable using a 16-bit offset -- it's just
   increasing the probability for this to happen.  */

static boolean
tricore_elf32_final_gp (output_bfd, info)
     bfd *output_bfd;
     struct bfd_link_info *info;
{
  asection *sgot;

  sgot = bfd_get_section_by_name (output_bfd, ".got");
  if (sgot && (sgot->_raw_size > 0))
    {
      struct elf_link_hash_entry *h;
      bfd_vma gp, sda;
      asection *sdata = NULL, *sbss = NULL;
      long got_size = sgot->_raw_size, sda_size = 0;
      long gp_offset;

      if (info->shared)
        {
	  sdata = bfd_get_section_by_name (output_bfd, ".sdata");
          sbss = bfd_get_section_by_name (output_bfd, ".sbss");
          if (sdata != NULL)
            {
              if (sdata->_cooked_size != 0)
	        sda_size += sdata->_cooked_size;
	      else
	        sda_size += sdata->_raw_size;
	    }

          if (sbss != NULL)
            {
              if (sbss->_cooked_size != 0)
	        sda_size += sbss->_cooked_size;
	      else
	        sda_size += sbss->_raw_size;
	    }

          if (sda_size > (0x8000 - 4))
            {
	      (*_bfd_error_handler) (_("%s: Too many SDA entries (%ld bytes)"),
	  			     bfd_archive_filename (output_bfd),
				     sda_size);
	      return false;
	    }
        }

      if ((got_size + sda_size) <= 0x8000)
        gp_offset = 0;
      else if ((got_size > 0x8000)
	       && ((sda_size + got_size - 0x8000) <= 0x8000))
	gp_offset = 0x8000;
      else
        gp_offset = got_size - 4;

      if (gp_offset != 0)
	elf_hash_table (info)->hgot->root.u.def.value = gp_offset;

      /* If there's any data in ".sdata"/".sbss", set the value of
         _SMALL_DATA_ to that of the GOT pointer.  */
      if (((sdata != NULL) && (sdata->_raw_size > 0))
          || ((sbss != NULL) && (sbss->_raw_size > 0)))
        {
	  h = (struct elf_link_hash_entry *)
	       bfd_link_hash_lookup (info->hash, "_SMALL_DATA_",
	  			     false, false, false);
	  if (h == NULL)
	    {
	      /* This can't possibly happen, as we're always creating the
	         ".sdata"/".sbss" output sections and the "_SMALL_DATA_"
		 symbol in tricore_elf32_check_relocs.  */
	      (*_bfd_error_handler)
	       (_("%s: SDA entries, but _SMALL_DATA_ undefined"),
	       bfd_archive_filename (output_bfd));
	      return false;
	    }
	  gp = gp_offset + sgot->output_section->vma + sgot->output_offset;
	  sdata = h->root.u.def.section;
	  sda = sdata->output_section->vma + sdata->output_offset;
	  h->root.u.def.value = gp - sda;
	}
    }

  return true;
}

/* This is called once from tricore_elf32_relocate_section, before any
   relocation has been performed.  We need to determine the final values
   for the various small data area base pointers, so that the 1[06]SM
   relocations (if any) can be handled correctly.  Of course, this is
   only necessary for a final link.  Note, however, that if this is a
   final dynamic link, ".sdata"/".sbss" will be adressed via %a12 instead
   of %a0, and the base pointer value for this particular area has
   already been computed by tricore_elf32_final_gp above.  */

static void
tricore_elf32_final_sda_bases (output_bfd, info)
     bfd *output_bfd;
     struct bfd_link_info *info;
{
  sda_t *sda;
  int i;
  asection *data, *bss;
  struct bfd_link_hash_entry *h;

  if (info->relocateable)
    return;

  for (sda = small_data_areas, i = 0; i < NR_SDAS; ++sda, ++i)
    {
      data = sda->data_section =
        bfd_get_section_by_name (output_bfd, sda->data_section_name);
      bss = sda->bss_section =
        bfd_get_section_by_name (output_bfd, sda->bss_section_name);
      if (!data && !bss)
        continue;

      if (!data)
        data = bss;
      h = bfd_link_hash_lookup (info->hash, sda->sda_symbol_name,
      				true, false, true);
      if ((i == 0) && info->shared)
        {
	  sda->areg = 12;
	  BFD_ASSERT (h->type == bfd_link_hash_defined);
	}
      else if (h->type != bfd_link_hash_defined)
        {
          h->u.def.value = 32768;
	  h->u.def.section = data;
	  h->type = bfd_link_hash_defined;
	}
      sda->gp_value = (h->u.def.value
		       + h->u.def.section->output_section->vma
		       + h->u.def.section->output_offset);
      sda->valid = true;
    }
}

/* This is called by tricore_elf32_relocate_section for each 1[06]SM
   relocation.  Check if the target of the relocation is within a known
   and defined output section OSEC (i.e., .s{data,bss}{,2,3,4}), and if
   so, return bfd_reloc_ok and the respective SDA base address and base
   address register in *SDA_BASE and *SDA_REG.  Otherwise, return
   bfd_reloc_dangerous to indicate an error.  */

static bfd_reloc_status_type
tricore_elf32_final_sda_base (osec, sda_base, sda_reg)
     asection *osec;
     bfd_vma *sda_base;
     int *sda_reg;
{
  sda_t *sda;
  int i;

  for (sda = small_data_areas, i = 0; i < NR_SDAS; ++sda, ++i)
    {
      if (!sda->valid)
        continue;

      if ((sda->data_section == osec) || (sda->bss_section == osec))
        {
	  *sda_base = sda->gp_value;
	  *sda_reg = sda->areg;
	  return bfd_reloc_ok;
	}
    }

  return bfd_reloc_dangerous;
}

/* Relocate a TriCore ELF section.  This function is responsible for
   adjusting the section contents as necessary, and, if generating a
   relocateable output file, adjusting the reloc addend as necessary.
   This function does not have to worry about setting the reloc
   address or the reloc symbol index.  LOCAL_SYMS is a pointer to
   the swapped-in local symbols.  LOCAL_SECTIONS is an array giving
   the section in the input file corresponding to the st_shndx field
   of each local symbol.  The global hash table entry for the global
   symbols can be found via elf_sym_hashes (input_bfd).  When generating
   relocateable output, this function must handle STB_LOCAL/STT_SECTION
   symbols specially: the output symbol is going to be the section
   symbol corresponding to the output section, which means that the
   addend must be adjusted accordingly.  */

static boolean
tricore_elf32_relocate_section (output_bfd, info, input_bfd, input_section,
			        contents, relocs, local_syms, local_sections)
     bfd *output_bfd;
     struct bfd_link_info *info;
     bfd *input_bfd;
     asection *input_section;
     bfd_byte *contents;
     Elf_Internal_Rela *relocs;
     Elf_Internal_Sym *local_syms;
     asection **local_sections;
{
  struct elf_link_hash_entry **sym_hashes = elf_sym_hashes (input_bfd), *h;
  unsigned long r_symndx, insn;
  int sda_reg, rel_abs, len32 = 0, r_type;
  const char *sym_name, *sec_name, *errmsg = NULL;
  static boolean final_gp = false;
  Elf_Internal_Shdr *symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  Elf_Internal_Sym *sym;
  Elf_Internal_Rela *rel, *relend;
  bfd *dynobj = elf_hash_table (info)->dynobj;
  bfd_vma *local_got_offsets = elf_local_got_offsets (input_bfd);
  bfd_vma got_base = 0, got_ptr = 0, sda_base, addend, offset, relocation;
  bfd_reloc_status_type r;
  bfd_byte *byte_ptr = NULL;
  asection *sgot = NULL, *splt = NULL, *sreloc = NULL, *sec;
  reloc_howto_type *howto;
  boolean will_become_local, unresolved_reloc, bitpos_seen = false;
  boolean do_pcpmap = false;
  boolean ret = true; /* Assume success.  */

#define CHECK_DISPLACEMENT(off,min,max)					\
  if (off & 1)								\
    {									\
      errmsg = _("Displacement is not even");				\
      break;								\
    }									\
  else if (((int) (off) < (min)) || ((int) (off) > (max)))		\
    {									\
      errmsg = _("Displacement overflow");				\
      break;								\
    }

  if (!info->relocateable)
    {
      if (!final_gp)
        {
          /* Determine the final values for "_GLOBAL_OFFSET_TABLE_" and the
             defined SDA symbols; also, if a map file is to be generated,
	     show the addresses assigned to bit objects.  */
          final_gp = true;
          if (!tricore_elf32_final_gp (output_bfd, info))
            return false;

          tricore_elf32_final_sda_bases (output_bfd, info);
          if (tricore_elf32_map_file)
	    {
	      tricore_elf32_list_bit_objects (info);
	      if (tricore_elf32_extmap_enabled)
	        tricore_elf32_do_extmap (info);
	    }
        }

      /* Initialize various variables needed for a final link.  */
      sgot = splt = sreloc = NULL;
      if (elf_hash_table (info)->hgot == NULL)
        got_base = got_ptr = 0;
      else
        {
          got_base = elf_hash_table (info)->hgot->root.u.def.value;
          got_ptr = (got_base
                     + (elf_hash_table (info)
      		        ->hgot->root.u.def.section->output_section->vma)
		     + (elf_hash_table (info)
		        ->hgot->root.u.def.section->output_offset));
        }

      /* See if we need to perform PCP address mappings.  */
      if ((tricore_elf32_pcpmap >= 0)
          && ((input_section->flags & PCP_SEG)
	      || !strcmp (input_section->name, ".pcp_c_ptr_init")
	      || !strncmp (input_section->name, ".pcp_c_ptr_init.", 16)))
	do_pcpmap = true;
    }


  /* Walk through all relocs of this input section.  */
  relend = relocs + input_section->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    {
      r_type = ELF32_R_TYPE (rel->r_info);
      r_symndx = ELF32_R_SYM (rel->r_info);

      if ((r_type < 0)
          || (r_type < R_TRICORE_NONE)
	  || (r_type >= R_TRICORE_max))
	{
	  (*_bfd_error_handler) (_("%s: unknown relocation type %d"),
				 bfd_get_filename (input_bfd), r_type);
	  bfd_set_error (bfd_error_bad_value);
	  ret = false;
	  bitpos_seen = false;
	  continue;
	}
      else if (r_type == R_TRICORE_NONE)
        {
	  /* This relocation type is typically caused by the TriCore-specific
	     relaxation pass, either because it has changed R_TRICORE_BITPOS
	     relocations against local bit variables to refer to the actual
	     symbol representing their respective bit offset, or because it
	     was found that a call instruction requires a trampoline to reach
	     its target (in which case the call instruction is modified to
	     directly branch to the trampoline, so the original relocation
	     against the target needs to be nullified, while the instructions
	     within the trampoline will add their own relocation entries).  */
	  bitpos_seen = false;
          continue;
	}

      if (info->relocateable)
	{
	  /* This is a relocateable link.  We don't have to change
	     anything, unless the reloc is against a local section
	     symbol, in which case we have to adjust according to
	     where the section symbol winds up in the output section.  */
	  if (r_symndx < symtab_hdr->sh_info)
	    {
	      sym = local_syms + r_symndx;
	      if (ELF_ST_TYPE (sym->st_info) == STT_SECTION)
	        {
		  sec = local_sections[r_symndx];
	          rel->r_addend += sym->st_value + sec->output_offset;
	          /* Addends are stored with relocs.  We're done.  */
		}
	    }

	  continue;
	}

      /* This is a final link.  All kinds of strange things may happen...  */

      if ((r_type == R_TRICORE_GNU_VTENTRY)
	  || (r_type == R_TRICORE_GNU_VTINHERIT))
	{
	  /* Handled by GC (if enabled via "--gc-sections", that is).  */
          continue;
	}

      if (r_type == R_TRICORE_BITPOS)
        {
	  /* The next relocation will be against a bit object; remember that
	     its bit position should be used rather than its address.  */
	  bitpos_seen = true;
	  continue;
	}

      howto = tricore_elf32_howto_table + r_type;
      addend = rel->r_addend;
      offset = rel->r_offset;
      unresolved_reloc = false;
      r = bfd_reloc_ok;
      h = NULL;
      sym = NULL;
      sec = NULL;
      insn = rel_abs = 0;
      errmsg = NULL;

      if (r_symndx < symtab_hdr->sh_info)
	{
	  /* Relocation against a local symbol.  */
	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];
	  sym_name = "<local symbol>";
	  will_become_local = true;
	  relocation = _bfd_elf_rela_local_sym (output_bfd, sym, sec, rel);
	}
      else
	{
	  /* Relocation against an external symbol.  */
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while ((h->root.type == bfd_link_hash_indirect)
		 || (h->root.type == bfd_link_hash_warning))
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	  sym_name = h->root.root.string;

	  /* Can this relocation be resolved immediately?  */
	  will_become_local = SYMBOL_REFERENCES_LOCAL (info, h);

	  if ((h->root.type == bfd_link_hash_defined)
	      || (h->root.type == bfd_link_hash_defweak))
	    {
	      sec = h->root.u.def.section;
	      if (sec->output_section == NULL)
		{
		  /* Set a flag that will be cleared later if we find a
		     relocation value for this symbol.  output_section
		     is typically NULL for symbols satisfied by a shared
		     library.  */
		  relocation = 0;
		  unresolved_reloc = true;
		}  
	      else if (howto->pc_relative && (sec == bfd_abs_section_ptr))
		{
		  rel_abs = 1;
		  relocation = (h->root.u.def.value
			        + sec->output_section->vma);
		}
	      else
		relocation = (h->root.u.def.value
			      + sec->output_section->vma
			      + sec->output_offset);
	    }
	  else if (h->root.type == bfd_link_hash_undefweak)
	    relocation = 0;
          else if (info->shared
                   && (!info->symbolic || info->allow_shlib_undefined)
                   && !info->no_undefined
                   && ELF_ST_VISIBILITY (h->other) == STV_DEFAULT)
	    relocation = 0;
	  else
	    {
              if (!((*info->callbacks->undefined_symbol)
                    (info, h->root.root.string, input_bfd,
                     input_section, rel->r_offset,
                     (!info->shared || info->no_undefined
                      || ELF_ST_VISIBILITY (h->other)))))
                return false;

	      ret = false;
	      bitpos_seen = false;
	      continue;
	    }
        }

      /* The linker's optimization passes may have deleted entries in
         the ".eh_frame" (ELF only) and ".stab" sections; as a result,
	 we have to skip relocations that are no longer valid, and to
	 compute and use the (potentially) new offsets of entries that
	 couldn't be eliminated during said optimization passes.  */
      {
	bfd_vma n_offset;

	n_offset = _bfd_elf_section_offset (output_bfd, info,
	      				    input_section, offset);
	if ((n_offset == (bfd_vma) -1)  /* eh_frame entry deleted.  */
	    || (n_offset == (bfd_vma) -2))  /* stab entry deleted.  */
	  continue;

	offset = n_offset;
      }

      /* Sanity check the address.  Note that if the relaxation pass has
         been enabled, additional instructions and relocs for these may
	 have been created, so we must compare the offset against the
	 section's cooked size.  This is okay even if relaxation hasn't
	 been enabled, or if it hasn't increased the size, because in the
	 former case, the linker will have set the cooked size to the raw
	 size in lang_size_sections_sections_1, while in the latter case
	 this is done by tricore_elf32_relax_section itself.  Likewise, if
	 relaxing has decreased the section size, the cooked size will be
	 smaller than the raw size, so if there are any relocations against
	 locations beyond the cooked size, this indicates an error.  */
      if (offset > input_section->_cooked_size)
	{
	  r = bfd_reloc_outofrange;
	  goto check_reloc;
	}

      /* If PC-relative, adjust the relocation value appropriately.  */
      if (howto->pc_relative && !rel_abs)
        {
          relocation -= (input_section->output_section->vma
                         + input_section->output_offset);
          if (howto->pcrel_offset)
            relocation -= offset;
        }

      /* Add the r_addend field.  */
      relocation += addend;

      /* Special case: the previous relocation was R_TRICORE_BITPOS,
         which means that the relocation value is not the symbol's
	 address, but its bit position.  */
      if (bitpos_seen)
        {
	  relocation = tricore_elf32_get_bitpos (input_bfd, info, rel,
	  					 symtab_hdr,
						 local_syms, sym_hashes,
						 input_section, &ret);
	  bitpos_seen = false;
	}
      else if (do_pcpmap && sec
      	       && ((sec->flags & SEC_DATA) || !(sec->flags & SEC_LOAD)))
	{
          /* Perform PCP address mapping based on the relocation value
	     and the specified mapping scheme (there's currently only
	     one of them, so we don't need to check the actual value of
	     tricore_elf32_pcpmap).  Note that we don't care about the
	     symbol or section the relocation value was derived from;
	     this is because even knowing the type of a symbol or a
	     section won't help in deciding automatically if a relocation
	     value should be mapped or not (e.g., if we would exempt
	     absolute symbols from being mapped, this would fail if the
	     symbol would hold the absolute address of a variable that
	     is located within a mappable memory region).  This sort of
	     hardware design really, really sucks big time!  */
          if ((relocation >= 0xc0000000) && (relocation < 0xd4100000))
	    {
	      bfd_vma poffset = 0;

	      if (relocation < 0xc0400000)
	        poffset = 0x28000000;
	      else if (relocation >= 0xd0000000)
	        {
		  if (relocation < 0xd0100000)
		    poffset = 0x18400000;
		  else if (relocation >= 0xd4000000)
		    poffset = 0x14500000;
		}

	      if (poffset)
	        {
		  if (tricore_elf32_debug_pcpmap)
		    {
		      printf ("PCPMAP: %s(%s+%ld): 0x%08lx [",
			      bfd_archive_filename (input_section->owner),
			      input_section->name,
			      offset,
			      relocation);
		      if (h)
			printf ("%s", sym_name);
		      else
			printf ("%s", sec->name);
		      if (addend)
			printf ("+%ld", addend);
		      printf ("] += 0x%08lx (= 0x%08lx)\n",
		      	      poffset, relocation + poffset);
		    }
		  relocation += poffset;
		}
	    }
	}

      /* Now apply the fixup.  Handle simple data relocs first.  */
      byte_ptr = (bfd_byte *) contents + offset;

      switch (r_type)
        {
	case R_TRICORE_8ABS:
	case R_TRICORE_PCREL8:
	  bfd_put_8 (input_bfd, relocation, byte_ptr);
	  continue;

	case R_TRICORE_16ABS:
	case R_TRICORE_PCREL16:
	  bfd_put_16 (input_bfd, relocation, byte_ptr);
	  continue;

	case R_TRICORE_32ABS:
	case R_TRICORE_32REL:
	  if (info->shared
	      && (r_symndx != 0)
	      && (input_section->flags & SEC_ALLOC)
	      && ((r_type != R_TRICORE_32REL)
	          || ((h != NULL)
		      && (h->dynindx != -1)
		      && (!info->symbolic
		          || !(h->elf_link_hash_flags
			       & ELF_LINK_HASH_DEF_REGULAR)))))
	    {
	      Elf_Internal_Rela outrel;
	      boolean skip = false, relocate = false;

	      if (sreloc == NULL)
	        {
		  const char *name;

		  name = (bfd_elf_string_from_elf_section
		  	  (input_bfd,
			   elf_elfheader (input_bfd)->e_shstrndx,
			   elf_section_data (input_section)->rel_hdr.sh_name));
		  if (name == NULL)
		    return false;

		  BFD_ASSERT (!strncmp (name, ".rela", 5)
		  	      && !strcmp (name + 5, bfd_get_section_name
			      			    (input_bfd,
					  	     input_section)));
		  sreloc = bfd_get_section_by_name (dynobj, name);
		  BFD_ASSERT (sreloc != NULL);
		}

	      outrel.r_offset =
	        _bfd_elf_section_offset (output_bfd, info, input_section,
					 rel->r_offset);
	      if (outrel.r_offset == (bfd_vma) -1)
	        skip = true;  /* eh_frame entry deleted.  */
	      else if (outrel.r_offset == (bfd_vma) -2)
	        skip = relocate = true;  /* stab entry deleted.  */
	      outrel.r_offset += (input_section->output_section->vma
	      			  + input_section->output_offset);
	      outrel.r_addend = rel->r_addend;

	      if (skip)
	        memset (&outrel, 0, sizeof (outrel));
	      else if (r_type == R_TRICORE_32REL)
	        {
		  BFD_ASSERT ((h != NULL) && (h->dynindx != -1));
		  outrel.r_info = ELF32_R_INFO (h->dynindx, R_TRICORE_32REL);
		}
	      else
	        {
		  /* h->dynindx may be -1 if this symbol was marked to
		     become local.  */
		  if ((h == NULL)
		      || ((info->symbolic || (h->dynindx == -1))
		          && (h->elf_link_hash_flags
			      & ELF_LINK_HASH_DEF_REGULAR)))
		    {
		      relocate = true;
		      outrel.r_info = ELF32_R_INFO (0, R_TRICORE_RELATIVE);
		    }
		  else
		    {
		      BFD_ASSERT (h->dynindx != -1);
		      outrel.r_info = ELF32_R_INFO
		      		       (h->dynindx, R_TRICORE_32ABS);
		    }
		}
	      bfd_elf32_swap_reloca_out (output_bfd, &outrel,
	      				 (((Elf32_External_Rela *)
					   sreloc->contents)
					  + sreloc->reloc_count));
	      ++sreloc->reloc_count;
	      
	      /* If this reloc is against an external symbol, we do
	         not want to fiddle with the addend.  Otherwise, we
		 need to include the symbol value so that it becomes
		 an addend for the dynamic reloc.  */
	      if (!relocate)
	        continue;
	    }
	  bfd_put_32 (input_bfd, relocation, byte_ptr);
	  continue;
	}

      /* It's a relocation against an instruction.  */

      /* Handle PCP relocs.  */
      if ((r_type >= R_TRICORE_PCPHI) && (r_type <= R_TRICORE_PCPTEXT))
	{
          switch (r_type)
            {
	    case R_TRICORE_PCPHI:
	      len32 = 0;
	      insn = ((relocation >> 16) & 0xffff);
	      goto check_reloc;

	    case R_TRICORE_PCPLO:
	      len32 = 0;
	      insn = (relocation & 0xffff);
	      goto check_reloc;

	    case R_TRICORE_PCPPAGE:
	      len32 = 0;
	      /* Sanity check: the target address of this reloc must
	         belong to a PCP data section.  */
	      if (sec && (!(sec->flags & PCP_SEG) || !(sec->flags & SEC_DATA)))
	        {
		  errmsg = _("PRAM target address not within a PCP "
		  	     "data section");
		  goto check_reloc;
		}
	      /* Ideally, the target address should be aligned to a
	         256-byte-boundary, so that PCP code can access the
		 64 words starting at the target address using the 6-bit
		 unsigned offset in "*.PI" instructions.  However, I
		 think we should allow a user to organize its data as
		 he sees fit, so we're just enforcing the minimum
		 requirement (target address must be word-aligned),
		 but issue a warning if the target address isn't
		 properly aligned, as this can potentially lead to
		 incorrect PRAM accesses for non-zero offsets; the user
		 may turn off this warning by defining the global symbol
		 "NOPCPWARNING" with a non-zero value.  */
	      if (relocation & 0x3)
	        {
		  errmsg = _("PRAM target address is not word-aligned");
		  goto check_reloc;
		}
	      else if (relocation & 0xff)
	        {
	          struct bfd_link_hash_entry *w;

                  w = bfd_link_hash_lookup (info->hash, "NOPCPWARNING",
	  			            false, false, false);
                  if ((w == (struct bfd_link_hash_entry *) NULL)
	              || ((w->type == bfd_link_hash_defined)
	                  && (w->u.def.value == 0)))
            	    {
		      const char *name, *msg;

		      if (h != NULL)
		        name = h->root.root.string;
		      else
		        {
		          name = (bfd_elf_string_from_elf_section
			          (input_bfd, symtab_hdr->sh_link,
				  sym->st_name));
		          if (name == NULL || *name == '\0')
			    name = bfd_section_name (input_bfd, sec);
		        }
		      msg = _("PRAM target address should be aligned "
		              "to a 256-byte boundary");
	              (void) ((*info->callbacks->warning)
		              (info, msg, name, input_bfd, input_section,
			      offset));
		    }
		}
              insn = bfd_get_16 (input_bfd, byte_ptr);
              insn |= (relocation & 0xff00);
	      goto check_reloc;

	    case R_TRICORE_PCPOFF:
	      len32 = 0;
	      /* Sanity check: the target address of this reloc must
	         belong to a PCP data section.  */
	      if (sec && (!(sec->flags & PCP_SEG) || !(sec->flags & SEC_DATA)))
	        {
		  errmsg = _("PRAM target address not within a PCP "
		  	     "data section");
		  goto check_reloc;
		}
	      if (relocation & 0x3)
	        {
		  errmsg = _("PRAM target address is not word-aligned");
		  goto check_reloc;
		}
              insn = bfd_get_16 (input_bfd, byte_ptr);
	      insn |= ((relocation >> 2) & 0x3f);	
	      goto check_reloc;

	    case R_TRICORE_PCPTEXT:
	      len32 = 0;
	      /* Sanity check: the target address of this reloc must
	         belong to a PCP text section.  */
	      if (sec && (!(sec->flags & PCP_SEG) || !(sec->flags & SEC_CODE)))
	        {
		  errmsg = _("PCODE target address not within a PCP "
		  	     "text section");
		  goto check_reloc;
		}
	      if (relocation & 0x1)
	        {
		  errmsg = _("PCODE target address is not even");
		  goto check_reloc;
		}
	      insn = ((relocation >> 1) & 0xffff);	
	      goto check_reloc;

	    default:
	      break;
	    }
	}

      /* Handle TriCore relocs.  */
      len32 = (*byte_ptr & 1);
      if (len32)
        insn = bfd_get_32 (input_bfd, byte_ptr);
      else
	insn = bfd_get_16 (input_bfd, byte_ptr);

      /* Prepare PIC relocs.  */
      switch (r_type)
        {
	case R_TRICORE_GOTOFF:
	case R_TRICORE_GOTOFF2:
	case R_TRICORE_GOTOFFHI:
	case R_TRICORE_GOTOFFLO:
	case R_TRICORE_GOTOFFLO2:
	case R_TRICORE_GOTOFFUP:
	  /* The relocation value is the offset between the
	     symbol and the GOT pointer.  */
	  relocation = relocation - got_ptr;
	  break;

	case R_TRICORE_GOTPC:
	case R_TRICORE_GOTPC2:
	case R_TRICORE_GOTPCHI:
	case R_TRICORE_GOTPCLO:
	case R_TRICORE_GOTPCLO2:
	case R_TRICORE_GOTPCUP:
	  /* The relocation value is the offset between the
	     GOT pointer and the current PC.  */
	  relocation = (relocation
	  		- (got_base
			   + input_section->output_section->vma
                           + input_section->output_offset
			   + offset));
	  break;

	case R_TRICORE_GOT:
	case R_TRICORE_GOT2:
	case R_TRICORE_GOTHI:
	case R_TRICORE_GOTLO:
	case R_TRICORE_GOTLO2:
	case R_TRICORE_GOTUP:
	  /* The relocation is to the GOT entry for this symbol.  */
	  if (sgot == NULL)
	    {
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      BFD_ASSERT (sgot != NULL);
	    }

	  if (h != NULL)
	    {
	      bfd_vma off;

	      off = h->got.offset;
	      BFD_ASSERT (off != (bfd_vma) -1);
	      /* The first entry of the GOT is reserved (-> _DYNAMIC),
	         so offsets of "real" entries start at 4.  However,
		 if tricore_elf32_final_gp has moved the GOT base to
		 a higher address, we must adjust the offsets of entries
		 located below the new GOT base by 4 bytes.  Should we
		 ever need to reserve more space for special entries,
		 we must use elf_backend_got_header_size (defined at
		 the end of this file) instead of the value 4.  */
	      if (off <= got_base)
	        off -= 4;

	      if (!elf_hash_table (info)->dynamic_sections_created
	          || (info->shared && SYMBOL_REFERENCES_LOCAL (info, h)))
		{
		  /* This is actually a static link, or it is a "-Bsymbolic"
		     link and the symbol is defined locally, or the symbol
		     was forced to be local because of a version file.  We
		     must initialize this GOT entry, and since the offset
		     must always be a multiple of 4, we use the least
		     significant bit to record whether we have initialized
		     it already.  When doing a dynamic link, we create a
		     ".rela.got" relocation entry to initialize the value.
		     This is done in tricore_elf32_finish_dynamic_symbol.  */
		  if ((off & 1) != 0)   
		    off &= ~1;
		  else
		    {
		      bfd_put_32 (output_bfd, relocation,
		      		  sgot->contents + off);
		      h->got.offset |= 1;
		    }  
		}
	      else
	        unresolved_reloc = false;

	      relocation = sgot->output_offset + off - got_base;
	    }
	  else
	    {
	      bfd_vma off;

	      BFD_ASSERT ((local_got_offsets != NULL)
	      		  && (local_got_offsets[r_symndx] != (bfd_vma) -1));
	      off = local_got_offsets[r_symndx];
	      if (off <= got_base)
	        off -= 4;
	      /* The offset must always be a multiple of 4.  We use
	         the least significant bit to record whether we have
		 already processed this entry.  */
	      if ((off & 1) != 0)
	        off &= ~1;
	      else
	        {
		  bfd_put_32 (output_bfd, relocation, sgot->contents + off);

		  if (info->shared)
		    {
		      asection *srelgot;
		      Elf_Internal_Rela outrel;

		      /* We need to generate a R_TRICORE_RELATIVE reloc
		         for the dynamic linker.  */
		      srelgot = bfd_get_section_by_name (dynobj, ".rela.got");
		      BFD_ASSERT (srelgot != NULL);
		      outrel.r_offset = (sgot->output_section->vma
		      			 + sgot->output_offset + off);
		      outrel.r_info = ELF32_R_INFO (0, R_TRICORE_RELATIVE);
		      outrel.r_addend = 0;
		      bfd_elf32_swap_reloca_out (output_bfd, &outrel,
		      				 (((Elf32_External_Rela *)
						   srelgot->contents)
						  + srelgot->reloc_count));
		      ++srelgot->reloc_count;
		    }

		  local_got_offsets[r_symndx] |= 1;
		}
	      relocation = sgot->output_offset + off - got_base;
	    }
	  break;  

	case R_TRICORE_PLT:
	case R_TRICORE_24REL:
	  if (!info->shared || !h)
	    break;

	  /* The relocation is to the PLT entry for this symbol.  */
          if (splt == NULL)
	    splt = bfd_get_section_by_name (dynobj, ".plt");
	  if ((h->plt.offset == (bfd_vma) -1) || (splt == NULL))
	    {
	      /* We didn't make a PLT entry for this symbol.  This
	         happens when statically linking PIC code, or when
		 using "-Bsymbolic".  */
	      break;
	    }
	  relocation = (splt->output_section->vma
	  		+ splt->output_offset
			+ h->plt.offset);
          relocation -= (input_section->output_section->vma
                         + input_section->output_offset);
          if (howto->pcrel_offset)
            relocation -= offset;
	  break;

	default:
	  break;
	}

      switch (r_type)
        {
	case R_TRICORE_GOT:
	case R_TRICORE_GOTOFF:
	case R_TRICORE_GOTPC:
	  if (((int) relocation < -32768) || ((int) relocation > 32767))
	    {
	      errmsg = _("16-bit signed GOT-relative offset overflow");
	      break;
	    }
	  insn |= ((relocation & 0xffff) << 12);
	  break;

	case R_TRICORE_GOT2:
	case R_TRICORE_GOTOFF2:
	case R_TRICORE_GOTPC2:
	  if (((int) relocation < -32768) || ((int) relocation > 32767))
	    {
	      errmsg = _("16-bit signed GOT-relative offset overflow");
	      break;
	    }
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 22);
	  insn |= ((relocation & 0xfc00) << 12);
	  break;

	case R_TRICORE_GOTHI:  
	case R_TRICORE_GOTOFFHI:  
	case R_TRICORE_GOTPCHI:  
	  insn |= ((((relocation + 0x8000) >> 16) & 0xffff) << 12);
	  break;

	case R_TRICORE_GOTLO:
	case R_TRICORE_GOTOFFLO:
	case R_TRICORE_GOTPCLO:
	  insn |= ((relocation & 0xffff) << 12);
	  break;

	case R_TRICORE_GOTLO2:
	case R_TRICORE_GOTOFFLO2:
	case R_TRICORE_GOTPCLO2:
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 22);
	  insn |= ((relocation & 0xfc00) << 12);
	  break;

	case R_TRICORE_GOTUP:
	case R_TRICORE_GOTOFFUP:
	case R_TRICORE_GOTPCUP:
	  insn |= (((relocation >> 16) & 0xffff) << 12);
	  break;

	case R_TRICORE_PLT:
	case R_TRICORE_24REL:
	  if (relocation & 1)
	    {
	      errmsg = _("24-bit PC-relative displacement is not even");
	      break;
	    }
	  else if (((int) (relocation) < (-16777216)) ||
		   ((int) (relocation) > (16777214)))
	    {
	      /* The target address is out of range; check if it
	         is reachable using absolute addressing mode.  */
	      bfd_vma target_address = relocation;

	      if (!rel_abs)
	        {
		  /* The target symbol isn't in the absolute section,
		     so we need to undo the relative offset.  */
	          target_address += input_section->output_section->vma
	      		          + input_section->output_offset;
                  if (howto->pcrel_offset)
                    target_address += offset;
		}

	      if (!(target_address & 0x0fe00000))
	        {
		  /* Okay, it's reachable using absolute addressing mode;
		     turn [f]call into [f]calla, or j[l] into j[l]a.  */
		  insn |= 0x80;
		  target_address >>= 1;
		  target_address |= ((target_address & 0x78000000) >> 7);
		  insn |= ((target_address & 0xffff) << 16);
		  insn |= ((target_address & 0xff0000) >> 8);
		}
	      else
	        {
		  /* We need additional instructions to reach the target.
		     If the target is a global symbol, then enabling the
		     linker's relaxing pass will do the trick.  Local
		     symbols, however, require the user to take action.  */
		  if (tricore_elf32_relax_bdata)
		    {
		      errmsg = _("24-bit PC-relative displacement overflow: "
		      		 "if the target of this call or jump insn "
				 "is a static function or procedure, just "
				 "turn it into a global one; otherwise try "
				 "to move excessive code from the affected "
				 "control statement's (e.g., if/else/switch) "
				 "body to separate new functions/procedures");
		    }
		  else
		    {
		      errmsg = _("24-bit PC-relative displacement overflow; "
		  	         "re-run linker with -relax or --relax-24rel");
		    }
		}

	      break;
	    }
	  else
	    {
	      /* Target address w/in +/- 16 MB.  */
	      relocation >>= 1;
	      insn |= ((relocation & 0xffff) << 16);
	      insn |= ((relocation & 0xff0000) >> 8);
	    }
	  break;

	case R_TRICORE_24ABS:
	  if (relocation & 0x0fe00001)
	    {
	      errmsg = _("Illegal 24-bit absolute address");
	      break;
	    }
          relocation >>= 1;
          relocation |= ((relocation & 0x78000000) >> 7);
          insn |= ((relocation & 0xffff) << 16);
          insn |= ((relocation & 0xff0000) >> 8);
          break;
	  
	case R_TRICORE_18ABS:
	  if (relocation & 0x0fffc000)
	    {
	      errmsg = _("Illegal 18-bit absolute address");
	      break;
	    }
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 22);
	  insn |= ((relocation & 0x3c00) << 12);
	  insn |= ((relocation & 0xf0000000) >> 16);
	  break;

	case R_TRICORE_HI:
	  insn |= (((relocation >> 16) & 0xffff) << 12);
	  break;

	case R_TRICORE_HIADJ:
	  insn |= ((((relocation + 0x8000) >> 16) & 0xffff) << 12);
	  break;

	case R_TRICORE_LO:
	  insn |= ((relocation & 0xffff) << 12);
	  break;

	case R_TRICORE_LO2:
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 22);
	  insn |= ((relocation & 0xfc00) << 12);
	  break;

	case R_TRICORE_16SM:
	case R_TRICORE_10SM:
	case R_TRICORE_16SM2:
	  BFD_ASSERT (sec);
	  sec_name = sec->name;
	  if (strcmp (sec_name, ".sdata")
	      && strcmp (sec_name, ".sbss")
	      && strncmp (sec_name, ".sdata.", 7)
	      && strncmp (sec_name, ".sbss.", 6))
	    {
	      errmsg = _("Small data relocation against an object not in a "
	      	         "named small data section (i.e., .s{data,bss}{,.*}");
	      break;
	    }
	  r = tricore_elf32_final_sda_base (sec->output_section,
	  				    &sda_base, &sda_reg);
	  if (r != bfd_reloc_ok)
	    {
	      errmsg = _("Undefined or illegal small data output section");
	      break;
	    }
	  relocation -= sda_base;
	  if ((r_type == R_TRICORE_16SM) || (r_type == R_TRICORE_16SM2))
	    {
	      if (((int) relocation < -32768) || ((int) relocation > 32767))
	      {
	        errmsg = _("16-bit signed SDA-relative offset overflow");
	        break;
	      }
	      if (r_type == R_TRICORE_16SM)
	        insn |= ((relocation & 0xfc00) << 12);
	      else
	        {
		  insn |= ((relocation & 0xffff) << 12);
		  break;
		}
	    }
	  else
	    {
	      if (((int) relocation < -512) || ((int) relocation > 511))
	      {
	        errmsg = _("10-bit signed SDA-relative offset overflow");
	        break;
	      }
	    }
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 22);
	  /* Insert the address register number for the given SDA,
	     except for 16SM2 (which only requests the SDA offset as a
	     constant and won't access a variable directly).  */
	  if ((sda_reg != 0) && (r_type != R_TRICORE_16SM2))
	    insn = (insn & 0xffff0fff) | (sda_reg << 12);
	  break;

	case R_TRICORE_16CONST:
	  if (((int) relocation < -32768) || ((int) relocation > 32767))
	    {
	      errmsg = _("16-bit signed value overflow");
	      break;
	    }
	  insn |= ((relocation & 0xffff) << 12);
	  break;

	case R_TRICORE_15REL:
	  CHECK_DISPLACEMENT (relocation, -32768, 32766);
	  insn |= (((relocation >> 1) & 0x7fff) << 16);
	  break;

	case R_TRICORE_9SCONST:
	  if (((int) relocation < -256) || ((int) relocation > 255))
	    {
	      errmsg = _("9-bit signed value overflow");
	      break;
	    }
	  insn |= ((relocation & 0x1ff) << 12);
	  break;

	case R_TRICORE_9ZCONST:
	  if (relocation & ~511)
	    {
	      errmsg = _("9-bit unsigned value overflow");
	      break;
	    }
	  insn |= (relocation << 12);
	  break;

	case R_TRICORE_8REL:
	  CHECK_DISPLACEMENT (relocation, -256, 254);
	  relocation >>= 1;
	  insn |= ((relocation & 0xff) << 8);
	  break;

	case R_TRICORE_8CONST:
	  if (relocation & ~255)
	    {
	      errmsg = _("8-bit unsigned value overflow");
	      break;
	    }
	  insn |= (relocation << 8);
	  break;

	case R_TRICORE_10OFF:
	  if (((int) relocation < -512) || ((int) relocation > 511))
	    {
	      errmsg = _("10-bit signed offset overflow");
	      break;
	    }
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 12);
	  break;

	case R_TRICORE_16OFF:
	  if (((int) relocation < -32768) || ((int) relocation > 32767))
	    {
	      errmsg = _("16-bit signed offset overflow");
	      break;
	    }
	  insn |= ((relocation & 0x3f) << 16);
	  insn |= ((relocation & 0x3c0) << 22);
	  insn |= ((relocation & 0xfc00) << 12);
	  break;

	case R_TRICORE_1BIT:
	  if (relocation & ~1)
	    {
	      errmsg = _("Invalid bit value");
	      break;
	    }
	  insn |= (relocation << 11);
	  break;

	case R_TRICORE_3POS:
	  if (relocation & ~7)
	    {
	      errmsg = _("Invalid 3-bit bit position");
	      break;
	    }
	  insn |= (relocation << 8);
	  break;

	case R_TRICORE_5POS:
	  if (relocation & ~31)
	    {
	      errmsg = _("Invalid 5-bit bit position");
	      break;
	    }
	  insn |= (relocation << 16);
	  break;

	case R_TRICORE_5POS2:
	  if (relocation & ~31)
	    {
	      errmsg = _("Invalid 5-bit bit position");
	      break;
	    }
	  insn |= (relocation << 23);
	  break;

	case R_TRICORE_BRCC:
	  if (((int) relocation < -8) || ((int) relocation > 7))
	    {
	      errmsg = _("4-bit signed value overflow");
	      break;
	    }
	  insn |= ((relocation & 0xf) << 12);
	  break;

	case R_TRICORE_BRCZ:
	  if (relocation & ~15)
	    {
	      errmsg = _("4-bit unsigned value overflow");
	      break;
	    }
	  insn |= (relocation << 12);
	  break;

	case R_TRICORE_BRNN:
	  if (relocation & ~31)
	    {
	      errmsg = _("Invalid 5-bit bit position");
	      break;
	    }
	  insn |= ((relocation & 0xf) << 12);
	  insn |= ((relocation & 0x10) << 3);
	  break;

	case R_TRICORE_RRN:
	  if (relocation & ~3)
	    {
	      errmsg = _("2-bit unsigned value overflow");
	      break;
	    }
	  insn |= (relocation << 16);
	  break;

	case R_TRICORE_4CONST:
	  if (((int) relocation < -8) || ((int) relocation > 7))
	    {
	      errmsg = _("4-bit signed value overflow");
	      break;
	    }
	  insn |= ((relocation & 0xf) << 12);
	  break;

	case R_TRICORE_4REL:
	  CHECK_DISPLACEMENT (relocation, 0, 30);
	  relocation >>= 1;
	  insn |= ((relocation & 0xf) << 8);
	  break;

	case R_TRICORE_4REL2:
	  CHECK_DISPLACEMENT (relocation, -32, -2);
	  relocation >>= 1;
	  insn |= ((relocation & 0xf) << 8);
	  break;

	case R_TRICORE_5REL:
	  CHECK_DISPLACEMENT (relocation, 0, 62);
	  relocation >>= 1;
	  insn &= ~0x0080;
	  insn |= ((relocation & 0xf) << 8);
	  insn |= ((relocation & 0x10) << 3);
	  break;

	case R_TRICORE_5POS3:
	  if (relocation & ~31)
	    {
	      errmsg = _("Invalid 5-bit bit position");
	      break;
	    }
	  insn |= ((relocation & 0xf) << 12);
	  insn |= ((relocation & 0x10) << 3);
	  break;

	case R_TRICORE_4OFF:
	  if (relocation & ~15)
	    {
	      errmsg = _("4-bit unsigned offset overflow");
	      break;
	    }
	  insn |= (relocation << 12);
	  break;

	case R_TRICORE_4OFF2:
	  if (relocation & ~31)
	    {
	      errmsg = _("5-bit unsigned offset overflow");
	      break;
	    }
	  else if (relocation & 1)
	    {
	      errmsg = _("5-bit unsigned offset is not even");
	      break;
	    }
	  insn |= (relocation << 11);
	  break;

	case R_TRICORE_4OFF4:
	  if (relocation & ~63)
	    {
	      errmsg = _("6-bit unsigned offset overflow");
	      break;
	    }
	  else if (relocation & 3)
	    {
	      errmsg = _("6-bit unsigned offset is not a multiple of 4");
	      break;
	    }
	  insn |= (relocation << 10);
	  break;

	case R_TRICORE_42OFF:
	  if (relocation & ~15)
	    {
	      errmsg = _("4-bit unsigned offset overflow");
	      break;
	    }
	  insn |= (relocation << 8);
	  break;

	case R_TRICORE_42OFF2:
	  if (relocation & ~31)
	    {
	      errmsg = _("5-bit unsigned offset overflow");
	      break;
	    }
	  else if (relocation & 1)
	    {
	      errmsg = _("5-bit unsigned offset is not even");
	      break;
	    }
	  insn |= (relocation << 7);
	  break;

	case R_TRICORE_42OFF4:
	  if (relocation & ~63)
	    {
	      errmsg = _("6-bit unsigned offset overflow");
	      break;
	    }
	  else if (relocation & 3)
	    {
	      errmsg = _("6-bit unsigned offset is not a multiple of 4");
	      break;
	    }
	  insn |= (relocation << 6);
	  break;

	case R_TRICORE_2OFF:
	  if (relocation & 3)
	    {
	      errmsg = _("2-bit unsigned value overflow");
	      break;
	    }
	  insn |= (relocation << 6);
	  break;

	case R_TRICORE_8CONST2:
	  if (relocation & ~1023)
	    {
	      errmsg = _("10-bit unsigned offset overflow");
	      break;
	    }
	  else if (relocation & 3)
	    {
	      errmsg = _("10-bit unsigned offset is not a multiple of 4");
	      break;
	    }
	  insn |= (relocation << 6);
	  break;

	case R_TRICORE_4POS:
	  if (relocation & ~15)
	    {
	      errmsg = _("Invalid 4-bit bit position");
	      break;
	    }
	  insn |= ((relocation & 0xf) << 12);
	  break;

	default:
	  errmsg = _("Internal error: unimplemented relocation type");
	  break;
	}


check_reloc:
      if ((r == bfd_reloc_ok) && (errmsg == NULL))
        {
	  /* No error occurred; just write back the relocated insn.  */
	  if (len32)
  	    bfd_put_32 (input_bfd, insn, byte_ptr);
	  else
  	    bfd_put_16 (input_bfd, insn, byte_ptr);
	}
      else
	{
	  /* Some error occured.  Gripe.  */
	  const char *name;

	  if (h != NULL)
	    name = h->root.root.string;
	  else
	    {
	      name = (bfd_elf_string_from_elf_section
		      (input_bfd, symtab_hdr->sh_link, sym->st_name));
	      if (name == NULL || *name == '\0')
		name = bfd_section_name (input_bfd, sec);
	    }

	  if (errmsg != NULL)
	    {
	      ret = false;
	      (*_bfd_error_handler)
	       ("%s; symbol name = %s (defined in section %s of file %s), "
	        "addend = %ld, input file = %s, input section = %s, "
		"relocation offset = 0x%08lx (VMA = 0x%08lx), "
		"relocation value = 0x%08lx (%ld), output section = %s",
	        errmsg, name, sec->name,
	        sec->owner ? bfd_archive_filename (sec->owner) : "<unknown>",
		addend,
	        bfd_archive_filename (input_bfd),
	        bfd_get_section_name (input_bfd, input_section),
	        offset, offset + input_section->output_section->vma
	              	       + input_section->output_offset,
	        relocation, relocation,
	        input_section->output_section->name);
	    }
	  else
	    {
	      switch (r)
	        {
	        case bfd_reloc_overflow:
	          if (!((*info->callbacks->reloc_overflow)
		        (info, name, howto->name, (bfd_vma) 0,
		        input_bfd, input_section, offset)))
		    return false;
		  ret = false;
	          break;

	        case bfd_reloc_undefined:
	          if (!((*info->callbacks->undefined_symbol)
		        (info, name, input_bfd, input_section, offset, true)))
		    return false;
	          break;

	        case bfd_reloc_outofrange:
	          errmsg = _("Out of range error");
	          goto common_error;

	        case bfd_reloc_notsupported:
	          errmsg = _("Unsupported relocation error");
	          goto common_error;

	        case bfd_reloc_dangerous:
	          errmsg = _("Dangerous error");
	          goto common_error;

	        default:
	          errmsg = _("Internal error: unknown error");
common_error:
		  ret = false;
	          if (!((*info->callbacks->warning)
		        (info, errmsg, name, input_bfd, input_section, offset)))
		    return false;
	          break;
	        }
	    }
	}
    }

  return ret;

#undef CHECK_DISPLACEMENT
}

/* Check whether it's okay to merge objects IBFD and OBFD.  */

static boolean
tricore_elf32_merge_private_bfd_data (ibfd, obfd)
     bfd *ibfd;
     bfd *obfd;
{
  boolean error = false;
  static unsigned long linkmask = 0;
  unsigned long mask;

  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return true;

  if ((bfd_get_arch (ibfd) != bfd_arch_tricore) ||
      (bfd_get_arch (obfd) != bfd_arch_tricore))
    {
      error = true;
      (*_bfd_error_handler)
       (_("%s and/or %s don't use the TriCore architecture."),
        bfd_get_filename (ibfd), bfd_get_filename (obfd));
    }
  else
    {
      unsigned long old_isa, new_isa;

      mask = elf_elfheader (ibfd)->e_flags;
      old_isa = linkmask & bfd_mach_rider_mask;
      new_isa = mask & bfd_mach_rider_mask;
#if 1
      if ((old_isa == bfd_mach_norider) && (new_isa != bfd_mach_norider))
        {
	  /* The first object determines the final mach type.  */
	  old_isa = linkmask = new_isa;
	  elf_elfheader (obfd)->e_flags = linkmask;
	}
      else if ((new_isa == bfd_mach_norider)
      	       || (new_isa > old_isa)	
      	       || ((old_isa == bfd_mach_rider_a)
	           && (new_isa != bfd_mach_rider_a))
	       || ((new_isa == bfd_mach_rider_a)
	           && (old_isa != bfd_mach_rider_a)))
	{
          error = true;
          (*_bfd_error_handler)
           (_("%s uses an incompatible TriCore instruction set architecture."),
            bfd_get_filename (ibfd));
	}
#else
      if ((old_isa != bfd_mach_norider) && (new_isa != bfd_mach_norider)
          && ((old_isa & new_isa) == bfd_mach_norider))
	{
          error = true;
          (*_bfd_error_handler)
           (_("%s uses an incompatible TriCore instruction set architecture."),
            bfd_get_filename (ibfd));
	}
      else
        {
	  linkmask |= mask;
	  elf_elfheader (obfd)->e_flags = linkmask;
	}
#endif
    }

  if (error)
    {
      bfd_set_error (bfd_error_bad_value);
      return false;
    }

  return true;
}

/* Copy e_flags from IBFD to OBFD.  */

static boolean
tricore_elf32_copy_private_bfd_data (ibfd, obfd)
     bfd *ibfd;
     bfd *obfd;
{
  boolean error = false;

  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return true;

  if (bfd_get_arch (ibfd) != bfd_arch_tricore)
    {
      error = true;
      (*_bfd_error_handler)
       (_("%s doesn't use the TriCore architecture."), bfd_get_filename (ibfd));
    }
  else
    elf_elfheader (obfd)->e_flags = elf_elfheader (ibfd)->e_flags;

  if (error)
    {
      bfd_set_error (bfd_error_bad_value);
      return false;
    }

  return true;
}

/* Set the correct machine number (i.e., the ID for the instruction set
   architecture) for a TriCore ELF file.  */

static void
tricore_elf32_set_arch_mach (abfd, arch)
     bfd *abfd;
     enum bfd_architecture arch;
{
  bfd_arch_info_type *ap, *def_ap;
  unsigned long mach;

  if (arch != bfd_arch_tricore)
    return; /* Case already handled by bfd_default_set_arch_mach.  */

  mach = elf_elfheader (abfd)->e_flags & bfd_mach_rider_mask;

  /* Find the default arch_info.  */
  def_ap = (bfd_arch_info_type *) bfd_scan_arch (DEFAULT_ISA);

  /* Scan all sub-targets of the default architecture until we find
     the one that matches "mach".  If we find a target that is not
     the current default, we're making it the new default.  */
  for (ap = def_ap; ap != NULL; ap = (bfd_arch_info_type *) ap->next)
    if (ap->mach == mach)
      {
	abfd->arch_info = ap;
	return;
      }

  abfd->arch_info = &bfd_default_arch_struct;
  bfd_set_error (bfd_error_bad_value);
}

/* This hack is needed because it's not possible to redefine the
   function bfd_default_set_arch_mach.  Since we need to set the
   correct instruction set architecture, we're redefining
   bfd_elf32_object_p below (but calling it here to do the real work)
   and then we're calling tricore_elf32_set_arch_mach to set the
   correct ISA.  */

const bfd_target *
tricore_elf32_object_p (abfd)
     bfd *abfd;
{
  const bfd_target *bt;
  struct elf_backend_data *ebd;
  extern const bfd_target *elf_object_p PARAMS ((bfd *));

  if ((bt = bfd_elf32_object_p (abfd)) != NULL)
    {
      ebd = abfd->xvec->backend_data;
      tricore_elf32_set_arch_mach (abfd, ebd->arch);
    }

  return bt;
}

/* Convert TriCore specific section flags to BFD's equivalent.  */

static boolean
tricore_elf32_section_flags (flags, hdr)
     flagword *flags;
     Elf32_Internal_Shdr *hdr;
{
  if (hdr->sh_flags & SHF_TRICORE_PCP)
    *flags |= SEC_ARCH_BIT_0;

  return true;
}  

/* Same as above, but vice-versa.  */

static boolean
tricore_elf32_fake_sections (abfd, hdr, sec)
     bfd *abfd ATTRIBUTE_UNUSED;
     Elf32_Internal_Shdr *hdr;
     asection *sec;
{
  if (sec->flags & SEC_ARCH_BIT_0)
    hdr->sh_flags |= SHF_TRICORE_PCP;

  return true;
}

/* This callback function is currently unused, but the possibilities we
   could apply to a BFD are pretty much unlimited...  ;-)  */

static boolean
tricore_elf32_set_private_flags (abfd, flags)
     bfd *abfd ATTRIBUTE_UNUSED;
     flagword flags ATTRIBUTE_UNUSED;
{
  return true;
}

/* Given a relocation entry REL in input section IS (part of ABFD) that
   references a bit object, return the bit position of said object within
   the referenced byte, and reset *OK on failure.  INFO points to the
   link info structure, SYMTAB_HDR points to ABFD's symbol table header,
   ISYMBUF points to ABFD's internal symbols, and HASHES points to ABFD's
   symbol hashes.  */

static unsigned long
tricore_elf32_get_bitpos (abfd, info, rel, symtab_hdr, isymbuf, hashes, is, ok)
     bfd *abfd;
     struct bfd_link_info *info;
     Elf_Internal_Rela *rel;
     Elf_Internal_Shdr *symtab_hdr;
     Elf_Internal_Sym *isymbuf;
     struct elf_link_hash_entry **hashes;
     asection *is;
     boolean *ok;
{
  unsigned long symidx = ELF32_R_SYM (rel->r_info);
  asection *bsec;
  struct elf_link_hash_entry *h, *h2;
  size_t len;
  const char *sname = NULL;
  char *pname;

  if (symidx < symtab_hdr->sh_info)
    {
      Elf_Internal_Sym *sym, *sym2;
      const char *sname2;
      asection *boffs;
      unsigned long boffs_shndx;

      if (symidx == SHN_UNDEF)
        {
	  /* This is a classic case of "Can't happen!": it is normally
	     not possible to produce a relocation against a local symbol
	     that is undefined, because the assembler (well, at least
	     GNU as) marks undefined symbols automatically as global,
	     and other assemblers requiring an explicit global/external
	     declaration would certainly gripe if references to local
	     symbols cannot be resolved.  So how can it be that we still
	     need to deal with references to undefined local symbols?
	     Part of the answer is how the GNU linker handles "garbage
	     collection" (enabled with "--gc-sections"), a method to
	     automatically remove any unneded sections.  A section is
	     considered unneeded if the linker script doesn't provide
	     an explicit "KEEP" statement for it, and if there are no
	     symbols within that section that are referenced from any
	     other section.  If both conditions are met, the linker can
	     safely remove this section from the output BFD.  However,
	     when considering whether there are any references to a
	     given symbol, the linker ignores relocations coming from
	     debug sections -- which makes sense, because debug sections
	     don't really contribute to a program's code and data, but
	     are merely relevant to a debugger.  It thus can happen that
	     a debug section references a symbol which is defined in an
	     otherwise unneeded section, and the GNU linker handles this
	     by completely zeroing such relocation entries in case they
	     are referencing global symbols, or by setting a referenced
	     local symbol's symbol index to zero.  This means that for
	     relocations against such "deleted" global symbols the
	     relocation type is 0 (i.e., BFD_RELOC_NONE, which is also
	     mapped to R_TRICORE_NONE), so tricore_elf32_relocate_section
	     will simply ignore it, and for relocations against "deleted"
	     local symbols, the symbol's section index will point to the
	     "undefined section" (SHN_UNDEF) --  a valid input section
	     to _bfd_elf_rela_local_sym (tricore_elf32_relocate_section
	     calls this function whenever it needs to resolve references
	     to local symbols).  Now, if a reference to a deleted symbol
	     is either ignored or properly handled, where's the problem,
	     then?  Well, the problem is that for relocations against
	     "deleted" local symbols only their *symbol index* will be
	     changed, while their original *relocation type* information
	     will remain intact, so the relocation function won't simply
	     ignore such relocs.  But while the "usual" processing of
	     relocations against deleted symbols causes no grief at all
	     (see comment above regarding _bfd_elf_rela_local_sym), this
	     TriCore port of the GNU tools supports the allocation of
	     "single-bit" objects, and so tricore_elf32_relocate_section
	     must call this current function to determine a bit object's
	     position (or "bit offset") within its containing byte.  So,
	     without checking for deleted local bit objects right here
	     (by means of the introductory SHN_UNDEF comparison), the
	     code below could potentially cause illegal memory accesses,
	     because "bfd_section_from_elf_index (abfd, 0)" returns NULL,
	     unless "abfd" actually has an "undefined section" attached
	     to it -- which is a rather unlikely event.  For more details,
	     take a look at elf_link_input_bfd and the various "garbage
	     collect" (gc) functions, all defined in elflink.h.  */

	  return 0;
	}

      /* Local bit symbol; either we haven't relaxed bit variables,
         or a bit section contains only a single local bit variable.
	 In both cases the bit offset is zero, so we don't have to
	 compute it.  However, we're doing some sanity checking, albeit
	 not too fancy: since relocs against local labels are usually
	 adjusted (i.e., turned into relocs against section+offset),
	 the reloc references the section symbol, not the bit symbol,
	 so we skip some tests in this case (of course, we could search
	 for the actual bit symbol, but it isn't worth the effort, as
	 making sure that the reloc is against a bit section, and that
	 the ".boffs" section exists in the input BFD (and has a size
	 of zero), already provides a great deal of assurance).  */
      sym = isymbuf + symidx;
      bsec = bfd_section_from_elf_index (abfd, sym->st_shndx);
      if (sym->st_name != 0)
	sname = (bfd_elf_string_from_elf_section
	    	 (abfd, symtab_hdr->sh_link, sym->st_name));
      if (sname == NULL)
        sname = "<unknown local>";

      /* Make sure the symbol lives in a bit section.  */
      if (strcmp (bsec->name, ".bdata") && strncmp (bsec->name, ".bdata.", 7)
          && strcmp (bsec->name, ".bbss") && strncmp (bsec->name, ".bbss.", 6))
	{
error_no_bit_section:
	  (*_bfd_error_handler)
	   (_("%s(%s+%ld): bit variable \"%s\" defined in non-bit "
	      "section \"%s\""),
	    bfd_archive_filename (abfd), is->name, rel->r_offset,
	    sname, bsec->name);
	  *ok = false;
	  return 0;
	}

      /* Make sure the input BFD has a section ".boffs" with size 0.  */
      boffs = bfd_get_section_by_name (abfd, ".boffs");
      if (boffs == NULL)
        {
          (*_bfd_error_handler) (_("%s: missing section \".boffs\""),
      			         bfd_archive_filename (abfd));
          *ok = false;
          return 0;
        }
      if (boffs->_raw_size != 0)
        {
          (*_bfd_error_handler) (_("%s: section \".boffs\" has non-zero size"),
      			         bfd_archive_filename (abfd));
          *ok = false;
          return 0;
        }

      /* Perform the following tests only if the reloc is against
         an actual bit symbol, not just the section symbol.  */
      if (sym->st_name != 0)
	{
	  /* Make sure the symbol represents a 1-byte object and is
	     associated with a ".pos" symbol in section ".boffs".  */
	  if ((symidx == (symtab_hdr->sh_info - 1))
	      || (ELF_ST_TYPE (sym->st_info) != STT_OBJECT)
	      || (sym->st_size != 1))
	    {
error_no_bit:
	      (*_bfd_error_handler)
	       (_("%s(%s+%ld): symbol \"%s\" doesn't represent a bit object"),
	        bfd_archive_filename (abfd), is->name, rel->r_offset, sname);
	      *ok = false;
	      return 0;
	    }

	  boffs_shndx = _bfd_elf_section_from_bfd_section (abfd, boffs);
	  len = strlen (sname);
	  sym2 = isymbuf + symidx + 1;
	  if ((sym2->st_name == 0)
	      || (sym2->st_shndx != boffs_shndx)
	      || (sym2->st_size != 0)
	      || (sym2->st_value != 0)
	      || (ELF_ST_TYPE (sym2->st_info) != STT_NOTYPE)
	      || ((sname2 = (bfd_elf_string_from_elf_section
			     (abfd, symtab_hdr->sh_link, sym2->st_name)))
		   == NULL)
	      || (strlen (sname2) != (len + 4))
	      || strncmp (sname2, sname, len)
	      || strcmp (sname2 + len, ".pos"))
	    goto error_no_bit;
	}

      return 0;
    }

  /* Global bit symbol; the bit offset is the value of the corresponding
     ".pos" symbol, which must be defined in some ".boffs" section.  */
  h = hashes[symidx - symtab_hdr->sh_info];
  while ((h->root.type == bfd_link_hash_indirect)
         || (h->root.type == bfd_link_hash_warning))
    h = (struct elf_link_hash_entry *) h->root.u.i.link;
  if ((h->root.type != bfd_link_hash_defined)
      && (h->root.type != bfd_link_hash_defweak))
    {
      /* Symbol is undefined; will be catched by the normal relocation
         process in tricore_elf32_relocate_section.  */
      *ok = false;
      return 0;
    }
  bsec = h->root.u.def.section;
  sname = h->root.root.string;

  /* Make sure the symbol lives in a bit section.  */
  if (strcmp (bsec->name, ".bdata") && strncmp (bsec->name, ".bdata.", 7)
      && strcmp (bsec->name, ".bbss") && strncmp (bsec->name, ".bbss.", 6))
    goto error_no_bit_section;

  /* Make sure the symbol represents a 1-byte object and is
     associated with a ".pos" symbol in section ".boffs".  */
  if ((h->type != STT_OBJECT) || (h->size != 1))
    goto error_no_bit;

  len = strlen (sname) + 5;
  if ((pname = bfd_malloc (len)) == NULL)
    {
      *ok = false;
      return 0;
    }
  sprintf (pname, "%s.pos", h->root.root.string);
  h2 = (struct elf_link_hash_entry *)
        bfd_link_hash_lookup (info->hash, pname, false, false, false);
  free (pname);
  if ((h2 == NULL) || (h2->root.type != bfd_link_hash_defined)
      || (h2->type != STT_NOTYPE)
      || (h2->size != 0)
      || strcmp (h2->root.u.def.section->name, ".boffs"))
    goto error_no_bit;

  return h2->root.u.def.value;
}

/* This is called once by tricore_elf32_relocate_section, and only if
   the linker was directed to create a map file.  We walk through all
   input BFDs (in link order) and print out all allocated bit objects
   (note that garbage collection ("--gc-sections") and bit relaxation
   ("-relax"|"--relax-bdata") have already been performed formerly in
   case they were requested by the user).  */

static void
tricore_elf32_list_bit_objects (info)
     struct bfd_link_info *info;
{
  FILE *out = tricore_elf32_map_file;
  bfd *ibfd;
  asection *bdata, *boffs;
  boolean header_printed = false;
  Elf_Internal_Shdr *symtab_hdr, *strtab_hdr;
  Elf_Internal_Sym *isymbuf;
  unsigned char *strtab;
  unsigned int symcount;
  unsigned int boffs_shndx;
  struct elf_link_hash_entry **beg_hashes, **end_hashes, **sym_hashes;

  for (ibfd = info->input_bfds; ibfd; ibfd = ibfd->link_next)
    {
      /* If this input BFD doesn't have a ".boffs" section, it also
         won't have any bit sections, so we can safely skip it.  */
      if (((boffs = bfd_get_section_by_name (ibfd, ".boffs")) == NULL)
          || (bfd_get_section_flags (ibfd, boffs) & SEC_EXCLUDE)
	  || (boffs->_raw_size != 0))
	continue;

      boffs_shndx = _bfd_elf_section_from_bfd_section (ibfd, boffs);
      isymbuf = NULL;
      strtab = NULL;
      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
      if (symtab_hdr->sh_info != 0)
        {
	  isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	  if (isymbuf == NULL)
	    isymbuf = bfd_elf_get_elf_syms (ibfd, symtab_hdr,
	    				    symtab_hdr->sh_info, 0,
					    NULL, NULL, NULL);
	  if (isymbuf == NULL)
	    continue;  /* This error will be catched elsewhere.  */

	  strtab_hdr = &elf_tdata (ibfd)->strtab_hdr;
	  strtab = strtab_hdr->contents;
	  if (strtab == NULL)
	    {
	      bfd_elf_get_str_section (ibfd, symtab_hdr->sh_link);
	      strtab = strtab_hdr->contents;
	    }
	  BFD_ASSERT (strtab);
	}

      /* Iterate over the sections of this BFD; skip non-bit sections.  */
      for (bdata = ibfd->sections; bdata; bdata = bdata->next)
	{
          if ((bfd_get_section_flags (ibfd, bdata) & SEC_EXCLUDE)
	      || (bdata->_raw_size == 0)
	      || (strcmp (bdata->name, ".bdata")
	          && strncmp (bdata->name, ".bdata.", 7)
		  && strcmp (bdata->name, ".bbss")
		  && strncmp (bdata->name, ".bbss.", 6)))
	    continue;

	  if (!header_printed)
	    {
	      fprintf (out, "\n%s\n",
	  	       _("Allocating bit symbols (l/g = local or global "
		         "scope)"));
	      fprintf (out, "%s\n\n",
	  	       _("Bit symbol          bit address  l/g  file"));
	      header_printed = true;
	    }

	  /* Walk through all local symbols in this section.  */
          if (isymbuf != NULL)
	    {
	      Elf_Internal_Sym *isym, *isymend = isymbuf + symtab_hdr->sh_info;
              const char *this_file = NULL, *this_base = NULL;
	      unsigned int bdata_shndx;

	      bdata_shndx = _bfd_elf_section_from_bfd_section (ibfd, bdata);
	      for (isym = isymbuf; isym < isymend; ++isym)
	        {
	          if (ELF_ST_TYPE (isym->st_info) == STT_FILE)
	            {
	              this_file = strtab + isym->st_name;
		      this_base = strchr (this_file, '.');
		      continue;
		    }

	          if ((isym->st_shndx == bdata_shndx)
	              && (ELF_ST_TYPE (isym->st_info) == STT_OBJECT))
		    {
		      Elf_Internal_Sym *isym2;
		      char *aname;
		      size_t len;

		      aname = strtab + isym->st_name;
		      len = strlen (aname);
		      if (isym->st_size != 1)
		        {
		          (*_bfd_error_handler)
		           (_("%s: bit symbol \"%s\" has size != 1 (%ld)"),
		            bfd_archive_filename (ibfd), aname, isym->st_size);
		          bfd_set_error (bfd_error_bad_value);
		          continue;
		        }
		      for (isym2 = isym + 1; isym2 < isymend; ++isym2)
		        {
			  const char *aname2;

		          /* Bail out if this symbol indicates the beginning
		             of a new file, as this means that the ".pos"
			     symbol is missing in the current file (shouldn't
			     really happen, but just in case).  */
	                  if (ELF_ST_TYPE (isym2->st_info) == STT_FILE)
		            {
			      isym2 = isymend;
			      break;
			    }

			  aname2 = strtab + isym2->st_name;
		          if ((isym2->st_shndx == boffs_shndx)
			      && (ELF_ST_TYPE (isym2->st_info) == STT_NOTYPE)
			      && (strlen (aname2) == (len + 4))
			      && !strncmp (aname2, aname, len)
			      && !strcmp (aname2 + len, ".pos"))
			    {
			      fprintf (out, "%-19s 0x%08lx.%ld  l   %s",
			  	       aname,
				       (bdata->output_section->vma
				        + bdata->output_offset
				        + isym->st_value),
				       isym2->st_value,
				       bfd_archive_filename (ibfd));

			      /* If this is an input object that is the result
			         of a relocateable link run, also print the
				 name of the original file that defined this
				 bit object.  */
			      if (this_file
			          && this_base
			          && strncmp (this_file, ibfd->filename,
			      		      this_base - this_file))
			        fprintf (out, " <%s>", this_file);
			      fprintf (out, "\n");
			      break;
			    }
		        }
		      if (isym2 == isymend)
		        {
#ifndef FATAL_BITVAR_ERRORS
		          (*_bfd_error_handler)
		           (_("%s: warning: missing or invalid local bit "
			      "position "
		              "symbol \"%s.pos\" in section \".boffs\""),
			    bfd_archive_filename (ibfd), aname);
#else
		          (*_bfd_error_handler)
		           (_("%s: missing or invalid local bit position "
		              "symbol \"%s.pos\" in section \".boffs\""),
			    bfd_archive_filename (ibfd), aname);
		          bfd_set_error (bfd_error_bad_value);
#endif
		        }
		    }
	        }
	    }

	  /* Walk through all global symbols in this section.  */
          symcount = (symtab_hdr->sh_size / sizeof (Elf32_External_Sym)
      		      - symtab_hdr->sh_info);
          beg_hashes = elf_sym_hashes (ibfd);
          end_hashes = elf_sym_hashes (ibfd) + symcount;
          for (sym_hashes = beg_hashes; sym_hashes < end_hashes; ++sym_hashes)
	    {
	      struct elf_link_hash_entry *sym = *sym_hashes;

	      if ((sym->root.u.def.section == bdata)
	          && (sym->type == STT_OBJECT)
	          && ((sym->root.type == bfd_link_hash_defined)
	              || (sym->root.type == bfd_link_hash_defweak)))
	        {
	          struct elf_link_hash_entry *sym2;
	          const char *aname = sym->root.root.string;
	          char *pname;
	          size_t len;

	          if (sym->size != 1)
	            {
		      (*_bfd_error_handler)
		       (_("%s: bit symbol \"%s\" has size != 1 (%ld)"),
		        bfd_archive_filename (ibfd), aname, sym->size);
		      bfd_set_error (bfd_error_bad_value);
	              continue;
	            }
	          if (sym->elf_link_hash_flags
	              & (ELF_LINK_HASH_REF_DYNAMIC | ELF_LINK_HASH_DEF_DYNAMIC))
		    {
		      (*_bfd_error_handler)
		       (_("%s: bit symbol \"%s\" defined or referenced by a "
		          "shared object"),
	  	       bfd_archive_filename (ibfd), aname);
		      bfd_set_error (bfd_error_bad_value);
	              continue;
		    }

	          /* Lookup the global symbol "aname.pos", which should be
	             defined in section ".boffs" and have type STT_NOTYPE.  */
	          len = strlen (aname) + 5;
	          if ((pname = bfd_malloc (len)) == NULL)
	            return;

		  sprintf (pname, "%s.pos", aname);
	          sym2 = (struct elf_link_hash_entry *)
	      	          bfd_link_hash_lookup (info->hash, pname,
	      				        false, false, false);
	          if ((sym2 == NULL)
		      || (sym2->root.u.def.section != boffs)
	              || (sym2->type != STT_NOTYPE))
		    {
		      (*_bfd_error_handler)
		       (_("%s: missing or invalid bit position symbol \"%s\""),
		        bfd_archive_filename (ibfd), pname);
		      bfd_set_error (bfd_error_bad_value);
		      free (pname);
		      continue;
		    }

	          free (pname);
	          fprintf (out, "%-19s 0x%08lx.%ld  g   %s\n",
		           aname,
		           (bdata->output_section->vma
			    + bdata->output_offset
			    + sym->root.u.def.value),
		           sym2->root.u.def.value,
		           bfd_archive_filename (ibfd));
	        }
	    }
        }
    }
}

/* Adjust all relocs contained in input object ABFD that reference the
   given bit object: SECIDX is the index of a bit section, OLD is the
   former offset of the bit address within this section, NEW is the
   new offset, BPOS is the new bit position (0..7), BIDX is the symbol
   index of the symbol representing that bit offset (i.e., "name.pos" in
   section ".boffs"), and INFO is a pointer to the link info structure.  */

static boolean
tricore_elf32_adjust_bit_relocs (abfd, info, secidx, old, new, bpos, bidx)
     bfd *abfd;
     struct bfd_link_info *info;
     unsigned long secidx;
     bfd_vma old, new;
     int bpos;
     unsigned int bidx;
{
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *internal_relocs;
  Elf_Internal_Rela *irel, *irelend;
  Elf_Internal_Rela *lastrel = NULL;
  Elf_Internal_Sym *isymbuf, *sym;
  unsigned long symidx;
  asection *sec;
  int r_type;
  boolean bitoff_seen = false;
  static boolean initialized = false;
  static char **reloc_adjusted;

  /* If nothing changes, return immediately.  */
  if ((old == new) && (bpos == 0))
    return true;

  /* We must remember which relocations we've already changed, as we have
     to avoid modifying the same relocation multiple times.  Why?  Well,
     consider the following case: first we change a relocation from, say,
     .bdata+42 to .bdata+3 (note that it doesn't matter whether we're also
     (or merely) changing the bit position); then another, yet unmodified
     relocation references .bdata+3, which should now, after relaxation,
     be changed into a relocation against .bdata+7.  Without remembering
     which relocs we've already changed, we would again modify the already
     modified relocation against .bdata+3 (which was originally against
     .bdata+42) -- ouch!  Unfortunately, this ain't too obvious...  */
  if (!initialized)
    {
      int i, max = -1;
      bfd *ibfd;
      asection *section;

      /* Find the highest section ID of all input sections.  */
      for (ibfd = info->input_bfds; ibfd; ibfd = ibfd->link_next)
        for (section = ibfd->sections; section; section = section->next)
	  if (section->id > max)
	    max = section->id;

      ++max;
      if ((reloc_adjusted = bfd_malloc (max * sizeof (char *))) == NULL)
        return false;

      for (i = 0; i < max; ++i)
	reloc_adjusted[i] = NULL;

      initialized = true;
    }

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;

  for (sec = abfd->sections; sec != NULL; sec = sec->next)
    {
      if (((sec->flags & SEC_RELOC) == 0)
          || (sec->reloc_count == 0))
	continue;

      internal_relocs = (_bfd_elf32_link_read_relocs
  		         (abfd, sec, (PTR) NULL, (Elf_Internal_Rela *) NULL,
		          true /* Cache the relocs.  */));
      if (internal_relocs == NULL)
        return false;

      if (!reloc_adjusted[sec->id])
        {
	  if ((reloc_adjusted[sec->id] = bfd_malloc (sec->reloc_count)) == NULL)
	    return false;

	  memset (reloc_adjusted[sec->id], 0, sec->reloc_count);
	}

      irelend = internal_relocs + sec->reloc_count;
      for (irel = internal_relocs; irel < irelend; irel++)
        {
	  if (irel->r_addend != old)
	    {
	      bitoff_seen = false;
	      continue;
	    }

	  r_type = ELF32_R_TYPE (irel->r_info);
	  if ((r_type == R_TRICORE_NONE)
	      || reloc_adjusted[sec->id][irel - internal_relocs])
	    {
	      /* Don't touch an already modified reloc; see comment above.  */
	      bitoff_seen = false;
	      continue;
	    }
          if ((r_type < 0)
              || (r_type < R_TRICORE_NONE)
	      || (r_type >= R_TRICORE_max))
	    {
	      (*_bfd_error_handler) (_("%s: unknown relocation type %d"),
				     sec->name, r_type);
	      bfd_set_error (bfd_error_bad_value);
	      return false;
	    }
	  if (r_type == R_TRICORE_BITPOS)
	    {
	      bitoff_seen = true;
	      lastrel = irel;
	      continue;
	    }

	  symidx = ELF32_R_SYM (irel->r_info);
	  if (symidx >= symtab_hdr->sh_info)
	    {
	      /* Relocation against a global symbol.  Can be safely
	         ignored, because even if bitoff_seen is true and
		 this symbol turns out to not represent a bit symbol,
		 this will be catched either by the relaxation pass
		 or later by tricore_elf32_relocate_section when it
		 calls tricore_elf32_get_bitpos.  */
	      bitoff_seen = false;
	      continue;
	    }

	  sym = isymbuf + symidx;
	  if ((sym->st_shndx != secidx)
	      || (ELF_ST_TYPE (sym->st_info) != STT_SECTION))
	    {
	      /* This symbol doesn't belong to the given bit section,
	         or the relocation is not against the section symbol.  */
	      bitoff_seen = false;
	      continue;
	    }

	  if (tricore_elf32_debug_relax)
	    {
	      asection *bsec;

	      bsec = bfd_section_from_elf_index (abfd, sym->st_shndx);
	      printf ("    %s+%ld: symbol at %s+%ld, changing ",
	              sec->name, irel->r_offset, bsec->name, irel->r_addend);
	    }

	  /* Remember that this reloc is going to be changed; see
	     comment above for details about why this is necessary.  */
	  reloc_adjusted[sec->id][irel - internal_relocs] = 1;
	  if (bitoff_seen)
	    {
	      if (tricore_elf32_debug_relax)
	        printf ("bpos from 0 to %d\n", bpos);

	      irel->r_info = ((irel->r_info & 0xff) | bidx << 8);
	      irel->r_addend = 0;
	      lastrel->r_info = R_TRICORE_NONE;
	      bitoff_seen = false;
	    }
	  else
	    {
	      if (tricore_elf32_debug_relax)
	        printf ("addr from %ld to %ld\n", old, new);

	      irel->r_addend = new;
	    }
	}
    }

  return true;
}

/* Allocate and return a new symbol entry (only used when an extended map
   file containing symbol listings was requested).  */

static symbol_t *
tricore_elf32_new_symentry ()
{
  if (symbol_list_idx == -1)
    {
      symbol_list =
        (symbol_t *) bfd_malloc (symbol_list_max * sizeof (symbol_t));
      BFD_ASSERT (symbol_list);
      symbol_list_idx = 0;
    }
  else if (++symbol_list_idx == symbol_list_max)
    {
      symbol_list_max <<= 2;
      symbol_list =
        (symbol_t *) bfd_realloc (symbol_list,
      				  symbol_list_max * sizeof (symbol_t));
      BFD_ASSERT (symbol_list);
    }

  return &symbol_list[symbol_list_idx];
}

/* This is called by qsort when sorting symbol_list by address.  */

static int
tricore_elf32_extmap_sort_addr (p1, p2)
     const void *p1, *p2;
{
  symbol_t *s1 = (symbol_t *) p1, *s2 = (symbol_t *) p2;

  if (s1->address == s2->address)
    {
      if (s1->is_bit)
        return s1->bitpos - s2->bitpos;

      return strcmp (s1->name, s2->name);
    }
  else if (s1->address < s2->address)
    return -1;

  return 1;
}

/* This is called by qsort when sorting symbol_list by name.  */

static int
tricore_elf32_extmap_sort_name (p1, p2)
     const void *p1, *p2;
{
  symbol_t *s1 = (symbol_t *) p1, *s2 = (symbol_t *) p2;
  int cv;

  cv = strcmp (s1->name, s2->name);
  if (cv == 0)
    {
      if (!s1->is_static && s2->is_static)
        return -1;
      else if (s1->is_static && !s2->is_static)
        return 1;

      return (int) (s1->address - s2->address);
    }

  return cv;
}

/* This is called by qsort when sorting memregs by name.  */

static int
tricore_elf32_extmap_sort_memregs (p1, p2)
     const void *p1, *p2;
{
  memreg_t *m1 = (memreg_t *) p1, *m2 = (memreg_t *) p2;

  if (!strcmp (m1->name, "*default*"))
    return 1;
  else if (!strcmp (m2->name, "*default*"))
    return -1;

  return strcmp (m1->name, m2->name);
}

/* This is the callback function for bfd_link_hash_traverse; it adds
   the global symbol pointed to by ENTRY to the symbol list.  */

static boolean
tricore_elf32_extmap_add_sym (entry, link_info)
     struct bfd_link_hash_entry *entry;
     PTR link_info;
{
  struct elf_link_hash_entry *h = (struct elf_link_hash_entry *) entry;
  struct bfd_link_info *info = (struct bfd_link_info *) link_info;
  symbol_t *sym;

  if (((h->root.type != bfd_link_hash_defined)
       && (h->root.type != bfd_link_hash_defweak))
      || (bfd_get_section_flags (h->root.u.def.section->owner,
      				 h->root.u.def.section)
          & SEC_EXCLUDE)
      || !strcmp (h->root.u.def.section->name, ".boffs")
      || (h->root.u.def.section->output_section == NULL))
    return true;

    sym = tricore_elf32_new_symentry ();
    sym->name = h->root.root.string;
    sym->address = (h->root.u.def.section->output_section->vma
	      	    + h->root.u.def.section->output_offset
		    + h->root.u.def.value);
    sym->region_name = "*default*";
    sym->is_bit = false;
    sym->bitpos = 0;
    sym->is_static = false;
    sym->size = h->size;
    sym->section = h->root.u.def.section;

    if (!strcmp (sym->section->name, ".bdata")
	|| !strncmp (sym->section->name, ".bdata.", 7)
	|| !strcmp (sym->section->name, ".bbss")
	|| !strncmp (sym->section->name, ".bbss.", 6))
      {
	size_t len = strlen (sym->name) + 5;
	char *pname;
	struct elf_link_hash_entry *psym;

	if ((pname = bfd_malloc (len)) == NULL)
	  return false;

	sprintf (pname, "%s.pos", sym->name);
	psym = (struct elf_link_hash_entry *)
		bfd_link_hash_lookup (info->hash, pname,
			  	      false, false, false);
	free (pname);
	if ((psym != NULL)
	    && !strcmp (psym->root.u.def.section->name, ".boffs"))
	  {
	    sym->is_bit = true;
	    sym->bitpos = psym->root.u.def.value;
	  }
      }

  return true;
}

/* Output additional information to the map file.  INFO is the link
   info that contains pointers to all input sections and the hash table.  */

static void
tricore_elf32_do_extmap (info)
     struct bfd_link_info *info;
{
  FILE *out = tricore_elf32_map_file;
  memreg_t *memregs = NULL;
  int nmemregs;
  size_t maxlen_size = 4;
  size_t maxlen_symname = 4;
  size_t maxlen_memreg = 6;
  size_t maxlen_osecname = 5;
  size_t maxlen_isecname = 5;
  size_t maxlen_modname = 12;
  boolean bit_seen = false;

  fprintf (out, "\n----- BEGIN EXTENDED MAP LISTING -----\n\n");

  if (tricore_elf32_extmap_header)
    {
      time_t now;
      char *tbuf;

      time (&now);
      tbuf = ctime (&now);
      fprintf (out, ">>> Linker and link run information\n\n");
      fprintf (out, "Linker version: ");
      tricore_elf32_extmap_ld_version (out);
      fprintf (out, "Name of linker executable: %s\n",
      	       tricore_elf32_extmap_ld_name);
      fprintf (out, "Date of link run: %s", tbuf);
      fprintf (out, "Name of linker map file: %s\n",
	       tricore_elf32_map_filename);
      fprintf (out, "\n");
    }

  /* Get hold of the defined memory regions, if required.  */
  if ((tricore_elf32_extmap_get_memregs != NULL)
      && ((tricore_elf32_extmap_syms_by_name > 0)
          || (tricore_elf32_extmap_syms_by_addr > 0)
	  || tricore_elf32_extmap_memory_segments))
    memregs = tricore_elf32_extmap_get_memregs (&nmemregs);

  /* Collect symbol information, if required.  */
  if ((tricore_elf32_extmap_syms_by_name > 0)
      || (tricore_elf32_extmap_syms_by_addr > 0))
    {
      symbol_t *sym;
      bfd_vma max_size = 0;
      char size_str[20];
      int i;
      size_t len;

      /* Add global symbols.  */
      bfd_link_hash_traverse (info->hash, tricore_elf32_extmap_add_sym,
      			      (PTR) info);

      /* Add local symbols, if so requested.  */
      if ((tricore_elf32_extmap_syms_by_name == 2)
      	  || (tricore_elf32_extmap_syms_by_addr == 2))
	{
	  Elf_Internal_Shdr *symtab_hdr, *strtab_hdr;
	  Elf_Internal_Sym *isymbuf, *isym, *isymend;
	  bfd *ibfd;
	  asection *boffs, *section;
	  unsigned int boffs_shndx = -1;
	  unsigned char *strtab;

	  for (ibfd = info->input_bfds; ibfd; ibfd = ibfd->link_next)
	    {
	      if ((boffs = bfd_get_section_by_name (ibfd, ".boffs")) != NULL)
		boffs_shndx = _bfd_elf_section_from_bfd_section (ibfd, boffs);

	      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
	      if (symtab_hdr->sh_info == 0)
	        continue;  /* Input object has no local symbols.  */

	      isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	      if (isymbuf == NULL)
	        isymbuf = bfd_elf_get_elf_syms (ibfd, symtab_hdr,
						symtab_hdr->sh_info, 0,
						NULL, NULL, NULL);
	      if (isymbuf == NULL)
	        continue;  /* This error will be catched elsewhere.  */

	      isymend = isymbuf + symtab_hdr->sh_info;
	      strtab_hdr = &elf_tdata (ibfd)->strtab_hdr;
	      strtab = strtab_hdr->contents;
	      if (strtab == NULL)
	        {
		  bfd_elf_get_str_section (ibfd, symtab_hdr->sh_link);
		  strtab = strtab_hdr->contents;
		}
	      BFD_ASSERT (strtab);

	      for (isym = isymbuf; isym < isymend; ++isym)
	        {
		  if (isym->st_shndx == SHN_ABS)
		    section = bfd_abs_section_ptr;
		  else
		    section = bfd_section_from_elf_index (ibfd, isym->st_shndx);
		  if ((section == NULL)
		      || (section->output_section == NULL)
		      || (isym->st_name == 0)
		      || (ELF_ST_TYPE (isym->st_info) == STT_FILE)
		      || (boffs && (isym->st_shndx == boffs_shndx))
		      || (bfd_get_section_flags (ibfd, section) & SEC_EXCLUDE))
		    continue;

		  sym = tricore_elf32_new_symentry ();
		  sym->name = strtab + isym->st_name;
		  sym->address = (section->output_section->vma
				  + section->output_offset
				  + isym->st_value);
		  sym->region_name = "*default*";
		  sym->is_bit = false;
		  sym->bitpos = 0;
		  sym->is_static = true;
		  sym->size = isym->st_size;
		  sym->section = section;
		  if (boffs
		      && (!strcmp (sym->section->name, ".bdata")
			  || !strncmp (sym->section->name, ".bdata.", 7)
			  || !strcmp (sym->section->name, ".bbss")
			  || !strncmp (sym->section->name, ".bbss.", 6)))
		    {
		      Elf_Internal_Sym *isym2;

		      len = strlen (sym->name);
		      for (isym2 = isym + 1; isym2 < isymend; ++isym2)
		        {
			  const char *aname2;

			  if (ELF_ST_TYPE (isym2->st_info) == STT_FILE)
			    {
			      isym2 = isymend;
			      break;
			    }
			  aname2 = strtab + isym2->st_name;
			  if ((isym2->st_shndx == boffs_shndx)
			      && (ELF_ST_TYPE (isym2->st_info) == STT_NOTYPE)
			      && (strlen (aname2) == (len + 4))
			      && !strncmp (aname2, sym->name, len)
			      && !strcmp (aname2 + len, ".pos"))
			    {
			      sym->is_bit = true;
			      sym->bitpos = isym2->st_value;
			      break;
			    }
			}
		      if (isym2 == isymend)
		        {
#ifndef FATAL_BITVAR_ERRORS
			  (*_bfd_error_handler)
			   (_("%s: warning: missing or invalid local bit "
			      "position "
			      "symbol \"%s.pos\" in section \".boffs\""),
			    bfd_archive_filename (ibfd), sym->name);
#else
			  (*_bfd_error_handler)
			   (_("%s: missing or invalid local bit position "
			      "symbol \"%s.pos\" in section \".boffs\""),
			    bfd_archive_filename (ibfd), sym->name);
			  bfd_set_error (bfd_error_bad_value);
#endif
			}
		    }
		}
	    }
	}

      /* Compute memory regions, alignment and maximum name lengths.  */
      for (sym = symbol_list, i = 0; i <= symbol_list_idx; ++sym, ++i)
	{
	  if ((len = strlen (sym->name)) > maxlen_symname)
	    maxlen_symname = len;
	  if (!strcmp (sym->section->name, "*ABS*"))
	    sym->region_name = "*ABS*";
	  else if (memregs)
	    {
	      int j;

	      for (j = 0; j < nmemregs; ++j)
		if ((sym->address >= memregs[j].start)
		    && (sym->address < (memregs[j].start + memregs[j].length)))
		  {
		    sym->region_name = memregs[j].name;
		    break;
		  }
	    }
	  if ((len = strlen (sym->region_name)) > maxlen_memreg)
	    maxlen_memreg = len;
	  if (sym->size > max_size)
	    max_size = sym->size;
	  if ((len = strlen (sym->section->name)) > maxlen_isecname)
	    maxlen_isecname = len;
	  if ((len = strlen (sym->section->output_section->name))
	       > maxlen_osecname)
	    maxlen_osecname = len;
	  if ((sym->section->owner != NULL)
	      && ((len = strlen (bfd_archive_filename (sym->section->owner)))
		   > maxlen_modname))
	    maxlen_modname = len;
	  if (sym->is_bit)
	    {
	      sym->align = 0;
	      bit_seen = true;
	    }
	  else if ((sym->address & 1)
		   || (sym->size & 1)
	  	   || !strcmp (sym->section->name, "*ABS*"))
	    sym->align = 1;
	  else if ((bfd_get_section_flags (sym->section->owner, sym->section)
	  	    & SEC_CODE))
	    sym->align = 2;
	  else if (!(sym->address & 7))
	    sym->align = 8;
	  else if (!(sym->address & 3))
	    sym->align = 4;
	  else
	    sym->align = 2;
	  if ((sym->size != 0) && (sym->size <= 8))
	    while (sym->align > (int) sym->size)
	      sym->align >>= 1;
	}
      if ((maxlen_size = sprintf (size_str, "%ld", max_size)) < 4)
        maxlen_size = 4;
    }

  /* Output symbol list (sorted by addr), if so requested.  */
  if (tricore_elf32_extmap_syms_by_addr)
    {
      int i, max_chars;
      symbol_t *sym = symbol_list;

      qsort (sym, symbol_list_idx + 1, sizeof (symbol_t),
	     tricore_elf32_extmap_sort_addr);
      fprintf (out, ">>> Symbols (global %s; sorted by address)\n\n",
               (tricore_elf32_extmap_syms_by_addr == 1)
	        ? "only" : "(S = g) and static (S = l)");
      max_chars = (10 + (bit_seen ? 2 : 0) + 1 + 10 + 1
      		   + maxlen_size + 1
                   + ((tricore_elf32_extmap_syms_by_addr == 1) ? 0 : 2)
		   + maxlen_symname + 1
		   + maxlen_memreg + 1
		   + maxlen_osecname + 1
		   + maxlen_isecname + 1
		   + maxlen_modname);
      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);
      fprintf (out, "\n");
      fprintf (out, "%-*s%s %-*s %*s %s%-*s %-*s %-*s %-*s %-*s\n",
      	       10, "Start",
      	       bit_seen ? "  " : "",
	       10, "End",
	       maxlen_size, "Size",
	       (tricore_elf32_extmap_syms_by_addr == 1) ? "" : "S ",
	       maxlen_symname, "Name",
	       maxlen_memreg, "Memory",
	       maxlen_osecname, "O-Sec",
	       maxlen_isecname, "I-Sec",
	       maxlen_modname, "Input object");
      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);
      fprintf (out, "\n");

      for (i = 0; i <= symbol_list_idx; ++sym, ++i)
        {
	  if ((tricore_elf32_extmap_syms_by_addr == 1) && sym->is_static)
	    continue;

	  if (sym->is_bit)
	    fprintf (out, "0x%08lx.%d ", sym->address, sym->bitpos);
	  else
	    fprintf (out, "0x%08lx%s ", sym->address, bit_seen ? "  " : "");
	  fprintf (out, "0x%08lx %*ld %s%-*s %-*s %-*s %-*s %s\n",
	  	   sym->address + sym->size - ((sym->size > 0) ? 1 : 0),
	  	   maxlen_size, sym->size,
	           (tricore_elf32_extmap_syms_by_addr == 1)
		    ? ""
		    : (sym->is_static ? "l " : "g "),
		   maxlen_symname, sym->name,
		   maxlen_memreg, sym->region_name,
		   maxlen_osecname, sym->section->output_section->name,
		   maxlen_isecname, sym->section->name,
		   (sym->section->owner == NULL)
		    ? "*ABS*"
		    : bfd_archive_filename (sym->section->owner));
	}
      fprintf (out, "\n");
    }

  /* Output symbol list (sorted by name), if so requested.  */
  if (tricore_elf32_extmap_syms_by_name)
    {
      int i, max_chars;
      symbol_t *sym = symbol_list;

      qsort (sym, symbol_list_idx + 1, sizeof (symbol_t),
	     tricore_elf32_extmap_sort_name);
      fprintf (out, ">>> Symbols (global %s; sorted by name)\n\n",
               (tricore_elf32_extmap_syms_by_name == 1)
	        ? "only" : "(S = g) and static (S = l)");
      max_chars = (maxlen_symname + 1
      		   + ((tricore_elf32_extmap_syms_by_name == 1) ? 0 : 2)
      		   + 10 + (bit_seen ? 2 : 0) + 1 + 10 + 1
      		   + maxlen_size + 1
		   + maxlen_memreg + 1
		   + maxlen_osecname + 1
		   + maxlen_isecname + 1
		   + maxlen_modname);
      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);
      fprintf (out, "\n");
      fprintf (out, "%-*s %s%-*s%s %-*s %*s %-*s %-*s %-*s %-*s\n",
	       maxlen_symname, "Name",
	       (tricore_elf32_extmap_syms_by_name == 1) ? "" : "S ",
	       10, "Start",
	       bit_seen ? "  " : "",
	       10, "End",
	       maxlen_size, "Size",
	       maxlen_memreg, "Memory",
	       maxlen_osecname, "O-Sec",
	       maxlen_isecname, "I-Sec",
	       maxlen_modname, "Input object");
      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);
      fprintf (out, "\n");

      for (i = 0; i <= symbol_list_idx; ++sym, ++i)
        {
	  if ((tricore_elf32_extmap_syms_by_name == 1) && sym->is_static)
	    continue;

	  fprintf (out, "%-*s %s",
		   maxlen_symname, sym->name,
	           (tricore_elf32_extmap_syms_by_name == 1)
		    ? ""
		    : (sym->is_static ? "l " : "g "));
	  if (sym->is_bit)
	    fprintf (out, "0x%08lx.%d ", sym->address, sym->bitpos);
	  else
	    fprintf (out, "0x%08lx%s ", sym->address, bit_seen ? "  " : "");
	  fprintf (out, "0x%08lx %*ld %-*s %-*s %-*s %s\n",
	  	   sym->address + sym->size - ((sym->size > 0) ? 1 : 0),
	  	   maxlen_size, sym->size,
		   maxlen_memreg, sym->region_name,
		   maxlen_osecname, sym->section->output_section->name,
		   maxlen_isecname, sym->section->name,
		   (sym->section->owner == NULL)
		    ? "*ABS*"
		    : bfd_archive_filename (sym->section->owner));
	}
      fprintf (out, "\n");
    }

  /* Output memory segments (sorted by name), if so requested.  */
  if (tricore_elf32_extmap_memory_segments && (memregs != NULL))
    {
      int i, max_chars;
      size_t len;

      qsort (memregs, nmemregs, sizeof (memreg_t),
	     tricore_elf32_extmap_sort_memregs);
      fprintf (out, ">>> Memory segments (sorted by name)\n\n");

      for (i = 0; i < nmemregs; ++i)
	if ((len = strlen (memregs[i].name)) > maxlen_memreg)
	  maxlen_memreg = len;

      max_chars = (maxlen_memreg + 1
      		   + 10 + 1
		   + 10 + 1
		   + 10 + 1
		   + 10);
      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);
      fprintf (out, "\n");
      fprintf (out, "%-*s %-*s %-*s %-*s %-*s\n",
      	       maxlen_memreg, "Name",
	       10, "Start",
	       10, "End",
	       10, "Used",
	       10, "Free");
      for (i = 0; i < max_chars; ++i)
        fputc ('=', out);
      fprintf (out, "\n");

      for (i = 0; i < nmemregs; ++i)
        fprintf (out, "%-*s 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n",
		 maxlen_memreg, memregs[i].name,
		 memregs[i].start,
		 memregs[i].start + memregs[i].length
		  - (((memregs[i].length > 0)
		      && (memregs[i].length != ~(bfd_size_type) 0)) ? 1 : 0),
		 memregs[i].used,
		 memregs[i].length - memregs[i].used);
      fprintf (out, "\n");
    }

  fprintf (out, "\n----- END EXTENDED MAP LISTING -----\n\n");
}

/* Relax a TriCore ELF section.  We use this function to take care of
   PC-relative call and jump instructions that cannot reach their target
   addresses (i.e., the target address is more than +/-16 MB away from
   the referencing call/jump instruction, and it is also not within the
   first 16 KB of a 256 MB TriCore memory segment).  In that case, we
   build a "trampoline" at the end of the text section that loads the
   target address into address register %a2 and then jumps indirectly
   through this register; the original call/jump instruction is modified
   to branch to the trampoline.  To save memory, we're keeping track of
   all target addresses for which we've already built a trampoline; all
   call/jump instructions referencing the same target address will also
   use the same trampoline.
   
   Another thing we're doing here is to compress sections containing
   bit objects.  Such bit objects initially occupy one byte each and are
   accessed via their absolute address and bit position zero; after we've
   relaxed them, up to eight bits share a single byte, and their bit
   position can be 0..7, so we need to adjust all relocations and symbols
   (in section ".boffs") dealing with bit objects.  */

static boolean
tricore_elf32_relax_section (abfd, sec, info, again)
     bfd *abfd;
     asection *sec;
     struct bfd_link_info *info;
     boolean *again;
{
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *internal_relocs = NULL;
  Elf_Internal_Rela *irel, *irelend;
  bfd_byte *contents = NULL;
  Elf_Internal_Sym *isymbuf = NULL;
  static boolean initialized = false;
  static boolean *rela_malloced = NULL;
  static struct _rela
  {
    bfd_vma offset;
    bfd_vma addend;
    unsigned long symoff;
    int size;
  } **rela_p = NULL;
  static int *num_rela = NULL, *max_rela = NULL;
  int now_rela = 0, idx = sec->id;

  if (tricore_elf32_debug_relax)
    printf ("Relaxing %s(%s) [secid = %d], raw = %ld, cooked = %ld\n",
  	    bfd_archive_filename (abfd), sec->name, sec->id,
	    sec->_raw_size, sec->_cooked_size);

  /* Assume nothing changes.  */
  *again = false;

  /* If this is the first time we've been called for this section,
     initialize its cooked size.  */
  if (sec->_cooked_size == 0)
    sec->_cooked_size = sec->_raw_size;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;

  /* If requested, compress bit sections (.b{data,bss}{,.*}).  */
  if (tricore_elf32_relax_bdata
      && !info->relocateable
      && (sec->_raw_size > 1)
      && (sec->_raw_size == sec->_cooked_size)
      && (!strcmp (sec->name, ".bdata")
          || !strncmp (sec->name, ".bdata.", 7)
	  || !strcmp (sec->name, ".bbss")
	  || !strncmp (sec->name, ".bbss.", 6)))
    {
      bfd_vma baddr = 0;
      int bpos = -1;
      asection *boffs;
      Elf_Internal_Shdr *strtab_hdr = NULL;
      unsigned char *strtab = NULL;
      unsigned int sec_shndx, boffs_shndx;
      unsigned int symcount;
      bfd_byte *orig_contents = NULL;
      struct elf_link_hash_entry **beg_hashes;
      struct elf_link_hash_entry **end_hashes;
      struct elf_link_hash_entry **sym_hashes;
      boolean is_bdata = (sec->name[2] == 'd');

      boffs = bfd_get_section_by_name (abfd, ".boffs");
      if (boffs == NULL)
        {
	  (*_bfd_error_handler) (_("%s: missing section \".boffs\""),
	  			 bfd_archive_filename (abfd));
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	}
      if (boffs->_raw_size != 0)
        {
	  (*_bfd_error_handler) (_("%s: section \".boffs\" has non-zero size"),
	  			 bfd_archive_filename (abfd));
	  bfd_set_error (bfd_error_bad_value);
	  return false;
	}
      sec_shndx = _bfd_elf_section_from_bfd_section (abfd, sec);
      boffs_shndx = _bfd_elf_section_from_bfd_section (abfd, boffs);

      if (is_bdata)
	{
	  /* Get the section contents.  */
	  if (elf_section_data (sec)->this_hdr.contents != NULL)
	    contents = elf_section_data (sec)->this_hdr.contents;
	  else
	    {
	      contents = (bfd_byte *) bfd_malloc (sec->_raw_size);
	      if (contents == NULL)
	        return false;

	      if (!bfd_get_section_contents (abfd, sec, contents,
	      				     (file_ptr) 0, sec->_raw_size))
	        return false;

	      /* Cache the contents, since we're going to change it.  */
              elf_section_data (sec)->this_hdr.contents = contents;
	    }

          /* Make a copy of the original data to avoid overwriting
             them while packing the bit objects.  */
          orig_contents = bfd_malloc (sec->_raw_size);
          if (orig_contents == NULL)
            return false;

          memcpy (orig_contents, contents, sec->_raw_size);
	}

      /* Read this BFD's local symbols.  */
      if (symtab_hdr->sh_info != 0)
        {
	  isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	  if (isymbuf == NULL)
	    isymbuf = bfd_elf_get_elf_syms (abfd, symtab_hdr,
	    				    symtab_hdr->sh_info, 0,
					    NULL, NULL, NULL);
	  if (isymbuf == NULL)
	    goto error_return;

	  /* Cache local syms, as we're going to change their values.  */
          symtab_hdr->contents = (unsigned char *) isymbuf;

	  strtab_hdr = &elf_tdata (abfd)->strtab_hdr;
	  strtab = strtab_hdr->contents;
	  if (strtab == NULL)
	    {
	      bfd_elf_get_str_section (abfd, symtab_hdr->sh_link);
	      strtab = strtab_hdr->contents;
	    }
	  BFD_ASSERT (strtab);
	}

      /* Walk through all local symbols in this section.  */
      if (isymbuf != NULL)
        {
	  Elf_Internal_Sym *isym, *isymend = isymbuf + symtab_hdr->sh_info;

	  for (isym = isymbuf; isym < isymend; isym++)
	    {
	      if ((isym->st_shndx == sec_shndx)
	          && (ELF_ST_TYPE (isym->st_info) == STT_OBJECT))
		{
		  Elf_Internal_Sym *isym2;
		  char *aname, *pname;
		  size_t len;

		  aname = strtab + isym->st_name;
		  len = strlen (aname);
		  if (isym->st_size != 1)
		    {
		      /* This symbol no bit.  Gripe.  */
	              (*_bfd_error_handler)
		       (_("%s: bit symbol \"%s\" has size != 1 (%ld)"),
	  		bfd_archive_filename (abfd), aname, isym->st_size);
		      bfd_set_error (bfd_error_bad_value);
		      goto error_return;
		    }

		  if (tricore_elf32_debug_relax)
		    {
		      printf ("  * %s (local), old addr = %ld",
		  	      aname, isym->st_value);
		      fflush (stdout);
		    }

		  /* Usually (in case of tricore-as, at least), we expect the
		     next local symbol after a local bit symbol to be a
		     local bit position symbol attached to section ".boffs",
		     but nevertheless we're allowing said bit position
		     symbol to be defined anywhere between the local bit
		     symbol and the end of this object file's local syms.  */
		  for (isym2 = isym + 1; isym2 < isymend; isym2++)
		    {
	      	      if (ELF_ST_TYPE (isym2->st_info) == STT_FILE)
		        {
			  /* No bit position symbol defined; bail out.  */
			  isym2 = isymend;
			  break;
			}

		      if ((isym2->st_shndx == boffs_shndx)
		          && (ELF_ST_TYPE (isym2->st_info) == STT_NOTYPE)
			  && (strlen (strtab + isym2->st_name) == (len + 4))
			  && !strncmp (aname, strtab + isym2->st_name, len)
			  && !strcmp (strtab + isym2->st_name + len, ".pos"))
			{
			  unsigned char byte = '\0';

			  pname = strtab + isym2->st_name;
			  if (isym2->st_value != 0)
			    {
			      if (tricore_elf32_debug_relax)
			        printf ("\n");

	                      (*_bfd_error_handler)
		               (_("%s: bit position symbol \"%s\" has "
			          "non-zero value %ld"),
	  		        bfd_archive_filename (abfd), pname,
				isym2->st_value);
		              bfd_set_error (bfd_error_bad_value);
			      goto error_return;
			    }

			  /* Determine next free bit address.  */
			  if (++bpos == 8)
			    {
			      bpos = 0;
			      ++baddr;
			    }

			  if (is_bdata)
			    {
			      /* Copy the bit object's original value
			         to its new bit address.  */
			      byte =
			        (orig_contents[isym->st_value] & 1) << bpos;
			      contents[baddr] &= ~(1 << bpos);
			      contents[baddr] |= byte;
			    }

			  if (tricore_elf32_debug_relax)
			    {
			      printf (".0, new addr = %ld.%d", baddr, bpos);
			      if (is_bdata)
			        printf (", val = 0x%02x", byte);
			      printf ("\n");
			    }

			  /* Adjust relocations against the old bit object.  */
                          if (!tricore_elf32_adjust_bit_relocs
			       (abfd, info, sec_shndx, isym->st_value, baddr,
			        bpos, isym2 - isymbuf))
			    return false;

			  /* Finally, set the new values.  */
			  isym->st_value = baddr;
			  isym2->st_value = bpos;
			  break;
			}
		    }
		  if (isym2 == isymend)
		    {
		      if (tricore_elf32_debug_relax)
		        printf ("\n");

	              (*_bfd_error_handler)
		       (_("%s: missing or invalid local bit position "
		          "symbol \"%s.pos\" in section \".boffs\""),
	  		bfd_archive_filename (abfd), aname);
		      bfd_set_error (bfd_error_bad_value);
		      goto error_return;
		    }
		}
	    }
	}

      /* Walk through all global symbols in this section.  */
      symcount = (symtab_hdr->sh_size / sizeof (Elf32_External_Sym)
      		  - symtab_hdr->sh_info);
      beg_hashes = elf_sym_hashes (abfd);
      end_hashes = elf_sym_hashes (abfd) + symcount;
      for (sym_hashes = beg_hashes; sym_hashes < end_hashes; sym_hashes++)
        {
	  struct elf_link_hash_entry *sym = *sym_hashes;

	  if ((sym->root.u.def.section == sec)
	      && (sym->type == STT_OBJECT)
	      && ((sym->root.type == bfd_link_hash_defined)
	          || (sym->root.type == bfd_link_hash_defweak)))
	    {
	      struct elf_link_hash_entry *sym2;
	      const char *aname = sym->root.root.string;
	      char *pname;
	      size_t len;

	      if (sym->size != 1)
		{
		  /* This symbol no bit.  Gripe.  */
	          (*_bfd_error_handler)
		   (_("%s: bit symbol \"%s\" has size != 1 (%ld)"),
	  	    bfd_archive_filename (abfd), aname, sym->size);
		  bfd_set_error (bfd_error_bad_value);
		  goto error_return;
		}
	      if (sym->elf_link_hash_flags
	          & (ELF_LINK_HASH_REF_DYNAMIC | ELF_LINK_HASH_DEF_DYNAMIC))
		{
		  (*_bfd_error_handler)
		   (_("%s: bit symbol \"%s\" defined or referenced by a "
		      "shared object"),
	  	    bfd_archive_filename (abfd), aname);
		  bfd_set_error (bfd_error_bad_value);
	          goto error_return;
		}

	      if (tricore_elf32_debug_relax)
	        {
		  printf ("  * %s (global), old addr = %ld",
	                  aname, sym->root.u.def.value);
	          fflush (stdout);
		}

	      len = strlen (aname) + 5;
	      if ((pname = bfd_malloc (len)) == NULL)
	        goto error_return;

	      sprintf (pname, "%s.pos", aname);
	      sym2 = (struct elf_link_hash_entry *)
	      	      bfd_link_hash_lookup (info->hash, pname,
		      			    false, false, false);
	      if ((sym2 == NULL)
		  || (sym2->type != STT_NOTYPE)
		  || (sym2->root.u.def.value != 0)
	          || strcmp (sym2->root.u.def.section->name, ".boffs"))
		{
	          if (tricore_elf32_debug_relax)
		    printf ("\n");

		  (*_bfd_error_handler)
		   (_("%s: missing or invalid global bit position "
		      "symbol \"%s\""),
		    bfd_archive_filename (abfd), pname);
		  bfd_set_error (bfd_error_bad_value);
		  free (pname);
		  goto error_return;
		}
	      else
		{
		  unsigned char byte = '\0';

		  free (pname);

		  /* Determine next free bit address.  */
		  if (++bpos == 8)
		    {
		      bpos = 0;
		      ++baddr;
		    }
		  if (is_bdata)
		    {
		      /* Copy the bit object's original value
		         to its new bit address.  */
		      byte = (orig_contents[sym->root.u.def.value] & 1) << bpos;
		      contents[baddr] &= ~(1 << bpos);
		      contents[baddr] |= byte;
		    }

	          if (tricore_elf32_debug_relax)
		    {
		      printf (".0, new addr = %ld.%d", baddr, bpos);
		      if (is_bdata)
		        printf (", val = 0x%02x", byte);
		      printf ("\n");
		    }

		  /* This catches cases in which the assembler (or other
		     binutil) has transformed relocations against "local
		     globals" (i.e., global symbols defined in the same
		     module as the instructions referencing them) into
		     relocations against section+offset, as it is usually
		     only done with relocs against local symbols.  */
                  if (!(tricore_elf32_adjust_bit_relocs
			(abfd, info, sec_shndx,
			 sym->root.u.def.value, baddr, bpos,
			 sym2 - *beg_hashes + symtab_hdr->sh_info)))
		    return false;

		  /* Finally, set the new values.  */
		  sym->root.u.def.value = baddr;
		  sym2->root.u.def.value = bpos;
		}
	    }
	}

      sec->_cooked_size = baddr + 1;
      if (is_bdata)
        free (orig_contents);
      *again = true;
      goto ok_return;
    }

  /* If requested, check whether all call and jump instructions can reach
     their respective target addresses.  We don't have to do anything for a
     relocateable link, or if this section doesn't contain code or relocs.  */
  if (!tricore_elf32_relax_24rel
      || info->relocateable
      || ((sec->flags & SEC_CODE) == 0)
      || ((sec->flags & SEC_RELOC) == 0)
      || (sec->reloc_count == 0))
    return true;

  if (!initialized)
    {
      int i, max = -1;
      bfd *ibfd;
      asection *section;

      /* Find the highest section ID of all input sections.  */
      for (ibfd = info->input_bfds; ibfd; ibfd = ibfd->link_next)
        for (section = ibfd->sections; section; section = section->next)
	  if (section->id > max)
	    max = section->id;

      ++max;
      if (((rela_malloced = bfd_malloc (max * sizeof (boolean))) == NULL)
          || ((rela_p = bfd_malloc (max * sizeof (struct _rela *))) == NULL)
          || ((num_rela = bfd_malloc (max * sizeof (int))) == NULL)
          || ((max_rela = bfd_malloc (max * sizeof (int))) == NULL))
        return false;

      for (i = 0; i < max; ++i)
        {
	  rela_malloced[i] = false;
	  rela_p[i] = NULL;
	  num_rela[i] = -1;
	  max_rela[i] = 12;
	}

      initialized = true;
    }

  /* Get a copy of the native relocations.  */
  internal_relocs = (_bfd_elf32_link_read_relocs
  		     (abfd, sec, (PTR) NULL, (Elf_Internal_Rela *) NULL,
		      info->keep_memory));
  if (internal_relocs == NULL)
    goto error_return;

  /* Walk through the relocs looking for call/jump targets.  */
  irelend = internal_relocs + sec->reloc_count;
  for (irel = internal_relocs; irel < irelend; irel++)
    {
      struct elf_link_hash_entry *h;
      const char *sym_name;
      bfd_vma pc, sym_val, use_offset;
      long diff, r_type;
      unsigned long r_index, insn;
      int add_bytes, doff = 0;

      r_type = ELF32_R_TYPE (irel->r_info);
      if ((r_type < 0)
       	  || (r_type < R_TRICORE_NONE)
  	  || (r_type >= R_TRICORE_max))
	goto error_return;

      if (r_type != R_TRICORE_24REL)
	continue;

      r_index = ELF32_R_SYM (irel->r_info);
      if (r_index < symtab_hdr->sh_info)
        {
	  /* This is a call/jump instruction to a local label or function.
	     The GNU/TriCore assembler will usually already have resolved
	     such local branches, so we won't see any relocs for them here,
	     but maybe this input object has been generated by another
	     vendor's assembler.  Anyway, we must assume that targets of
	     local branches can always be reached directly, so there's no
	     need for us to bother.  */
	  continue;
	}
      else
        r_index -= symtab_hdr->sh_info;

      /* Get the value of the symbol referred to by the reloc.  */
      h = elf_sym_hashes (abfd)[r_index];
      BFD_ASSERT (h != NULL);
      while ((h->root.type == bfd_link_hash_indirect)
	     || (h->root.type == bfd_link_hash_warning))
	h = (struct elf_link_hash_entry *) h->root.u.i.link;
      if ((h->root.type != bfd_link_hash_defined)
          && (h->root.type != bfd_link_hash_defweak))
	{
	  /* This appears to be a reference to an undefined symbol.  Just
	     ignore it; it'll be caught by the regular reloc processing.  */
	  continue;
	}
      sym_name = h->root.root.string;
      sym_val = h->root.u.def.value
		+ h->root.u.def.section->output_section->vma
		+ h->root.u.def.section->output_offset
		+ irel->r_addend;
      pc = irel->r_offset + sec->output_section->vma + sec->output_offset;

      /* If the target address can be reached directly or absolutely, or if
         the PC or target address are odd, we don't need to do anything, as
	 all of this will be handled by tricore_elf32_relocate_section.  */
      diff = sym_val - pc;
      if (((diff >= -16777216) && (diff <= 16777214))
          || !(sym_val & 0x0fe00000)
	  || (sym_val & 1) || (pc & 1))
	continue;

      /* Get the section contents if we haven't done so already.  */
      if (contents == NULL)
        {
	  /* Get cached copy if it exists.  */
	  if (elf_section_data (sec)->this_hdr.contents != NULL)
	    contents = elf_section_data (sec)->this_hdr.contents;
	  else
	    {
	      /* Go get them off disk.  */
	      contents = (bfd_byte *) bfd_malloc (sec->_raw_size);
	      if (contents == NULL)
	        goto error_return;

	      if (!bfd_get_section_contents (abfd, sec, contents,
	      				     (file_ptr) 0, sec->_raw_size))
		goto error_return;
	    }
	}

      /* Get the instruction.  */
      insn = bfd_get_32 (abfd, contents + irel->r_offset);
      if (((insn & 0xff) == 0x6d) || ((insn & 0xff) == 0x61))
        {
	  struct bfd_link_hash_entry *h;

          h = bfd_link_hash_lookup (info->hash, "CPUBUG14",
	  			    false, false, false);
          if ((h != (struct bfd_link_hash_entry *) NULL)
	      && (h->type == bfd_link_hash_defined)
	      && (h->u.def.value != 0))
            add_bytes = 20;
	  else
	    add_bytes = 12;
	}
      else if (((insn & 0xff) == 0x1d) || ((insn & 0xff) == 0x5d))
        add_bytes = 12;
      else
        {
	  (*_bfd_error_handler) (_("%s: Unknown call/jump insn at offset %ld"),
	  			 bfd_get_filename (abfd), irel->r_offset);
	  goto error_return;
	}

      /* See if we already created a stub for this symbol.  */
      use_offset = sec->_cooked_size;
      if (rela_p[idx] != NULL)
        {
	  int i;

	  for (i = 0; i <= num_rela[idx]; ++i)
	    if ((rela_p[idx][i].symoff == (r_index + symtab_hdr->sh_info))
	        && (rela_p[idx][i].addend == irel->r_addend)
		&& (rela_p[idx][i].size == add_bytes))
	      {
		/* Use the existing stub.  */
		use_offset = rela_p[idx][i].offset;
      		diff = use_offset - irel->r_offset;
		if (add_bytes == 20)
      		  {
		    diff -= 8;
		    use_offset -= 8;
		  }
	        if (diff > 16777214)
		  goto error_return;

		diff >>= 1;
                insn |= ((diff & 0xffff) << 16);
                insn |= ((diff & 0xff0000) >> 8);
                bfd_put_32 (abfd, insn, contents + irel->r_offset);
      		irel->r_info = R_TRICORE_NONE;
		add_bytes = 0;
		break;
	      }
	}

      if (tricore_elf32_debug_relax)
	{
	  printf ("  # 0x%08lx[+%ld]: \"%s %s",
	  	  pc, irel->r_offset,
		  ((insn & 0xff) == 0x6d) ? "call"
		   : ((insn & 0xff) == 0x61) ? "fcall"
		      : ((insn & 0xff) == 0x1d) ? "j" : "jl",
      	          sym_name);
	  if (irel->r_addend != 0)
	    printf ("+%ld", irel->r_addend);
	  printf ("\" @0x%08lx[+%ld,%d]->0x%08lx\n", 
		  use_offset + sec->output_section->vma + sec->output_offset,
		  use_offset, add_bytes, sym_val);
	}

      if (add_bytes == 0)
        continue;
      else if ((sec->_cooked_size - irel->r_offset) > 16777214)
        goto error_return;

      /* Make room for the additional instructions.  */
      contents = bfd_realloc (contents, sec->_cooked_size + add_bytes);
      if (contents == NULL)
        goto error_return;

      /* Read this BFD's local symbols if we haven't done so already.  */
      if ((isymbuf == NULL) && (symtab_hdr->sh_info != 0))
        {
	  isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	  if (isymbuf == NULL)
	    isymbuf = bfd_elf_get_elf_syms (abfd, symtab_hdr,
	    				    symtab_hdr->sh_info, 0,
					    NULL, NULL, NULL);
	  if (isymbuf == NULL)
	    goto error_return;
	}

      /* For simplicity of coding, we are going to modify the section
         contents, the section relocs, and the BFD symbol table.  We
	 must tell the rest of the code not to free up this information.  */
      elf_section_data (sec)->relocs = internal_relocs;
      elf_section_data (sec)->this_hdr.contents = contents;
      symtab_hdr->contents = (unsigned char *) isymbuf;

      /* Modify the call or jump insn to branch to the beginning of
         the additional code.  */
      diff = (sec->_cooked_size - irel->r_offset) >> 1;
      insn |= ((diff & 0xffff) << 16);
      insn |= ((diff & 0xff0000) >> 8);
      bfd_put_32 (abfd, insn, contents + irel->r_offset);

      /* Nullify the insn's reloc.  */
      irel->r_info = R_TRICORE_NONE;

      /* Emit the additonal code.  */
      if (add_bytes == 20)
        {
	  /* This is called by a [f]call instruction, and we need a
	     "dsync; nop; nop" sequence as a workaround for a hw bug.  */
	  bfd_put_32 (abfd, 0x0480000d, contents + sec->_cooked_size);
	  bfd_put_32 (abfd, 0x00000000, contents + sec->_cooked_size + 4);
	  doff = 8;
	}
      /* This is "movh.a %a2,hi:tgt; lea %a2,[%a2]lo:tgt; nop; ji %a2".  */
      bfd_put_32 (abfd, 0x20000091, contents + sec->_cooked_size + doff);
      bfd_put_32 (abfd, 0x000022d9, contents + sec->_cooked_size + doff + 4);
      bfd_put_32 (abfd, 0x02dc0000, contents + sec->_cooked_size + doff + 8);

      /* Remember the new relocs.  */
      if (num_rela[idx] == -1)
        {
	  if ((rela_p[idx] = (struct _rela *)
	        bfd_malloc (max_rela[idx] * sizeof (struct _rela))) == NULL)
	    goto error_return;

	  num_rela[idx] = 0;
	}
      else if (++num_rela[idx] == max_rela[idx])
        {
	  max_rela[idx] *= 2;
	  if ((rela_p[idx] = (struct _rela *)
	  	bfd_realloc (rela_p[idx],
			     max_rela[idx] * sizeof (struct _rela))) == NULL)
	    goto error_return;
	}
      ++now_rela;
      rela_p[idx][num_rela[idx]].offset = sec->_cooked_size + doff;
      rela_p[idx][num_rela[idx]].addend = irel->r_addend;
      rela_p[idx][num_rela[idx]].symoff = r_index + symtab_hdr->sh_info;
      rela_p[idx][num_rela[idx]].size = add_bytes;

      /* Set the new section size.  */
      sec->_cooked_size += add_bytes;

      /* We've changed something.  */
      *again = true;
    }

  /* Add new relocs, if required.  */
  if (now_rela > 0)
    {
      int i;

      if (info->keep_memory && !rela_malloced[idx])
        {
	  /* In this case, the memory for the relocs has been
	     bfd_alloc()ed, so we can't use bfd_realloc() to make
	     room for the additional relocs, and we can also not
	     free(internal_relocs).  */
	  if ((irel = bfd_malloc ((sec->reloc_count + 2 * now_rela)
	  			    * sizeof (Elf_Internal_Rela))) == NULL)
	    goto error_return;

	  memcpy (irel, internal_relocs,
	  	  sec->reloc_count * sizeof (Elf_Internal_Rela));
	  internal_relocs = irel;
	  rela_malloced[idx] = true;
	}
      else if ((internal_relocs =
                  bfd_realloc (internal_relocs,
	    		       (sec->reloc_count + 2 * now_rela)
			         * sizeof (Elf_Internal_Rela))) == NULL)
	goto error_return;

      elf_section_data (sec)->relocs = internal_relocs;
      irel = internal_relocs + sec->reloc_count;
      for (i = num_rela[idx] - now_rela + 1; i <= num_rela[idx]; ++i, ++irel)
        {
	  irel->r_addend = rela_p[idx][i].addend;
	  irel->r_offset = rela_p[idx][i].offset;
	  irel->r_info = ELF32_R_INFO (rela_p[idx][i].symoff, R_TRICORE_HIADJ);
	  ++irel;
	  irel->r_addend = rela_p[idx][i].addend;
	  irel->r_offset = rela_p[idx][i].offset + 4;
	  irel->r_info = ELF32_R_INFO (rela_p[idx][i].symoff, R_TRICORE_LO2);
	}

      /* Set the new relocation count.  */
      sec->reloc_count += 2 * now_rela;
    }

  /* Free or cache the memory we've allocated for relocs, local symbols,
     etc.  */
  if ((internal_relocs != NULL)
      && elf_section_data (sec)->relocs != internal_relocs)
    free (internal_relocs);

ok_return:
  if ((isymbuf != NULL)
      && (symtab_hdr->contents != (unsigned char *) isymbuf))
    {
      if (!info->keep_memory)
        free (isymbuf);
      else
        symtab_hdr->contents = (unsigned char *) isymbuf;
    }

  if ((contents != NULL)
      && (elf_section_data (sec)->this_hdr.contents != contents))
    {
      if (!info->keep_memory)
        free (contents);
      else
        elf_section_data (sec)->this_hdr.contents = contents;
    }

  return true;

error_return:
  if ((internal_relocs != NULL)
      && (elf_section_data (sec)->relocs != internal_relocs))
    free (internal_relocs);

  if ((isymbuf != NULL)
      && (symtab_hdr->contents != (unsigned char *) isymbuf))
    free (isymbuf);

  if ((contents != NULL)
      && (elf_section_data (sec)->this_hdr.contents != contents))
    free (contents);

  return false;
}

/* Now #define all necessary stuff to describe this target.  */

#define USE_RELA			1
#define ELF_ARCH			bfd_arch_tricore
#define ELF_MACHINE_CODE		EM_TRICORE
#define ELF_MAXPAGESIZE			0x4000
#define TARGET_LITTLE_SYM		bfd_elf32_tricore_vec
#define TARGET_LITTLE_NAME		"elf32-tricore"
#define bfd_elf32_bfd_relax_section	tricore_elf32_relax_section
#define bfd_elf32_bfd_reloc_type_lookup	tricore_elf32_reloc_type_lookup
#define bfd_elf32_object_p		tricore_elf32_object_p
#define bfd_elf32_bfd_merge_private_bfd_data \
					tricore_elf32_merge_private_bfd_data
#define bfd_elf32_bfd_copy_private_bfd_data \
					tricore_elf32_copy_private_bfd_data
#define bfd_elf32_bfd_set_private_flags tricore_elf32_set_private_flags
#define elf_info_to_howto		tricore_elf32_info_to_howto
#define elf_info_to_howto_rel		0
#define elf_backend_section_flags	tricore_elf32_section_flags
#define elf_backend_relocate_section	tricore_elf32_relocate_section
#define elf_backend_fake_sections	tricore_elf32_fake_sections

#if 1
#define bfd_elf32_bfd_final_link	_bfd_elf32_gc_common_final_link
#define elf_backend_gc_mark_hook        tricore_elf32_gc_mark_hook
#define elf_backend_gc_sweep_hook       tricore_elf32_gc_sweep_hook
#define elf_backend_reloc_type_class    tricore_elf32_reloc_type_class
#define elf_backend_check_relocs	tricore_elf32_check_relocs
#define elf_backend_adjust_dynamic_symbol \
					tricore_elf32_adjust_dynamic_symbol
#define elf_backend_finish_dynamic_symbol \
					tricore_elf32_finish_dynamic_symbol
#define elf_backend_create_dynamic_sections \
					_bfd_elf_create_dynamic_sections
#define elf_backend_size_dynamic_sections \
					tricore_elf32_size_dynamic_sections
#define elf_backend_finish_dynamic_sections \
					tricore_elf32_finish_dynamic_sections

#define elf_backend_can_gc_sections	1
#define elf_backend_can_refcount	1
#define elf_backend_plt_readonly	0
#define elf_backend_want_plt_sym	0
#define elf_backend_want_dynbss		1
#define elf_backend_got_header_size	4
#define elf_backend_plt_header_size	(PLT_RESERVED_SLOTS * PLT_ENTRY_SIZE)
#endif

#include "elf32-target.h"
