#
# Emulation parameters for Linux/TriCore applications.
# Copyright (C) 1998-2003 Free Software Foundation, Inc.
# Contributed by Michael Schumacher (mike@hightec-rt.com).
#
SCRIPT_NAME=elf
OUTPUT_FORMAT="elf32-tricore"
ARCH=tricore
MACHINE=
TEXT_START_ADDR=0x01000000
NONPAGED_TEXT_START_ADDR=0
DATA_ALIGNMENT="((. + 7) & -8)"
MAXPAGESIZE=0x4000
GENERATE_SHLIB_SCRIPT=yes
TEMPLATE_NAME=elf32
EXTRA_EM_FILE=tricoreelf
DATA_START_SYMBOLS='__data_start = . ;'
OTHER_BSS_SYMBOLS='__bss_start__ = .;'
OTHER_BSS_END_SYMBOLS='_bss_end__ = . ; __bss_end__ = . ; __end__ = . ;'
DATA_PLT=
ENTRY=_start
