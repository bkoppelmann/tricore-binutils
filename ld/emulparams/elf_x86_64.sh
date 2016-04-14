SCRIPT_NAME=elf
ELFSIZE=64
OUTPUT_FORMAT="elf64-x86-64"
TEXT_START_ADDR=0x400000
MAXPAGESIZE=0x100000
COMMONPAGESIZE=0x1000
NONPAGED_TEXT_START_ADDR=0x400000
ARCH="i386:x86-64"
MACHINE=
NOP=0x90909090
TEMPLATE_NAME=elf32
GENERATE_SHLIB_SCRIPT=yes
NO_SMALL_DATA=yes

if [ "x${host}" = "x${target}" ]; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      # Native, and default or emulation requesting LIB_PATH.

      # Linux modify the default library search path to first include
      # a 64-bit specific directory.
      case "$target" in
        x86_64*-linux*)
          suffix=64 ;;
      esac

      if [ -n "${suffix}" ]; then

	LIB_PATH=/lib${suffix}:/lib
	LIB_PATH=${LIB_PATH}:/usr/lib${suffix}:/usr/lib
	if [ -n "${NATIVE_LIB_DIRS}" ]; then
	  LIB_PATH=${LIB_PATH}:`echo ${NATIVE_LIB_DIRS} | sed s_:_${suffix}:_g`${suffix}:${NATIVE_LIB_DIRS}
	fi
	if [ "${libdir}" != /usr/lib ]; then
	  LIB_PATH=${LIB_PATH}:${libdir}${suffix}:${libdir}
	fi
	if [ "${libdir}" != /usr/local/lib ]; then
	  LIB_PATH=${LIB_PATH}:/usr/local/lib${suffix}:/usr/local/lib
	fi

      fi
    ;;
  esac
fi
