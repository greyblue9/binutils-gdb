/* readelf.c -- display contents of an ELF format file
   Copyright (C) 1998 Free Software Foundation, Inc.

   Originally developed by Eric Youngdale <eric@andante.jic.com>
   Modifications by Nick Clifton <nickc@cygnus.com>

   This file is part of GNU Binutils.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */


#include <assert.h>
#include <sys/stat.h>
#include <stdio.h>
#include <time.h>

#include "bfd.h"

#include "elf/common.h"
#include "elf/external.h"
#include "elf/internal.h"

/* The following headers use the elf/reloc-macros.h file to
   automatically generate relocation recognition functions
   such as elf_mips_reloc_type()  */

#define RELOC_MACROS_GEN_FUNC

#include "elf/i386.h"
#include "elf/v850.h"
#include "elf/ppc.h"
#include "elf/mips.h"
#include "elf/alpha.h"
#include "elf/arm.h"
#include "elf/m68k.h"
#include "elf/sparc.h"
#include "elf/m32r.h"
#include "elf/d10v.h"
#include "elf/d30v.h"
#include "elf/sh.h"
#include "elf/mn10200.h"
#include "elf/mn10300.h"
#include "elf/hppa.h"
#include "elf/arc.h"

#include "bucomm.h"
#include "getopt.h"

#ifdef ANSI_PROTOTYPES
#include <stdarg.h>
#else
#include <varargs.h>
#endif

char *          	program_name = "readelf";
unsigned int    	dynamic_addr;
unsigned int    	dynamic_size;
unsigned int    	rela_addr;
unsigned int    	rela_size;
char *          	dynamic_strings;
char *			string_table;
Elf_Internal_Sym * 	dynamic_symbols;
Elf_Internal_Syminfo *	dynamic_syminfo;
unsigned long int	dynamic_syminfo_offset;
unsigned int		dynamic_syminfo_nent;
char            	program_interpreter [64];
int             	dynamic_info[DT_JMPREL + 1];
int             	version_info[16];
int             	loadaddr = 0;
Elf_Internal_Ehdr       elf_header;
Elf_Internal_Shdr *     section_headers;
Elf_Internal_Dyn *      dynamic_segment;
int 			show_name;
int 			do_dynamic;
int 			do_syms;
int 			do_reloc;
int 			do_sections;
int 			do_segments;
int 			do_using_dynamic;
int 			do_header;
int 			do_dump;
int 			do_version;
int			do_histogram;

static unsigned long int (* byte_get) PARAMS ((unsigned char *, int));

#define NUM_DUMP_SECTS	100
char 			dump_sects [NUM_DUMP_SECTS];

#define HEX_DUMP	1
#define DISASS_DUMP	2

/* Forward declarations for dumb compilers.  */
static const char * get_mips_dynamic_type PARAMS ((unsigned long type));
static const char * get_dynamic_type PARAMS ((unsigned long type));
static int    dump_relocations
  PARAMS ((FILE *, unsigned long, unsigned long, Elf_Internal_Sym *, char *));
static char * get_file_type     PARAMS ((unsigned e_type));
static char * get_machine_name  PARAMS ((unsigned e_machine));
static char * get_machine_data  PARAMS ((unsigned e_data));
static char * get_machine_flags PARAMS ((unsigned, unsigned e_machine));
static const char * get_mips_segment_type PARAMS ((unsigned long type));
static const char * get_segment_type  PARAMS ((unsigned long p_type));
static const char * get_mips_section_type_name PARAMS ((unsigned int sh_type));
static const char * get_section_type_name PARAMS ((unsigned int sh_type));
static char * get_symbol_binding PARAMS ((unsigned int binding));
static char * get_symbol_type    PARAMS ((unsigned int type));
static void   usage PARAMS ((void));
static void   parse_args PARAMS ((int argc, char ** argv));
static int    process_file_header PARAMS ((void));
static int    process_program_headers PARAMS ((FILE *));
static int    process_section_headers PARAMS ((FILE *));
static void   dynamic_segment_mips_val PARAMS ((Elf_Internal_Dyn *entry));
static int    process_dynamic_segment PARAMS ((FILE *));
static int    process_symbol_table PARAMS ((FILE *));
static int    process_section_contents PARAMS ((FILE *));
static void   process_file PARAMS ((char * file_name));
static int    process_relocs PARAMS ((FILE *));
static int    process_version_sections PARAMS ((FILE *));
static char * get_ver_flags PARAMS ((unsigned int flags));
static char * get_symbol_index_type PARAMS ((unsigned int type));
static int    get_section_headers PARAMS ((FILE * file));
static int    get_file_header PARAMS ((FILE * file));
static Elf_Internal_Sym * get_elf_symbols
  PARAMS ((FILE * file, unsigned long offset, unsigned long number));
static int *  get_dynamic_data PARAMS ((FILE * file, unsigned int number));

typedef int Elf32_Word;

#define SECTION_NAME(X) 	(string_table + (X)->sh_name)

#define DT_VERSIONTAGIDX(tag)	(DT_VERNEEDNUM - (tag))	/* Reverse order! */

#define BYTE_GET(field) 	byte_get (field, sizeof (field))

#define NUM_ELEM(array) 	(sizeof (array) / sizeof ((array)[0]))

#define GET_DATA_ALLOC(offset, size, var, type, reason)			\
  if (fseek (file, offset, SEEK_SET))					\
    {									\
      error (_("Unable to seek to start of %s at %x\n"), reason, offset); \
      return 0;								\
    }									\
									\
  var = (type) malloc (size);						\
									\
  if (var == NULL)							\
    {									\
      error (_("Out of memory allocating %d bytes for %s\n"), size, reason); \
      return 0;								\
    } 									\
 									\
  if (fread (var, size, 1, file) != 1) 					\
    { 									\
      error (_("Unable to read in %d bytes of %s\n"), size, reason); 	\
      free (var); 							\
      var = NULL;							\
      return 0; 							\
    }


#define GET_DATA(offset, var, reason) 					\
  if (fseek (file, offset, SEEK_SET))					\
    { 									\
      error (_("Unable to seek to %x for %s\n"), offset, reason);	\
      return 0;								\
    }									\
  else if (fread (& var, sizeof (var), 1, file) != 1)			\
    {									\
      error (_("Unable to read data at %x for %s\n"), offset, reason);	\
      return 0;								\
    }

#ifdef ANSI_PROTOTYPES
static void
error (const char * message, ...)
{
  va_list args;

  fprintf (stderr, _("%s: Error: "), program_name);
  va_start (args, message);
  vfprintf (stderr, message, args);
  va_end (args);
  return;
}

static void
warn (const char * message, ...)
{
  va_list args;

  fprintf (stderr, _("%s: Warning: "), program_name);
  va_start (args, message);
  vfprintf (stderr, message, args);
  va_end (args);
  return;
}
#else
static void
error (va_alist)
     va_dcl
{
  char * message;
  va_list args;

  fprintf (stderr, _("%s: Error: "), program_name);
  va_start (args);
  message = va_arg (args, char *);
  vfprintf (stderr, message, args);
  va_end (args);
  return;
}

static void
warn (va_alist)
     va_dcl
{
  char * message;
  va_list args;

  fprintf (stderr, _("%s: Warning: "), program_name);
  va_start (args);
  message = va_arg (args, char *);
  vfprintf (stderr, message, args);
  va_end (args);
  return;
}
#endif

static unsigned long int
byte_get_little_endian (field, size)
     unsigned char * field;
     int             size;
{
  switch (size)
    {
    case 1:
      return * field;

    case 2:
      return  ((unsigned int) (field [0]))
	|    (((unsigned int) (field [1])) << 8);

    case 4:
      return  ((unsigned long) (field [0]))
	|    (((unsigned long) (field [1])) << 8)
	|    (((unsigned long) (field [2])) << 16)
	|    (((unsigned long) (field [3])) << 24);

    default:
      error (_("Unhandled data length: %d\n"), size);
      abort();
    }
}

static unsigned long int
byte_get_big_endian (field, size)
     unsigned char * field;
     int             size;
{
  switch (size)
    {
    case 1:
      return * field;

    case 2:
      return ((unsigned int) (field [1])) | (((int) (field [0])) << 8);

    case 4:
      return ((unsigned long) (field [3]))
	|   (((unsigned long) (field [2])) << 8)
	|   (((unsigned long) (field [1])) << 16)
	|   (((unsigned long) (field [0])) << 24);

    default:
      error (_("Unhandled data length: %d\n"), size);
      abort();
    }
}


/* Display the contents of the relocation data
   found at the specified offset.  */
static int
dump_relocations (file, rel_offset, rel_size, symtab, strtab)
     FILE *                 file;
     unsigned long          rel_offset;
     unsigned long          rel_size;
     Elf_Internal_Sym *     symtab;
     char *                 strtab;
{
  unsigned int        i;
  int                 is_rela;
  Elf_Internal_Rel *  rels;
  Elf_Internal_Rela * relas;


  /* Compute number of relocations and read them in.  */
  switch (elf_header.e_machine)
    {
    case EM_386:
    case EM_486:
    case EM_CYGNUS_M32R:
    case EM_CYGNUS_D10V:
    case EM_MIPS:
    case EM_MIPS_RS4_BE:
      {
	Elf32_External_Rel * erels;

	GET_DATA_ALLOC (rel_offset, rel_size, erels,
			Elf32_External_Rel *, "relocs");

	rel_size = rel_size / sizeof (Elf32_External_Rel);

	rels = (Elf_Internal_Rel *) malloc (rel_size *
					    sizeof (Elf_Internal_Rel));

	for (i = 0; i < rel_size; i++)
	  {
	    rels[i].r_offset = BYTE_GET (erels[i].r_offset);
	    rels[i].r_info   = BYTE_GET (erels[i].r_info);
	  }

	free (erels);

	is_rela = 0;
	relas   = (Elf_Internal_Rela *) rels;
      }
    break;

    case EM_ARM:
    case EM_68K:
    case EM_SPARC:
    case EM_PPC:
    case EM_CYGNUS_V850:
    case EM_CYGNUS_D30V:
    case EM_CYGNUS_MN10200:
    case EM_CYGNUS_MN10300:
    case EM_SH:
    case EM_ALPHA:
      {
	Elf32_External_Rela * erelas;

	GET_DATA_ALLOC (rel_offset, rel_size, erelas,
			Elf32_External_Rela *, "relocs");

	rel_size = rel_size / sizeof (Elf32_External_Rela);

	relas = (Elf_Internal_Rela *) malloc (rel_size *
					      sizeof (Elf_Internal_Rela));

	for (i = 0; i < rel_size; i++)
	  {
	    relas[i].r_offset = BYTE_GET (erelas[i].r_offset);
	    relas[i].r_info   = BYTE_GET (erelas[i].r_info);
	    relas[i].r_addend = BYTE_GET (erelas[i].r_addend);
	  }

	free (erelas);

	is_rela = 1;
	rels    = (Elf_Internal_Rel *) relas;
      }
    break;

    default:
      warn (_("Don't know about relocations on this machine architecture\n"));
      return 0;
    }

  if (is_rela)
    printf
      (_("  Offset    Value Type            Symbol's Value  Symbol's Name          Addend\n"));
  else
    printf
      (_("  Offset    Value Type            Symbol's Value  Symbol's Name\n"));

  for (i = 0; i < rel_size; i++)
    {
      const char *  rtype;
      unsigned long offset;
      unsigned long info;
      int           symtab_index;

      if (is_rela)
	{
	  offset = relas [i].r_offset;
	  info   = relas [i].r_info;
	}
      else
	{
	  offset = rels [i].r_offset;
	  info   = rels [i].r_info;
	}

      printf ("  %8.8lx  %5.5lx ", offset, info);

      switch (elf_header.e_machine)
	{
	default:
	  rtype = NULL;
	  break;

	case EM_CYGNUS_M32R:
	  rtype = elf_m32r_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_386:
	case EM_486:
	  rtype = elf_i386_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_68K:
	  rtype = elf_m68k_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_SPARC:
	  rtype = elf_sparc_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_CYGNUS_V850:
	  rtype = v850_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_CYGNUS_D10V:
	  rtype = elf_d10v_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_CYGNUS_D30V:
	  rtype = elf_d30v_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_SH:
	  rtype = elf_sh_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_CYGNUS_MN10300:
	  rtype = elf_mn10300_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_CYGNUS_MN10200:
	  rtype = elf_mn10200_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_PPC:
	  rtype = elf_ppc_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_MIPS:
	case EM_MIPS_RS4_BE:
	  rtype = elf_mips_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_ALPHA:
	  rtype = elf_alpha_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_ARM:
	  rtype = elf_arm_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_CYGNUS_ARC:
	  rtype = elf_arc_reloc_type (ELF32_R_TYPE (info));
	  break;

	case EM_PARISC:
	  rtype = elf32_hppa_reloc_type (ELF32_R_TYPE (info));
	  break;
	}

      if (rtype == NULL)
	printf (_("unrecognised: %-7x"), ELF32_R_TYPE (info));
      else
	printf ("%-21.21s", rtype);

      symtab_index = ELF32_R_SYM (info);

      if (symtab_index && symtab != NULL)
	{
	  Elf_Internal_Sym * psym;

	  psym = symtab + symtab_index;

	  printf (" %08lx  ", (unsigned long) psym->st_value);

	  if (psym->st_name == 0)
	    printf ("%-25.25s",
		    SECTION_NAME (section_headers + psym->st_shndx));
	  else if (strtab == NULL)
	    printf (_("<string table index %3d>"), psym->st_name);
	  else
	    printf ("%-25.25s", strtab + psym->st_name);

	  if (is_rela)
	    printf (" + %lx", (unsigned long) relas [i].r_addend);
	}

      putchar ('\n');
    }

  free (relas);

  return 1;
}

static const char *
get_mips_dynamic_type (type)
     unsigned long type;
{
  switch (type)
    {
    case DT_MIPS_RLD_VERSION: return "MIPS_RLD_VERSION";
    case DT_MIPS_TIME_STAMP: return "MIPS_TIME_STAMP";
    case DT_MIPS_ICHECKSUM: return "MIPS_ICHECKSUM";
    case DT_MIPS_IVERSION: return "MIPS_IVERSION";
    case DT_MIPS_FLAGS: return "MIPS_FLAGS";
    case DT_MIPS_BASE_ADDRESS: return "MIPS_BASE_ADDRESS";
    case DT_MIPS_MSYM: return "MIPS_MSYM";
    case DT_MIPS_CONFLICT: return "MIPS_CONFLICT";
    case DT_MIPS_LIBLIST: return "MIPS_LIBLIST";
    case DT_MIPS_LOCAL_GOTNO: return "MIPS_LOCAL_GOTNO";
    case DT_MIPS_CONFLICTNO: return "MIPS_CONFLICTNO";
    case DT_MIPS_LIBLISTNO: return "MIPS_LIBLISTNO";
    case DT_MIPS_SYMTABNO: return "MIPS_SYMTABNO";
    case DT_MIPS_UNREFEXTNO: return "MIPS_UNREFEXTNO";
    case DT_MIPS_GOTSYM: return "MIPS_GOTSYM";
    case DT_MIPS_HIPAGENO: return "MIPS_HIPAGENO";
    case DT_MIPS_RLD_MAP: return "MIPS_RLD_MAP";
    case DT_MIPS_DELTA_CLASS: return "MIPS_DELTA_CLASS";
    case DT_MIPS_DELTA_CLASS_NO: return "MIPS_DELTA_CLASS_NO";
    case DT_MIPS_DELTA_INSTANCE: return "MIPS_DELTA_INSTANCE";
    case DT_MIPS_DELTA_INSTANCE_NO: return "MIPS_DELTA_INSTANCE_NO";
    case DT_MIPS_DELTA_RELOC: return "MIPS_DELTA_RELOC";
    case DT_MIPS_DELTA_RELOC_NO: return "MIPS_DELTA_RELOC_NO";
    case DT_MIPS_DELTA_SYM: return "MIPS_DELTA_SYM";
    case DT_MIPS_DELTA_SYM_NO: return "MIPS_DELTA_SYM_NO";
    case DT_MIPS_DELTA_CLASSSYM: return "MIPS_DELTA_CLASSSYM";
    case DT_MIPS_DELTA_CLASSSYM_NO: return "MIPS_DELTA_CLASSSYM_NO";
    case DT_MIPS_CXX_FLAGS: return "MIPS_CXX_FLAGS";
    case DT_MIPS_PIXIE_INIT: return "MIPS_PIXIE_INIT";
    case DT_MIPS_SYMBOL_LIB: return "MIPS_SYMBOL_LIB";
    case DT_MIPS_LOCALPAGE_GOTIDX: return "MIPS_LOCALPAGE_GOTIDX";
    case DT_MIPS_LOCAL_GOTIDX: return "MIPS_LOCAL_GOTIDX";
    case DT_MIPS_HIDDEN_GOTIDX: return "MIPS_HIDDEN_GOTIDX";
    case DT_MIPS_PROTECTED_GOTIDX: return "MIPS_PROTECTED_GOTIDX";
    case DT_MIPS_OPTIONS: return "MIPS_OPTIONS";
    case DT_MIPS_INTERFACE: return "MIPS_INTERFACE";
    case DT_MIPS_DYNSTR_ALIGN: return "MIPS_DYNSTR_ALIGN";
    case DT_MIPS_INTERFACE_SIZE: return "MIPS_INTERFACE_SIZE";
    case DT_MIPS_RLD_TEXT_RESOLVE_ADDR: return "MIPS_RLD_TEXT_RESOLVE_ADDR";
    case DT_MIPS_PERF_SUFFIX: return "MIPS_PERF_SUFFIX";
    case DT_MIPS_COMPACT_SIZE: return "MIPS_COMPACT_SIZE";
    case DT_MIPS_GP_VALUE: return "MIPS_GP_VALUE";
    case DT_MIPS_AUX_DYNAMIC: return "MIPS_AUX_DYNAMIC";
    default:
      return NULL;
    }
}

static const char *
get_dynamic_type (type)
     unsigned long type;
{
  static char buff [32];

  switch (type)
    {
    case DT_NULL:	return "NULL";
    case DT_NEEDED:	return "NEEDED";
    case DT_PLTRELSZ:	return "PLTRELSZ";
    case DT_PLTGOT:	return "PLTGOT";
    case DT_HASH:	return "HASH";
    case DT_STRTAB:	return "STRTAB";
    case DT_SYMTAB:	return "SYMTAB";
    case DT_RELA:	return "RELA";
    case DT_RELASZ:	return "RELASZ";
    case DT_RELAENT:	return "RELAENT";
    case DT_STRSZ:	return "STRSZ";
    case DT_SYMENT:	return "SYMENT";
    case DT_INIT:	return "INIT";
    case DT_FINI:	return "FINI";
    case DT_SONAME:	return "SONAME";
    case DT_RPATH:	return "RPATH";
    case DT_SYMBOLIC:	return "SYMBOLIC";
    case DT_REL:	return "REL";
    case DT_RELSZ:	return "RELSZ";
    case DT_RELENT:	return "RELENT";
    case DT_PLTREL:	return "PLTREL";
    case DT_DEBUG:	return "DEBUG";
    case DT_TEXTREL:	return "TEXTREL";
    case DT_JMPREL:	return "JMPREL";
    case DT_VERDEF:	return "VERDEF";
    case DT_VERDEFNUM:	return "VERDEFNUM";
    case DT_VERNEED:	return "VERNEED";
    case DT_VERNEEDNUM:	return "VERNEEDNUM";
    case DT_VERSYM:	return "VERSYN";
    case DT_AUXILIARY:	return "AUXILARY";
    case DT_FILTER:	return "FILTER";
    case DT_POSFLAG_1:	return "POSFLAG_1";
    case DT_SYMINSZ:	return "SYMINSZ";
    case DT_SYMINENT:	return "SYMINENT";
    case DT_SYMINFO:	return "SYMINFO";
    case DT_RELACOUNT:	return "RELACOUNT";
    case DT_RELCOUNT:	return "RELCOUNT";
    case DT_FLAGS_1:	return "FLAGS_1";
    case DT_USED:	return "USED";

    default:
      if ((type >= DT_LOPROC) && (type <= DT_HIPROC))
	{
	  const char *result = NULL;
	  switch (elf_header.e_machine)
	    {
	    case EM_MIPS:
	    case EM_MIPS_RS4_BE:
	      result = get_mips_dynamic_type (type);
	    }

	  if (result == NULL)
	    {
	      sprintf (buff, _("Processor Specific"), type);
	      result = buff;
	    }
	  return result;
	}
      else
	sprintf (buff, _("<unknown>: %x"), type);
      return buff;
    }
}

static char *
get_file_type (e_type)
     unsigned e_type;
{
  static char buff [32];

  switch (e_type)
    {
    case ET_NONE:	return _("NONE (None)");
    case ET_REL:	return _("REL (Relocatable file)");
    case ET_EXEC:       return _("EXEC (Executable file)");
    case ET_DYN:        return _("DYN (Shared object file)");
    case ET_CORE:       return _("CORE (Core file)");

    default:
      if ((e_type >= ET_LOPROC) && (e_type <= ET_HIPROC))
	sprintf (buff, _("Processor Specific: (%x)"), e_type);
      else
	sprintf (buff, _("<unknown>: %x"), e_type);
      return buff;
    }
}

static char *
get_machine_name (e_machine)
     unsigned e_machine;
{
  static char buff [32];

  switch (e_machine)
    {
    case EM_NONE:        	return _("None");
    case EM_M32:         	return "WE32100";
    case EM_SPARC:       	return "Sparc";
    case EM_386:         	return "Intel 80386";
    case EM_68K:         	return "MC68000";
    case EM_88K:         	return "MC88000";
    case EM_486:         	return "Intel 80486";
    case EM_860:         	return "Intel 80860";
    case EM_MIPS:        	return "MIPS R3000 big-endian";
    case EM_S370:        	return "Amdahl";
    case EM_MIPS_RS4_BE: 	return "MIPS R4000 big-endian";
    case EM_OLD_SPARCV9:	return "Sparc v9 (old)";
    case EM_PARISC:      	return "HPPA";
    case EM_PPC_OLD:		return "Power PC (old)";
    case EM_SPARC32PLUS: 	return "Sparc v8+" ;
    case EM_960:         	return "Intel 90860";
    case EM_PPC:         	return "PowerPC";
    case EM_V800:         	return "NEC V800";
    case EM_FR20:         	return "Fujitsu FR20";
    case EM_RH32:         	return "TRW RH32";
    case EM_MMA:         	return "Fujitsu MMA";
    case EM_ARM:	 	return "ARM";
    case EM_OLD_ALPHA:	 	return "Digital Alpha (old)";
    case EM_SH:		 	return "Hitachi SH";
    case EM_SPARCV9:     	return "Sparc v9";
    case EM_ALPHA:       	return "Alpha";
    case EM_CYGNUS_D10V:        return "d10v";
    case EM_CYGNUS_D30V:        return "d30v";
    case EM_CYGNUS_ARC:		return "Arc";
    case EM_CYGNUS_M32R:	return "M32r";
    case EM_CYGNUS_V850:	return "v850";
    case EM_CYGNUS_MN10300:	return "mn10300";
    case EM_CYGNUS_MN10200:	return "mn10200";

    default:
      sprintf (buff, _("<unknown>: %x"), e_machine);
      return buff;
    }
}

static char *
get_machine_flags (e_flags, e_machine)
     unsigned e_flags;
     unsigned e_machine;
{
  static char buf [1024];

  buf[0] = '\0';
  if (e_flags)
    {
      switch (e_machine)
	{
	default:
	  break;

	case EM_PPC:
	  if (e_flags & EF_PPC_EMB)
	    strcat (buf, ", emb");

	  if (e_flags & EF_PPC_RELOCATABLE)
	    strcat (buf, ", relocatable");

	  if (e_flags & EF_PPC_RELOCATABLE_LIB)
	    strcat (buf, ", relocatable-lib");
	  break;

	case EM_CYGNUS_M32R:
	  if ((e_flags & EF_M32R_ARCH) == E_M32R_ARCH)
	    strcat (buf, ", m32r");

	  /* start-sanitize-m32rx */
#ifdef E_M32RX_ARCH
	  if ((e_flags & EF_M32R_ARCH) == E_M32RX_ARCH)
	    strcat (buf, ", m32rx");
#endif
	  /* end-sanitize-m32rx */
	  break;

	case EM_MIPS:
	case EM_MIPS_RS4_BE:
	  if (e_flags & EF_MIPS_NOREORDER)
	    strcat (buf, ", noreorder");

	  if (e_flags & EF_MIPS_PIC)
	    strcat (buf, ", pic");

	  if (e_flags & EF_MIPS_CPIC)
	    strcat (buf, ", cpic");

	  if (e_flags & EF_MIPS_ABI2)
	    strcat (buf, ", abi2");

	  if ((e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_1)
	    strcat (buf, ", mips1");

	  if ((e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_2)
	    strcat (buf, ", mips2");

	  if ((e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_3)
	    strcat (buf, ", mips3");

	  if ((e_flags & EF_MIPS_ARCH) == E_MIPS_ARCH_4)
	    strcat (buf, ", mips4");
	  break;
	}
    }

  return buf;
}

static char *
get_machine_data (e_data)
     unsigned e_data;
{
  static char buff [32];

  switch (e_data)
    {
    case ELFDATA2LSB: return _("ELFDATA2LSB (little endian)");
    case ELFDATA2MSB: return _("ELFDATA2MSB (big endian)");
    default:
      sprintf (buff, _("<unknown>: %x"), e_data);
      return buff;
    }
}

static const char *
get_mips_segment_type (type)
     unsigned long type;
{
  switch (type)
    {
    case PT_MIPS_REGINFO:
      return "REGINFO";
    case PT_MIPS_RTPROC:
      return "RTPROC";
    case PT_MIPS_OPTIONS:
      return "OPTIONS";
    default:
      break;
    }

  return NULL;
}

static const char *
get_segment_type (p_type)
     unsigned long p_type;
{
  static char buff [32];

  switch (p_type)
    {
    case PT_NULL:       return "NULL";
    case PT_LOAD:       return "LOAD";
    case PT_DYNAMIC:	return "DYNAMIC";
    case PT_INTERP:     return "INTERP";
    case PT_NOTE:       return "NOTE";
    case PT_SHLIB:      return "SHLIB";
    case PT_PHDR:       return "PHDR";

    default:
      if ((p_type >= PT_LOPROC) && (p_type <= PT_HIPROC))
	{
	  const char *result;
	  switch (elf_header.e_machine)
	    {
	    case EM_MIPS:
	    case EM_MIPS_RS4_BE:
	      result = get_mips_segment_type (p_type);
	      break;
	    default:
	      result = NULL;
	      break;
	    }
	  if (result == NULL)
	    {
	      sprintf (buff, "LOPROC+%d", p_type - PT_LOPROC);
	      result = buff;
	    }
	  return result;
	}
      else
	{
	  sprintf (buff, _("<unknown>: %x"), p_type);
	  return buff;
	}
    }
}

static const char *
get_mips_section_type_name (sh_type)
     unsigned int sh_type;
{
  switch (sh_type)
    {
    case SHT_MIPS_LIBLIST:
      return "MIPS_LIBLIST";
    case SHT_MIPS_MSYM:
      return "MIPS_MSYM";
    case SHT_MIPS_CONFLICT:
      return "MIPS_CONFLICT";
    case SHT_MIPS_GPTAB:
      return "MIPS_GPTAB";
    case SHT_MIPS_UCODE:
      return "MIPS_UCODE";
    case SHT_MIPS_DEBUG:
      return "MIPS_DEBUG";
    case SHT_MIPS_REGINFO:
      return "MIPS_REGINFO";
    case SHT_MIPS_PACKAGE:
      return "MIPS_PACKAGE";
    case SHT_MIPS_PACKSYM:
      return "MIPS_PACKSYM";
    case SHT_MIPS_RELD:
      return "MIPS_RELD";
    case SHT_MIPS_IFACE:
      return "MIPS_IFACE";
    case SHT_MIPS_CONTENT:
      return "MIPS_CONTENT";
    case SHT_MIPS_OPTIONS:
      return "MIPS_OPTIONS";
    case SHT_MIPS_SHDR:
      return "MIPS_SHDR";
    case SHT_MIPS_FDESC:
      return "MIPS_FDESC";
    case SHT_MIPS_EXTSYM:
      return "MIPS_EXTSYM";
    case SHT_MIPS_DENSE:
      return "MIPS_DENSE";
    case SHT_MIPS_PDESC:
      return "MIPS_PDESC";
    case SHT_MIPS_LOCSYM:
      return "MIPS_LOCSYM";
    case SHT_MIPS_AUXSYM:
      return "MIPS_AUXSYM";
    case SHT_MIPS_OPTSYM:
      return "MIPS_OPTSYM";
    case SHT_MIPS_LOCSTR:
      return "MIPS_LOCSTR";
    case SHT_MIPS_LINE:
      return "MIPS_LINE";
    case SHT_MIPS_RFDESC:
      return "MIPS_RFDESC";
    case SHT_MIPS_DELTASYM:
      return "MIPS_DELTASYM";
    case SHT_MIPS_DELTAINST:
      return "MIPS_DELTAINST";
    case SHT_MIPS_DELTACLASS:
      return "MIPS_DELTACLASS";
    case SHT_MIPS_DWARF:
      return "MIPS_DWARF";
    case SHT_MIPS_DELTADECL:
      return "MIPS_DELTADECL";
    case SHT_MIPS_SYMBOL_LIB:
      return "MIPS_SYMBOL_LIB";
    case SHT_MIPS_EVENTS:
      return "MIPS_EVENTS";
    case SHT_MIPS_TRANSLATE:
      return "MIPS_TRANSLATE";
    case SHT_MIPS_PIXIE:
      return "MIPS_PIXIE";
    case SHT_MIPS_XLATE:
      return "MIPS_XLATE";
    case SHT_MIPS_XLATE_DEBUG:
      return "MIPS_XLATE_DEBUG";
    case SHT_MIPS_WHIRL:
      return "MIPS_WHIRL";
    case SHT_MIPS_EH_REGION:
      return "MIPS_EH_REGION";
    case SHT_MIPS_XLATE_OLD:
      return "MIPS_XLATE_OLD";
    case SHT_MIPS_PDR_EXCEPTION:
      return "MIPS_PDR_EXCEPTION";
    default:
      break;
    }
  return NULL;
}

static const char *
get_section_type_name (sh_type)
     unsigned int sh_type;
{
  static char buff [32];

  switch (sh_type)
    {
    case SHT_NULL:		return "NULL";
    case SHT_PROGBITS:		return "PROGBITS";
    case SHT_SYMTAB:		return "SYMTAB";
    case SHT_STRTAB:		return "STRTAB";
    case SHT_RELA:		return "RELA";
    case SHT_HASH:		return "HASH";
    case SHT_DYNAMIC:		return "DYNAMIC";
    case SHT_NOTE:		return "NOTE";
    case SHT_NOBITS:		return "NOBITS";
    case SHT_REL:		return "REL";
    case SHT_SHLIB:		return "SHLIB";
    case SHT_DYNSYM:		return "DYNSYM";
    case SHT_GNU_verdef:	return "VERDEF";
    case SHT_GNU_verneed:	return "VERNEED";
    case SHT_GNU_versym:	return "VERSYM";
    case 0x6ffffff0:	        return "VERSYM";
    case 0x6ffffffc:	        return "VERDEF";
    case 0x7ffffffd:		return "AUXILIARY";
    case 0x7fffffff:		return "FILTER";

    default:
      if ((sh_type >= SHT_LOPROC) && (sh_type <= SHT_HIPROC))
	{
	  const char *result;

	  switch (elf_header.e_machine)
	    {
	    case EM_MIPS:
	    case EM_MIPS_RS4_BE:
	      result = get_mips_section_type_name (sh_type);
	      break;
	    default:
	      result = NULL;
	      break;
	    }

	  if (result == NULL)
	    {
	      sprintf (buff, _("SHT_LOPROC+%d"), sh_type - SHT_LOPROC);
	      result = buff;
	    }
	  return result;
	}
      else if ((sh_type >= SHT_LOUSER) && (sh_type <= SHT_HIUSER))
	sprintf (buff, _("SHT_LOUSER+%d"), sh_type - SHT_LOUSER);
      else
	sprintf (buff, _("<unknown>: %x"), sh_type);
      return buff;
    }
}

struct option options [] =
{
  {"all", no_argument, 0, 'a'},
  {"file-header", no_argument, 0, 'h'},
  {"program-headers", no_argument, 0, 'l'},
  {"headers", no_argument, 0, 'e'},
  {"histogram", no_argument, &do_histogram, 1},
  {"segments", no_argument, 0, 'l'},
  {"sections", no_argument, 0, 'S'},
  {"section-headers", no_argument, 0, 'S'},
  {"symbols", no_argument, 0, 's'},
  {"syms", no_argument, 0, 's'},
  {"relocs", no_argument, 0, 'r'},
  {"dynamic", no_argument, 0, 'd'},
  {"version-info", no_argument, 0, 'V'},
  {"use-dynamic", no_argument, 0, 'D'},

  {"hex-dump", required_argument, 0, 'x'},
#ifdef SUPPORT_DISASSEMBLY
  {"instruction-dump", required_argument, 0, 'i'},
#endif

  {"version", no_argument, 0, 'v'},
  {"help", no_argument, 0, 'H'},

  {0, no_argument, 0, 0}
};

static void
usage ()
{
  fprintf (stdout, _("Usage: readelf {options} elf-file(s)\n"));
  fprintf (stdout, _("  Options are:\n"));
  fprintf (stdout, _("  -a or --all               Equivalent to: -h -l -S -s -r -d -V --histogram\n"));
  fprintf (stdout, _("  -h or --file-header       Display the ELF file header\n"));
  fprintf (stdout, _("  -l or --program-headers or --segments\n"));
  fprintf (stdout, _("                            Display the program headers\n"));
  fprintf (stdout, _("  -S or --section-headers or --sections\n"));
  fprintf (stdout, _("                            Display the sections' header\n"));
  fprintf (stdout, _("  -e or --headers           Equivalent to: -h -l -S\n"));
  fprintf (stdout, _("  -s or --syms or --symbols Display the symbol table\n"));
  fprintf (stdout, _("  -r or --relocs            Display the relocations (if present)\n"));
  fprintf (stdout, _("  -d or --dynamic           Display the dynamic segment (if present)\n"));
  fprintf (stdout, _("  -V or --version-info      Display the version sections (if present)\n"));
  fprintf (stdout, _("  -D or --use-dynamic       Use the dynamic section info when displaying symbols\n"));
  fprintf (stdout, _("  -x <number> or --hex-dump=<number>\n"));
  fprintf (stdout, _("                            Dump the contents of section <number>\n"));
#ifdef SUPPORT_DISASSEMBLY
  fprintf (stdout, _("  -i <number> or --instruction-dump=<number>\n"));
  fprintf (stdout, _("                            Disassemble the contents of section <number>\n"));
#endif
  fprintf (stdout, _("        --histogram         Display histogram of bucket list lengths\n"));
  fprintf (stdout, _("  -v or --version           Display the version number of readelf\n"));
  fprintf (stdout, _("  -H or --help              Display this information\n"));
  fprintf (stdout, _("Report bugs to bug-gnu-utils@gnu.org\n"));

  exit (0);
}

static void
parse_args (argc, argv)
     int argc;
     char ** argv;
{
  int c;

  if (argc < 2)
    usage ();

  while ((c = getopt_long
	  (argc, argv, "ersahldSDx:i:vV", options, NULL)) != EOF)
    {
      char *    cp;
      int	section;

      switch (c)
	{
	case 0:
	  /* Long options.  */
	  break;
	case 'H':
	  usage ();
	  break;

	case 'a':
	  do_syms ++;
	  do_reloc ++;
	  do_dynamic ++;
	  do_header ++;
	  do_sections ++;
	  do_segments ++;
	  do_version ++;
	  do_histogram ++;
	  break;
	case 'e':
	  do_header ++;
	  do_sections ++;
	  do_segments ++;
	  break;
	case 'D':
	  do_using_dynamic ++;
	  break;
	case 'r':
	  do_reloc ++;
	  break;
	case 'h':
	  do_header ++;
	  break;
	case 'l':
	  do_segments ++;
	  break;
	case 's':
	  do_syms ++;
	  break;
	case 'S':
	  do_sections ++;
	  break;
	case 'd':
	  do_dynamic ++;
	  break;
	case 'x':
	  do_dump ++;
	  section = strtoul (optarg, & cp, 0);
	  if (! * cp && section >= 0 && section < NUM_DUMP_SECTS)
	    {
	      dump_sects [section] |= HEX_DUMP;
	      break;
	    }
	  goto oops;
#ifdef SUPPORT_DISASSEMBLY
	case 'i':
	  do_dump ++;
	  section = strtoul (optarg, & cp, 0);
	  if (! * cp && section >= 0 && section < NUM_DUMP_SECTS)
	    {
	      dump_sects [section] |= DISASS_DUMP;
	      break;
	    }
	  goto oops;
#endif
	case 'v':
	  print_version (program_name);
	  break;
	case 'V':
	  do_version ++;
	  break;
	default:
	oops:
	  /* xgettext:c-format */
	  error (_("Invalid option '-%c'\n"), c);
	  /* Drop through.  */
	case '?':
	  usage ();
	}
    }

  if (!do_dynamic && !do_syms && !do_reloc && !do_sections
      && !do_segments && !do_header && !do_dump && !do_version
      && !do_histogram)
    usage ();
  else if (argc < 3)
    {
      warn (_("Nothing to do.\n"));
      usage();
    }
}

/* Decode the data held in 'elf_header'.  */
static int
process_file_header ()
{
  if (   elf_header.e_ident [EI_MAG0] != ELFMAG0
      || elf_header.e_ident [EI_MAG1] != ELFMAG1
      || elf_header.e_ident [EI_MAG2] != ELFMAG2
      || elf_header.e_ident [EI_MAG3] != ELFMAG3)
    {
      error
	(_("Not an ELF file - it has the wrong magic bytes at the start\n"));
      return 0;
    }

  if (elf_header.e_ident [EI_CLASS] != ELFCLASS32)
    {
      error (_("Not a 32 bit ELF file\n"));
      return 0;
    }

  if (do_header)
    {
      int i;

      printf (_("ELF Header:\n"));
      printf (_("  Magic:   "));
      for (i = 0; i < EI_NIDENT; i ++)
	printf ("%2.2x ", elf_header.e_ident [i]);
      printf ("\n");
      printf (_("  Type:                              %s\n"),
	      get_file_type (elf_header.e_type));
      printf (_("  Machine:                           %s\n"),
	      get_machine_name (elf_header.e_machine));
      printf (_("  Version:                           0x%lx\n"),
	      (unsigned long) elf_header.e_version);
      printf (_("  Data:                              %s\n"),
	      get_machine_data (elf_header.e_ident [EI_DATA]));
      printf (_("  Entry point address:               0x%lx\n"),
	      (unsigned long) elf_header.e_entry);
      printf (_("  Start of program headers:          %ld (bytes into file)\n"),
	      (long) elf_header.e_phoff);
      printf (_("  Start of section headers:          %ld (bytes into file)\n"),
	      (long) elf_header.e_shoff);
      printf (_("  Flags:                             0x%lx%s\n"),
	      (unsigned long) elf_header.e_flags,
	      get_machine_flags (elf_header.e_flags, elf_header.e_machine));
      printf (_("  Size of this header:               %ld (bytes)\n"),
	      (long) elf_header.e_ehsize);
      printf (_("  Size of program headers:           %ld (bytes)\n"),
	      (long) elf_header.e_phentsize);
      printf (_("  Number of program headers:         %ld\n"),
	      (long) elf_header.e_phnum);
      printf (_("  Size of section headers:           %ld (bytes)\n"),
	      (long) elf_header.e_shentsize);
      printf (_("  Number of section headers:         %ld\n"),
	      (long) elf_header.e_shnum);
      printf (_("  Section header string table index: %ld\n"),
	      (long) elf_header.e_shstrndx);
    }

  return 1;
}


static int
process_program_headers (file)
     FILE * file;
{
  Elf32_External_Phdr * phdrs;
  Elf32_Internal_Phdr * program_headers;
  Elf32_Internal_Phdr * segment;
  unsigned int	        i;

  if (elf_header.e_phnum == 0)
    {
      if (do_segments)
	printf (_("\nThere are no program headers in this file.\n"));
      return 1;
    }

  if (do_segments && !do_header)
    {
      printf (_("\nElf file is %s\n"), get_file_type (elf_header.e_type));
      printf (_("Entry point 0x%lx\n"), (unsigned long) elf_header.e_entry);
      printf (_("There are %d program headers, starting at offset %lx:\n"),
	      elf_header.e_phnum, (unsigned long) elf_header.e_phoff);
    }

  GET_DATA_ALLOC (elf_header.e_phoff,
		  elf_header.e_phentsize * elf_header.e_phnum,
		  phdrs, Elf32_External_Phdr *, "program headers");

  program_headers = (Elf32_Internal_Phdr *) malloc
    (elf_header.e_phnum * sizeof (Elf32_Internal_Phdr));

  if (program_headers == NULL)
    {
      error (_("Out of memory\n"));
      return 0;
    }

  for (i = 0, segment = program_headers;
       i < elf_header.e_phnum;
       i ++, segment ++)
    {
      segment->p_type   = BYTE_GET (phdrs[i].p_type);
      segment->p_offset = BYTE_GET (phdrs[i].p_offset);
      segment->p_vaddr  = BYTE_GET (phdrs[i].p_vaddr);
      segment->p_paddr  = BYTE_GET (phdrs[i].p_paddr);
      segment->p_filesz = BYTE_GET (phdrs[i].p_filesz);
      segment->p_memsz  = BYTE_GET (phdrs[i].p_memsz);
      segment->p_flags  = BYTE_GET (phdrs[i].p_flags);
      segment->p_align  = BYTE_GET (phdrs[i].p_align);
    }

  free (phdrs);

  if (do_segments)
    {
      printf
	(_("\nProgram Header%s:\n"), elf_header.e_phnum > 1 ? "s" : "");
      printf
	(_("  Type        Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align\n"));
    }

  loadaddr = -1;
  dynamic_addr = 0;

  for (i = 0, segment = program_headers;
       i < elf_header.e_phnum;
       i ++, segment ++)
    {
      if (do_segments)
	{
	  printf ("  %-11.11s ", get_segment_type (segment->p_type));
	  printf ("0x%6.6lx ", (unsigned long) segment->p_offset);
	  printf ("0x%8.8lx ", (unsigned long) segment->p_vaddr);
	  printf ("0x%8.8lx ", (unsigned long) segment->p_paddr);
	  printf ("0x%5.5lx ", (unsigned long) segment->p_filesz);
	  printf ("0x%5.5lx ", (unsigned long) segment->p_memsz);
	  printf ("%c%c%c ",
		  (segment->p_flags & PF_R ? 'R' : ' '),
		  (segment->p_flags & PF_W ? 'W' : ' '),
		  (segment->p_flags & PF_X ? 'E' : ' '));
	  printf ("%#lx", (unsigned long) segment->p_align);
	}

      switch (segment->p_type)
	{
	case PT_LOAD:
	  if (loadaddr == -1)
	    loadaddr = (segment->p_vaddr & 0xfffff000)
	      - (segment->p_offset & 0xfffff000);
	  break;

	case PT_DYNAMIC:
	  if (dynamic_addr)
	    error (_("more than one dynamic segment\n"));

	  dynamic_addr = segment->p_offset;
	  dynamic_size = segment->p_filesz;
	  break;

	case PT_INTERP:
	  if (fseek (file, segment->p_offset, SEEK_SET))
	    error (_("Unable to find program interpreter name\n"));
	  else
	    {
	      program_interpreter[0] = 0;
	      fscanf (file, "%63s", program_interpreter);

	      if (do_segments)
		printf (_("\n      [Requesting program interpreter: %s]"),
		    program_interpreter);
	    }
	  break;
	}

      if (do_segments)
	putc ('\n', stdout);
    }

  if (loadaddr == -1)
    {
      /* Very strange. */
      loadaddr = 0;
    }

  if (do_segments && section_headers != NULL)
    {
      printf (_("\n Section to Segment mapping:\n"));
      printf (_("  Segment Sections...\n"));

      assert (string_table != NULL);

      for (i = 0; i < elf_header.e_phnum; i++)
	{
	  int        j;
	  Elf32_Internal_Shdr * section;

	  segment = program_headers + i;
	  section = section_headers;

	  printf ("   %2.2d     ", i);

	  for (j = 0; j < elf_header.e_shnum; j++, section ++)
	    {
	      if (section->sh_size > 0
		  /* Compare allocated sections by VMA, unallocated
		     sections by file offset.  */
		  && (section->sh_flags & SHF_ALLOC
		      ? (section->sh_addr >= segment->p_vaddr
			 && section->sh_addr + section->sh_size
			 <= segment->p_vaddr + segment->p_memsz)
		      : (section->sh_offset >= segment->p_offset
			 && (section->sh_offset + section->sh_size
			     <= segment->p_offset + segment->p_filesz))))
		printf ("%s ", SECTION_NAME (section));
	    }

	  putc ('\n',stdout);
	}
    }

  free (program_headers);

  return 1;
}


static int
get_section_headers (file)
     FILE * file;
{
  Elf32_External_Shdr * shdrs;
  Elf32_Internal_Shdr * internal;
  unsigned int          i;

  GET_DATA_ALLOC (elf_header.e_shoff,
		  elf_header.e_shentsize * elf_header.e_shnum,
		  shdrs, Elf32_External_Shdr *, "section headers");

  section_headers = (Elf32_Internal_Shdr *) malloc
    (elf_header.e_shnum * sizeof (Elf32_Internal_Shdr));

  if (section_headers == NULL)
    {
      error (_("Out of memory\n"));
      return 0;
    }

  for (i = 0, internal = section_headers;
       i < elf_header.e_shnum;
       i ++, internal ++)
    {
      internal->sh_name      = BYTE_GET (shdrs[i].sh_name);
      internal->sh_type      = BYTE_GET (shdrs[i].sh_type);
      internal->sh_flags     = BYTE_GET (shdrs[i].sh_flags);
      internal->sh_addr      = BYTE_GET (shdrs[i].sh_addr);
      internal->sh_offset    = BYTE_GET (shdrs[i].sh_offset);
      internal->sh_size      = BYTE_GET (shdrs[i].sh_size);
      internal->sh_link      = BYTE_GET (shdrs[i].sh_link);
      internal->sh_info      = BYTE_GET (shdrs[i].sh_info);
      internal->sh_addralign = BYTE_GET (shdrs[i].sh_addralign);
      internal->sh_entsize   = BYTE_GET (shdrs[i].sh_entsize);
    }

  free (shdrs);

  return 1;
}

static Elf_Internal_Sym *
get_elf_symbols (file, offset, number)
     FILE * file;
     unsigned long offset;
     unsigned long number;
{
  Elf32_External_Sym * esyms;
  Elf_Internal_Sym *   isyms;
  Elf_Internal_Sym *   psym;
  unsigned int         j;

  GET_DATA_ALLOC (offset, number * sizeof (Elf32_External_Sym),
		  esyms, Elf32_External_Sym *, "symbols");

  isyms = (Elf_Internal_Sym *) malloc (number * sizeof (Elf_Internal_Sym));

  if (isyms == NULL)
    {
      error (_("Out of memory\n"));
      free (esyms);

      return NULL;
    }

  for (j = 0, psym = isyms;
       j < number;
       j ++, psym ++)
    {
      psym->st_name  = BYTE_GET (esyms[j].st_name);
      psym->st_value = BYTE_GET (esyms[j].st_value);
      psym->st_size  = BYTE_GET (esyms[j].st_size);
      psym->st_shndx = BYTE_GET (esyms[j].st_shndx);
      psym->st_info  = BYTE_GET (esyms[j].st_info);
      psym->st_other = BYTE_GET (esyms[j].st_other);
    }

  free (esyms);

  return isyms;
}

static int
process_section_headers (file)
     FILE * file;
{
  Elf32_Internal_Shdr * section;
  int        i;

  section_headers = NULL;

  if (elf_header.e_shnum == 0)
    {
      if (do_sections)
	printf (_("\nThere are no sections in this file.\n"));

      return 1;
    }

  if (do_sections && !do_header)
    printf (_("There are %d section headers, starting at offset %x:\n"),
	    elf_header.e_shnum, elf_header.e_shoff);

  if (! get_section_headers (file))
    return 0;

  /* Read in the string table, so that we have names to display.  */
  section = section_headers + elf_header.e_shstrndx;

  if (section->sh_size != 0)
    {
      unsigned long string_table_offset;

      string_table_offset = section->sh_offset;

      GET_DATA_ALLOC (section->sh_offset, section->sh_size,
		      string_table, char *, "string table");
    }

  /* Scan the sections for the dynamic symbol table
     and dynamic string table.  */
  dynamic_symbols = NULL;
  dynamic_strings = NULL;
  dynamic_syminfo = NULL;
  for (i = 0, section = section_headers;
       i < elf_header.e_shnum;
       i ++, section ++)
    {
      if (section->sh_type == SHT_DYNSYM)
	{
	  if (dynamic_symbols != NULL)
	    {
	      error (_("File contains multiple dynamic symbol tables\n"));
	      continue;
	    }

	  dynamic_symbols = get_elf_symbols
	    (file, section->sh_offset, section->sh_size / section->sh_entsize);
	}
      else if (section->sh_type == SHT_STRTAB
	       && strcmp (SECTION_NAME (section), ".dynstr") == 0)
	{
	  if (dynamic_strings != NULL)
	    {
	      error (_("File contains multiple dynamic string tables\n"));
	      continue;
	    }

	  GET_DATA_ALLOC (section->sh_offset, section->sh_size,
			  dynamic_strings, char *, "dynamic strings");
	}
    }

  if (! do_sections)
    return 1;

  printf (_("\nSection Header%s:\n"), elf_header.e_shnum > 1 ? "s" : "");
  printf
    (_("  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al\n"));

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum;
       i ++, section ++)
    {
      printf ("  [%2d] %-17.17s %-15.15s ",
	      i,
	      SECTION_NAME (section),
	      get_section_type_name (section->sh_type));

      printf ( "%8.8lx %6.6lx %6.6lx %2.2lx",
	       (unsigned long) section->sh_addr,
	       (unsigned long) section->sh_offset,
	       (unsigned long) section->sh_size,
	       (unsigned long) section->sh_entsize);

      printf (" %c%c%c %2ld %3lx %ld \n",
	      (section->sh_flags & SHF_WRITE ? 'W' : ' '),
	      (section->sh_flags & SHF_ALLOC ? 'A' : ' '),
	      (section->sh_flags & SHF_EXECINSTR ? 'X' : ' '),
	      (unsigned long) section->sh_link,
	      (unsigned long) section->sh_info,
	      (unsigned long) section->sh_addralign);
    }

  return 1;
}

/* Process the reloc section.  */
static int
process_relocs (file)
     FILE * file;
{
  unsigned long    rel_size;
  unsigned long	   rel_offset;


  if (!do_reloc)
    return 1;

  if (do_using_dynamic)
    {
      rel_size   = 0;
      rel_offset = 0;

      if (dynamic_info[DT_REL])
	{
	  rel_offset = dynamic_info[DT_REL];
	  rel_size   = dynamic_info[DT_RELSZ];
	}
      else if (dynamic_info [DT_RELA])
	{
	  rel_offset = dynamic_info[DT_RELA];
	  rel_size   = dynamic_info[DT_RELASZ];
	}
      else if (dynamic_info[DT_JMPREL])
	{
	  rel_offset = dynamic_info[DT_JMPREL];
	  rel_size   = dynamic_info[DT_PLTRELSZ];
	}

      if (rel_size)
	{
	  printf
	    (_("\nRelocation section at offset 0x%x contains %d bytes:\n"),
	     rel_offset, rel_size);

	  dump_relocations (file, rel_offset - loadaddr, rel_size,
			    dynamic_symbols, dynamic_strings);
	}
      else
	printf (_("\nThere are no dynamic relocations in this file.\n"));
    }
  else
    {
      Elf32_Internal_Shdr *     section;
      unsigned long 		i;
      int           		found = 0;

      for (i = 0, section = section_headers;
	   i < elf_header.e_shnum;
	   i++, section ++)
	{
	  if (   section->sh_type != SHT_RELA
	      && section->sh_type != SHT_REL)
	    continue;

	  rel_offset = section->sh_offset;
	  rel_size   = section->sh_size;

	  if (rel_size)
	    {
	      Elf32_Internal_Shdr * strsec;
	      Elf32_Internal_Shdr * symsec;
	      Elf_Internal_Sym *    symtab;
	      char *                strtab;

	      printf (_("\nRelocation section "));

	      if (string_table == NULL)
		printf ("%d", section->sh_name);
	      else
		printf ("'%s'", SECTION_NAME (section));

	      printf (_(" at offset 0x%x contains %d entries:\n"),
		 rel_offset, rel_size / section->sh_entsize);

	      symsec = section_headers + section->sh_link;

	      symtab = get_elf_symbols (file, symsec->sh_offset,
					symsec->sh_size / symsec->sh_entsize);

	      if (symtab == NULL)
		continue;

	      strsec = section_headers + symsec->sh_link;

	      GET_DATA_ALLOC (strsec->sh_offset, strsec->sh_size, strtab,
			      char *, "string table");

	      dump_relocations (file, rel_offset, rel_size, symtab, strtab);

	      free (strtab);
	      free (symtab);

	      found = 1;
	    }
	}

      if (! found)
	printf (_("\nThere are no relocations in this file.\n"));
    }

  return 1;
}


static void
dynamic_segment_mips_val (entry)
     Elf_Internal_Dyn *entry;
{
  if (do_dynamic)
    switch (entry->d_tag)
      {
      case DT_MIPS_FLAGS:
	if (entry->d_un.d_val == 0)
	  printf ("NONE\n");
	else
	  {
	    static const char *opts[] =
	    {
	      "QUICKSTART", "NOTPOT", "NO_LIBRARY_REPLACEMENT",
	      "NO_MOVE", "SGI_ONLY", "GUARANTEE_INIT", "DELTA_C_PLUS_PLUS",
	      "GUARANTEE_START_INIT", "PIXIE", "DEFAULT_DELAY_LOAD",
	      "REQUICKSTART", "REQUICKSTARTED", "CORD", "NO_UNRES_UNDEF",
	      "RLD_ORDER_SAFE"
	    };
	    unsigned int cnt;
	    int first = 1;
	    for (cnt = 0; cnt < sizeof (opts) / sizeof (opts[0]); ++cnt)
	      if (entry->d_un.d_val & (1 << cnt))
		{
		  printf ("%s%s", first ? "" : " ", opts[cnt]);
		  first = 0;
		}
	    puts ("");
	  }
	break;

      case DT_MIPS_IVERSION:
	if (dynamic_strings != NULL)
	  printf ("Interface Version: %s\n",
		  dynamic_strings + entry->d_un.d_val);
	else
	  printf ("%#ld\n", (long) entry->d_un.d_ptr);
	break;

      case DT_MIPS_TIME_STAMP:
	{
	  char timebuf[20];
	  time_t time = entry->d_un.d_val;
	  strftime (timebuf, 20, "%Y-%m-%dT%H:%M:%S", gmtime (&time));
	  printf ("Time Stamp: %s\n", timebuf);
	}
	break;

      case DT_MIPS_RLD_VERSION:
      case DT_MIPS_LOCAL_GOTNO:
      case DT_MIPS_CONFLICTNO:
      case DT_MIPS_LIBLISTNO:
      case DT_MIPS_SYMTABNO:
      case DT_MIPS_UNREFEXTNO:
      case DT_MIPS_HIPAGENO:
      case DT_MIPS_DELTA_CLASS_NO:
      case DT_MIPS_DELTA_INSTANCE_NO:
      case DT_MIPS_DELTA_RELOC_NO:
      case DT_MIPS_DELTA_SYM_NO:
      case DT_MIPS_DELTA_CLASSSYM_NO:
      case DT_MIPS_COMPACT_SIZE:
	printf ("%#ld\n", (long) entry->d_un.d_ptr);
	break;

      default:
	printf ("%#lx\n", (long) entry->d_un.d_ptr);
      }
}

/* Parse the dynamic segment */
static int
process_dynamic_segment (file)
     FILE * file;
{
  Elf_Internal_Dyn *    entry;
  Elf32_External_Dyn *  edyn;
  unsigned int i;

  if (dynamic_size == 0)
    {
      if (do_dynamic)
	printf (_("\nThere is no dynamic segment in this file.\n"));

      return 1;
    }

  GET_DATA_ALLOC (dynamic_addr, dynamic_size,
		  edyn, Elf32_External_Dyn *, "dynamic segment");

  /* SGI's ELF has more than one section in the DYNAMIC segment.  Determine
     how large .dynamic is now.  We can do this even before the byte
     swapping since the DT_NULL tag is recognizable.  */
  dynamic_size = 0;
  while (*(Elf32_Word *) edyn[dynamic_size++].d_tag != DT_NULL)
    ;

  dynamic_segment = (Elf_Internal_Dyn *)
    malloc (dynamic_size * sizeof (Elf_Internal_Dyn));

  if (dynamic_segment == NULL)
    {
      error (_("Out of memory\n"));
      free (edyn);
      return 0;
    }

  for (i = 0, entry = dynamic_segment;
       i < dynamic_size;
       i ++, entry ++)
    {
      entry->d_tag      = BYTE_GET (edyn [i].d_tag);
      entry->d_un.d_val = BYTE_GET (edyn [i].d_un.d_val);
    }

  free (edyn);

  /* Find the appropriate symbol table.  */
  if (dynamic_symbols == NULL)
    {
      for (i = 0, entry = dynamic_segment;
	   i < dynamic_size;
	   ++i, ++ entry)
	{
	  unsigned long        offset;
	  long                 num_syms;

	  if (entry->d_tag != DT_SYMTAB)
	    continue;

	  dynamic_info[DT_SYMTAB] = entry->d_un.d_val;

	  /* Since we do not know how big the symbol table is,
	     we default to reading in the entire file (!) and
	     processing that.  This is overkill, I know, but it
	     should work. */

	  offset = entry->d_un.d_val - loadaddr;

	  if (fseek (file, 0, SEEK_END))
	    error (_("Unable to seek to end of file!"));

	  num_syms = (ftell (file) - offset) / sizeof (Elf32_External_Sym);

	  if (num_syms < 1)
	    {
	      error (_("Unable to determine the number of symbols to load\n"));
	      continue;
	    }

	  dynamic_symbols = get_elf_symbols (file, offset, num_syms);
	}
    }

  /* Similarly find a string table.  */
  if (dynamic_strings == NULL)
    {
      for (i = 0, entry = dynamic_segment;
	   i < dynamic_size;
	   ++i, ++ entry)
	{
	  unsigned long offset;
	  long          str_tab_len;

	  if (entry->d_tag != DT_STRTAB)
	    continue;

	  dynamic_info[DT_STRTAB] = entry->d_un.d_val;

	  /* Since we do not know how big the string table is,
	     we default to reading in the entire file (!) and
	     processing that.  This is overkill, I know, but it
	     should work. */

	  offset = entry->d_un.d_val - loadaddr;
	  if (fseek (file, 0, SEEK_END))
	    error (_("Unable to seek to end of file\n"));
	  str_tab_len = ftell (file) - offset;

	  if (str_tab_len < 1)
	    {
	      error
		(_("Unable to determine the length of the dynamic string table\n"));
	      continue;
	    }

	  GET_DATA_ALLOC (offset, str_tab_len, dynamic_strings, char *,
			  "dynamic string table");

	  break;
	}
    }

  /* And find the syminfo section if available.  */
  if (dynamic_syminfo == NULL)
    {
      unsigned int syminsz = 0;

      for (i = 0, entry = dynamic_segment;
	   i < dynamic_size;
	   ++i, ++ entry)
	{
	  if (entry->d_tag == DT_SYMINENT)
	    assert (sizeof (Elf_External_Syminfo) == entry->d_un.d_val);
	  else if (entry->d_tag == DT_SYMINSZ)
	    syminsz = entry->d_un.d_val;
	  else if (entry->d_tag == DT_SYMINFO)
	    dynamic_syminfo_offset = entry->d_un.d_val - loadaddr;
	}

      if (dynamic_syminfo_offset != 0 && syminsz != 0)
	{
	  Elf_External_Syminfo *extsyminfo;
	  Elf_Internal_Syminfo *syminfo;

	  /* There is a syminfo section.  Read the data.  */
	  GET_DATA_ALLOC (dynamic_syminfo_offset, syminsz, extsyminfo,
			  Elf_External_Syminfo *, "symbol information");

	  dynamic_syminfo = (Elf_Internal_Syminfo *) malloc (syminsz);
	  if (dynamic_syminfo == NULL)
	    {
	      error (_("Out of memory\n"));
	      return 0;
	    }

	  dynamic_syminfo_nent = syminsz / sizeof (Elf_External_Syminfo);
	  for (i = 0, syminfo = dynamic_syminfo; i < dynamic_syminfo_nent;
	       ++i, ++syminfo)
	    {
	      syminfo->si_boundto = BYTE_GET (extsyminfo[i].si_boundto);
	      syminfo->si_flags = BYTE_GET (extsyminfo[i].si_flags);
	    }

	  free (extsyminfo);
	}
    }

  if (do_dynamic && dynamic_addr)
    printf (_("\nDynamic segment at offset 0x%x contains %d entries:\n"),
	    dynamic_addr, dynamic_size);
  if (do_dynamic)
    printf (_("  Tag        Type                         Name/Value\n"));

  for (i = 0, entry = dynamic_segment;
       i < dynamic_size;
       i++, entry ++)
    {
      if (do_dynamic)
	printf (_("  0x%-8.8lx (%s)%*s"),
		(unsigned long) entry->d_tag,
		get_dynamic_type (entry->d_tag),
		27 - strlen (get_dynamic_type (entry->d_tag)),
		" ");

      switch (entry->d_tag)
	{
	case DT_AUXILIARY:
	case DT_FILTER:
	  if (do_dynamic)
	    {
	      if (entry->d_tag == DT_AUXILIARY)
		printf (_("Auxiliary library"));
	      else
		printf (_("Filter library"));

	      if (dynamic_strings)
		printf (": [%s]\n", dynamic_strings + entry->d_un.d_val);
	      else
		printf (": %#lx\n", (long) entry->d_un.d_val);
	    }
	  break;

	case DT_POSFLAG_1:
	  if (do_dynamic)
	    {
	      printf (_("Flags:"));
	      if (entry->d_un.d_val == 0)
		printf (_(" None\n"));
	      else
		{
		  if (entry->d_un.d_val & DF_P1_LAZYLOAD)
		    printf (" LAZYLOAD");
		  if (entry->d_un.d_val & DF_P1_LAZYLOAD)
		    printf (" GROUPPERM");
		  puts ("");
		}
	    }
	  break;

	case DT_FLAGS_1:
	  if (do_dynamic)
	    {
	      printf (_("Flags:"));
	      if (entry->d_un.d_val == 0)
		printf (_(" None\n"));
	      else
		{
		  if (entry->d_un.d_val & DF_1_NOW)
		    printf (" NOW");
		  if (entry->d_un.d_val & DF_1_GLOBAL)
		    printf (" GLOBAL");
		  if (entry->d_un.d_val & DF_1_GROUP)
		    printf (" GROUP");
		  if (entry->d_un.d_val & DF_1_NODELETE)
		    printf (" NODELETE");
		  if (entry->d_un.d_val & DF_1_LOADFLTR)
		    printf (" LOADFLTR");
		  if (entry->d_un.d_val & DF_1_INITFIRST)
		    printf (" INITFIRST");
		  if (entry->d_un.d_val & DF_1_NOOPEN)
		    printf (" NOOPEN");
		  if (entry->d_un.d_val & DF_1_ORIGIN)
		    printf (" ORIGIN");
		  if (entry->d_un.d_val & DF_1_DIRECT)
		    printf (" DIRECT");
		  if (entry->d_un.d_val & DF_1_TRANS)
		    printf (" TRANS");
		  if (entry->d_un.d_val & DF_1_INTERPOSE)
		    printf (" INTERPOSE");
		  puts ("");
		}
	    }
	  break;

	case DT_NULL	:
	case DT_NEEDED	:
	case DT_PLTRELSZ:
	case DT_PLTGOT	:
	case DT_HASH	:
	case DT_STRTAB	:
	case DT_SYMTAB	:
	case DT_RELA	:
	case DT_INIT	:
	case DT_FINI	:
	case DT_SONAME	:
	case DT_RPATH	:
	case DT_SYMBOLIC:
	case DT_REL	:
	case DT_PLTREL	:
	case DT_DEBUG	:
	case DT_TEXTREL	:
	case DT_JMPREL	:
	  dynamic_info[entry->d_tag] = entry->d_un.d_val;

	  if (do_dynamic)
	    {
	      char * name;

	      if (dynamic_strings == NULL)
		name = NULL;
	      else
		name = dynamic_strings + entry->d_un.d_val;

	      if (name)
		{
		  switch (entry->d_tag)
		    {
		    case DT_NEEDED:
		      printf (_("Shared library: [%s]"), name);

		      if (strcmp (name, program_interpreter))
			printf ("\n");
		      else
			printf (_(" program interpreter\n"));
		      break;

		    case DT_SONAME:
		      printf (_("Library soname: [%s]\n"), name);
		      break;

		    case DT_RPATH:
		      printf (_("Library rpath: [%s]\n"), name);
		      break;

		    default:
		      printf ("%#lx\n", (long) entry->d_un.d_val);
		    }
		}
	      else
		printf ("%#lx\n", (long) entry->d_un.d_val);
	    }
	  break;

	case DT_RELASZ	:
	case DT_RELAENT	:
	case DT_STRSZ	:
	case DT_SYMENT	:
	case DT_RELSZ	:
	case DT_RELENT	:
	case DT_VERDEFNUM:
	case DT_VERNEEDNUM:
	case DT_RELACOUNT:
	case DT_RELCOUNT:
	  if (do_dynamic)
	    printf ("%ld\n", entry->d_un.d_val);
	  break;

	case DT_SYMINSZ	:
	case DT_SYMINENT:
	case DT_SYMINFO	:
	case DT_USED:
	  if (do_dynamic)
	    {
	      char * name;

	      if (dynamic_strings == NULL)
		name = NULL;
	      else
		name = dynamic_strings + entry->d_un.d_val;



	      if (name)
		{
		  switch (entry->d_tag)
		    {
		    case DT_USED:
		      printf (_("Not needed object: [%s]\n"), name);
		      break;

		    default:
		      printf ("%#lx\n", (long) entry->d_un.d_val);
		    }
		}
	      else
		printf ("%#lx\n", (long) entry->d_un.d_val);
	    }
	  break;

	default:
	  if ((entry->d_tag >= DT_VERSYM) && (entry->d_tag <= DT_VERNEEDNUM))
	    {
	      version_info [DT_VERSIONTAGIDX (entry->d_tag)] =
		entry->d_un.d_val;

	      if (do_dynamic)
		printf ("%#lx\n", (long) entry->d_un.d_ptr);
	    }
	  else
	    switch (elf_header.e_machine)
	      {
	      case EM_MIPS:
	      case EM_MIPS_RS4_BE:
		dynamic_segment_mips_val (entry);
		break;
	      default:
		if (do_dynamic)
		  printf ("%#lx\n", (long) entry->d_un.d_ptr);
	      }
	  break;
	}
    }

  return 1;
}

static char *
get_ver_flags (flags)
     unsigned int flags;
{
  static char buff [32];

  buff[0] = 0;

  if (flags == 0)
    return _("none");

  if (flags & VER_FLG_BASE)
    strcat (buff, "BASE ");

  if (flags & VER_FLG_WEAK)
    {
      if (flags & VER_FLG_BASE)
	strcat (buff, "| ");

      strcat (buff, "WEAK ");
    }

  if (flags & ~(VER_FLG_BASE | VER_FLG_WEAK))
    strcat (buff, "| <unknown>");

  return buff;
}

/* Display the contents of the version sections.  */
static int
process_version_sections (file)
     FILE * file;
{
  Elf32_Internal_Shdr * section;
  unsigned   i;
  int        found = 0;

  if (! do_version)
    return 1;

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum;
       i++, section ++)
    {
      switch (section->sh_type)
	{
	case SHT_GNU_verdef:
	  {
	    Elf_External_Verdef * edefs;
	    unsigned int          idx;
	    unsigned int          cnt;

	    found = 1;

	    printf
	      (_("\nVersion definition section '%s' contains %d entries:\n"),
	       SECTION_NAME (section), section->sh_info);

	    printf (_("  Addr: %#08x  Offset: %#08x  Link: %x (%s)\n"),
		    section->sh_addr, section->sh_offset, section->sh_link,
		    SECTION_NAME (section_headers + section->sh_link));

	    GET_DATA_ALLOC (section->sh_offset, section->sh_size,
			    edefs, Elf_External_Verdef *,
			    "version definition section");

	    for (idx = cnt = 0; cnt < section->sh_info; ++ cnt)
	      {
		char *                 vstart;
		Elf_External_Verdef *  edef;
		Elf_Internal_Verdef    ent;
		Elf_External_Verdaux * eaux;
		Elf_Internal_Verdaux   aux;
		int                    j;
		int                    isum;

		vstart = ((char *) edefs) + idx;

		edef = (Elf_External_Verdef *) vstart;

		ent.vd_version = BYTE_GET (edef->vd_version);
		ent.vd_flags   = BYTE_GET (edef->vd_flags);
		ent.vd_ndx     = BYTE_GET (edef->vd_ndx);
		ent.vd_cnt     = BYTE_GET (edef->vd_cnt);
		ent.vd_hash    = BYTE_GET (edef->vd_hash);
		ent.vd_aux     = BYTE_GET (edef->vd_aux);
		ent.vd_next    = BYTE_GET (edef->vd_next);

		printf (_("  %#06x: Rev: %d  Flags: %s"),
			idx, ent.vd_version, get_ver_flags (ent.vd_flags));

		printf (_("  Index: %ld  Cnt: %ld  "), ent.vd_ndx, ent.vd_cnt);

		vstart += ent.vd_aux;

		eaux = (Elf_External_Verdaux *) vstart;

		aux.vda_name = BYTE_GET (eaux->vda_name);
		aux.vda_next = BYTE_GET (eaux->vda_next);

		if (dynamic_strings)
		  printf (_("Name: %s\n"), dynamic_strings + aux.vda_name);
		else
		  printf (_("Name index: %ld\n"), aux.vda_name);

		isum = idx + ent.vd_aux;

		for (j = 1; j < ent.vd_cnt; j ++)
		  {
		    isum   += aux.vda_next;
		    vstart += aux.vda_next;

		    eaux = (Elf_External_Verdaux *) vstart;

		    aux.vda_name = BYTE_GET (eaux->vda_name);
		    aux.vda_next = BYTE_GET (eaux->vda_next);

		    if (dynamic_strings)
		      printf (_("  %#06x: Parent %d: %s\n"),
			      isum, j, dynamic_strings + aux.vda_name);
		    else
		      printf (_("  %#06x: Parent %d, name index: %ld\n"),
			      isum, j, aux.vda_name);
		  }

		idx += ent.vd_next;
	      }

	    free (edefs);
	  }
	break;

	case SHT_GNU_verneed:
	  {
	    Elf_External_Verneed *  eneed;
	    unsigned int            idx;
	    unsigned int            cnt;

	    found = 1;

	    printf (_("\nVersion needs section '%s' contains %d entries:\n"),
		    SECTION_NAME (section), section->sh_info);

	    printf
	      (_(" Addr: %#08x  Offset: %#08x  Link to section: %d (%s)\n"),
	       section->sh_addr, section->sh_offset, section->sh_link,
	       SECTION_NAME (section_headers + section->sh_link));

	    GET_DATA_ALLOC (section->sh_offset, section->sh_size,
			    eneed, Elf_External_Verneed *,
			    "version need section");

	    for (idx = cnt = 0; cnt < section->sh_info; ++cnt)
	      {
		Elf_External_Verneed * entry;
		Elf_Internal_Verneed     ent;
		int                      j;
		int                      isum;
		char *                   vstart;

		vstart = ((char *) eneed) + idx;

		entry = (Elf_External_Verneed *) vstart;

		ent.vn_version = BYTE_GET (entry->vn_version);
		ent.vn_cnt     = BYTE_GET (entry->vn_cnt);
		ent.vn_file    = BYTE_GET (entry->vn_file);
		ent.vn_aux     = BYTE_GET (entry->vn_aux);
		ent.vn_next    = BYTE_GET (entry->vn_next);

		printf (_("  %#06x: Version: %d"), idx, ent.vn_version);

		if (dynamic_strings)
		  printf (_("  File: %s"), dynamic_strings + ent.vn_file);
		else
		  printf (_("  File: %lx"), ent.vn_file);

		printf (_("  Cnt: %d\n"), ent.vn_cnt);

		vstart += ent.vn_aux;

		for (j = 0, isum = idx + ent.vn_aux; j < ent.vn_cnt; ++j)
		  {
		    Elf_External_Vernaux * eaux;
		    Elf_Internal_Vernaux   aux;

		    eaux = (Elf_External_Vernaux *) vstart;

		    aux.vna_hash  = BYTE_GET (eaux->vna_hash);
		    aux.vna_flags = BYTE_GET (eaux->vna_flags);
		    aux.vna_other = BYTE_GET (eaux->vna_other);
		    aux.vna_name  = BYTE_GET (eaux->vna_name);
		    aux.vna_next  = BYTE_GET (eaux->vna_next);

		    if (dynamic_strings)
		      printf (_("  %#06x: Name: %s"),
			      isum, dynamic_strings + aux.vna_name);
		    else
		      printf (_("  %#06x: Name index: %lx"),
			      isum, aux.vna_name);

		    printf (_("  Flags: %s  Version: %d\n"),
			    get_ver_flags (aux.vna_flags), aux.vna_other);

		    isum   += aux.vna_next;
		    vstart += aux.vna_next;
		  }

		idx += ent.vn_next;
	      }

	    free (eneed);
	  }
	break;

	case SHT_GNU_versym:
	  {
	    Elf32_Internal_Shdr *       link_section;
	    int              		total;
	    int              		cnt;
	    unsigned char * 		edata;
	    unsigned short * 		data;
	    char *           		strtab;
	    Elf_Internal_Sym * 		symbols;
	    Elf32_Internal_Shdr *       string_sec;

	    link_section = section_headers + section->sh_link;
	    total = section->sh_size / section->sh_entsize;

	    found = 1;

	    symbols = get_elf_symbols
	      (file, link_section->sh_offset,
	       link_section->sh_size / link_section->sh_entsize);

	    string_sec = section_headers + link_section->sh_link;

	    GET_DATA_ALLOC (string_sec->sh_offset, string_sec->sh_size,
			    strtab, char *, "version string table");

	    printf (_("\nVersion symbols section '%s' contains %d entries:\n"),
		    SECTION_NAME (section), total);

	    printf (_(" Addr: %#08x  Offset: %#08x  Link: %x (%s)\n"),
		    section->sh_addr, section->sh_offset, section->sh_link,
		    SECTION_NAME (link_section));

	    GET_DATA_ALLOC (version_info [DT_VERSIONTAGIDX (DT_VERSYM)]
			    - loadaddr,
			    total * sizeof (short), edata,
			    char *, "version symbol data");

	    data = (unsigned short *) malloc (total * sizeof (short));

	    for (cnt = total; cnt --;)
	      data [cnt] = byte_get (edata + cnt * sizeof (short), sizeof (short));

	    free (edata);

	    for (cnt = 0; cnt < total; cnt += 4)
	      {
		int j, nn;

		printf ("  %03x:", cnt);

		for (j = 0; (j < 4) && (cnt + j) < total; ++j)
		  switch (data [cnt + j])
		    {
		    case 0:
		      fputs (_("   0 (*local*)    "), stdout);
		      break;

		    case 1:
		      fputs (_("   1 (*global*)   "), stdout);
		      break;

		    default:
		      nn = printf ("%4x%c", data [cnt + j] & 0x7fff,
				   data [cnt + j] & 0x8000 ? 'h' : ' ');

		      if (symbols [cnt + j].st_shndx < SHN_LORESERVE
			  && section_headers[symbols [cnt + j].st_shndx].sh_type
			  == SHT_NOBITS)
			{
			  /* We must test both.  */
			  Elf_Internal_Verneed     ivn;
			  unsigned long            offset;

			  offset = version_info [DT_VERSIONTAGIDX (DT_VERNEED)]
			    - loadaddr;

			  do
			    {
			      Elf_External_Verneed   evn;
			      Elf_External_Vernaux   evna;
			      Elf_Internal_Vernaux     ivna;
			      unsigned long            vna_off;

			      GET_DATA (offset, evn, "version need");

			      ivn.vn_aux  = BYTE_GET (evn.vn_aux);
			      ivn.vn_next = BYTE_GET (evn.vn_next);

			      vna_off = offset + ivn.vn_aux;

			      do
				{
				  GET_DATA (vna_off, evna,
					    "version need aux (1)");

				  ivna.vna_next  = BYTE_GET (evna.vna_next);
				  ivna.vna_other = BYTE_GET (evna.vna_other);

				  vna_off += ivna.vna_next;
				}
			      while (ivna.vna_other != data [cnt + j]
				     && ivna.vna_next != 0);

			      if (ivna.vna_other == data [cnt + j])
				{
				  ivna.vna_name = BYTE_GET (evna.vna_name);

				  nn += printf ("(%s%-*s",
						strtab + ivna.vna_name,
						12 - strlen (strtab
							     + ivna.vna_name),
						")");
				  break;
				}
			      else if (ivn.vn_next == 0)
				{
				  if (data [cnt + j] != 0x8001)
				    {
				      Elf_Internal_Verdef  ivd;
				      Elf_External_Verdef  evd;

				      offset = version_info
					[DT_VERSIONTAGIDX (DT_VERDEF)]
					- loadaddr;

				      do
					{
					  GET_DATA (offset, evd,
						    "version definition");

					  ivd.vd_next = BYTE_GET (evd.vd_next);
					  ivd.vd_ndx  = BYTE_GET (evd.vd_ndx);

					  offset += ivd.vd_next;
					}
				      while (ivd.vd_ndx
					     != (data [cnt + j] & 0x7fff)
					     && ivd.vd_next != 0);

				      if (ivd.vd_ndx
					  == (data [cnt + j] & 0x7fff))
					{
					  Elf_External_Verdaux  evda;
					  Elf_Internal_Verdaux  ivda;

					  ivd.vd_aux = BYTE_GET (evd.vd_aux);

					  GET_DATA (offset + ivd.vd_aux, evda,
						    "version definition aux");

					  ivda.vda_name =
					    BYTE_GET (evda.vda_name);

					  nn +=
					    printf ("(%s%-*s",
						    strtab + ivda.vda_name,
						    12
						    - strlen (strtab
							      + ivda.vda_name),
						    ")");
					}
				    }

				  break;
				}
			      else
				offset += ivn.vn_next;
			    }
			  while (ivn.vn_next);
			}
		      else if (symbols [cnt + j].st_shndx == SHN_UNDEF)
			{
			  Elf_Internal_Verneed     ivn;
			  unsigned long            offset;

			  offset = version_info [DT_VERSIONTAGIDX (DT_VERNEED)]
			    - loadaddr;

		          do
			    {
			      Elf_Internal_Vernaux     ivna;
			      Elf_External_Verneed   evn;
			      Elf_External_Vernaux   evna;
			      unsigned long            a_off;

			      GET_DATA (offset, evn, "version need");

			      ivn.vn_aux  = BYTE_GET (evn.vn_aux);
			      ivn.vn_next = BYTE_GET (evn.vn_next);

			      a_off = offset + ivn.vn_aux;

			      do
				{
				  GET_DATA (a_off, evna,
					    "version need aux (2)");

				  ivna.vna_next  = BYTE_GET (evna.vna_next);
				  ivna.vna_other = BYTE_GET (evna.vna_other);

				  a_off += ivna.vna_next;
				}
			      while (ivna.vna_other != data [cnt + j]
				     && ivna.vna_next != 0);

			      if (ivna.vna_other == data [cnt + j])
				{
				  ivna.vna_name = BYTE_GET (evna.vna_name);

				  nn += printf ("(%s%-*s",
						strtab + ivna.vna_name,
						12 - strlen (strtab
							     + ivna.vna_name),
						")");
				  break;
				}

			      offset += ivn.vn_next;
			    }
			  while (ivn.vn_next);
			}
		      else if (data [cnt + j] != 0x8001)
			{
			  Elf_Internal_Verdef  ivd;
			  Elf_External_Verdef  evd;
			  unsigned long        offset;

			  offset = version_info
			    [DT_VERSIONTAGIDX (DT_VERDEF)] - loadaddr;

			  do
			    {
			      GET_DATA (offset, evd, "version def");

			      ivd.vd_next = BYTE_GET (evd.vd_next);
			      ivd.vd_ndx  = BYTE_GET (evd.vd_ndx);

			      offset += ivd.vd_next;
			    }
			  while (ivd.vd_ndx != (data [cnt + j] & 0x7fff)
				 && ivd.vd_next != 0);

			  if (ivd.vd_ndx == (data [cnt + j] & 0x7fff))
			    {
			      Elf_External_Verdaux  evda;
			      Elf_Internal_Verdaux  ivda;

			      ivd.vd_aux = BYTE_GET (evd.vd_aux);

			      GET_DATA (offset - ivd.vd_next + ivd.vd_aux,
					evda, "version def aux");

			      ivda.vda_name = BYTE_GET (evda.vda_name);

			      nn += printf ("(%s%-*s",
					    strtab + ivda.vda_name,
					    12 - strlen (strtab
							 + ivda.vda_name),
					    ")");
			    }
			}

		      if (nn < 18)
			printf ("%*c", 18 - nn, ' ');
		    }

		putchar ('\n');
	      }

	    free (data);
	    free (strtab);
	    free (symbols);
	  }
	break;

	default:
	break;
	}
    }

  if (! found)
    printf (_("\nNo version information found in this file.\n"));

  return 1;
}

static char *
get_symbol_binding (binding)
     unsigned int binding;
{
  static char buff [32];

  switch (binding)
    {
    case STB_LOCAL:  return _("LOCAL");
    case STB_GLOBAL: return _("GLOBAL");
    case STB_WEAK:   return _("WEAK");
    default:
      if (binding >= STB_LOPROC && binding <= STB_HIPROC)
	sprintf (buff, _("<processor specific>: %d"), binding);
      else
	sprintf (buff, _("<unknown>: %d"), binding);
      return buff;
    }
}

static char *
get_symbol_type (type)
     unsigned int type;
{
  static char buff [32];

  switch (type)
    {
    case STT_NOTYPE:   return _("NOTYPE");
    case STT_OBJECT:   return _("OBJECT");
    case STT_FUNC:     return _("FUNC");
    case STT_SECTION:  return _("SECTION");
    case STT_FILE:     return _("FILE");
    default:
      if (type >= STT_LOPROC && type <= STT_HIPROC)
	sprintf (buff, _("<processor specific>: %d"), type);
      else
	sprintf (buff, _("<unknown>: %d"), type);
      return buff;
    }
}

static char *
get_symbol_index_type (type)
     unsigned int type;
{
  switch (type)
    {
    case SHN_UNDEF:  return "UND";
    case SHN_ABS:    return "ABS";
    case SHN_COMMON: return "COM";
    default:
      if (type >= SHN_LOPROC && type <= SHN_HIPROC)
	return "PRC";
      else if (type >= SHN_LORESERVE && type <= SHN_HIRESERVE)
	return "RSV";
      else
	{
	  static char buff [32];

	  sprintf (buff, "%3d", type);
	  return buff;
	}
    }
}


static int *
get_dynamic_data (file, number)
     FILE *       file;
     unsigned int number;
{
  char * e_data;
  int *  i_data;

  e_data = (char *) malloc (number * 4);

  if (e_data == NULL)
    {
      error (_("Out of memory\n"));
      return NULL;
    }

  if (fread (e_data, 4, number, file) != number)
    {
      error (_("Unable to read in dynamic data\n"));
      return NULL;
    }

  i_data = (int *) malloc (number * sizeof (* i_data));

  if (i_data == NULL)
    {
      error (_("Out of memory\n"));
      free (e_data);
      return NULL;
    }

  while (number--)
    i_data [number] = byte_get (e_data + number * 4, 4);

  free (e_data);

  return i_data;
}

/* Dump the symbol table */
static int
process_symbol_table (file)
     FILE * file;
{
  Elf32_Internal_Shdr *   section;
  char   nb [4];
  char   nc [4];
  int    nbuckets;
  int    nchains;
  int *  buckets = NULL;
  int *  chains = NULL;

  if (! do_syms && !do_histogram)
    return 1;

  if (dynamic_info[DT_HASH] && ((do_using_dynamic && dynamic_strings != NULL)
				|| do_histogram))
    {
      if (fseek (file, dynamic_info[DT_HASH] - loadaddr, SEEK_SET))
	{
	  error (_("Unable to seek to start of dynamic information"));
	  return 0;
	}

      if (fread (& nb, sizeof (nb), 1, file) != 1)
	{
	  error (_("Failed to read in number of buckets\n"));
	  return 0;
	}

      if (fread (& nc, sizeof (nc), 1, file) != 1)
	{
	  error (_("Failed to read in number of chains\n"));
	  return 0;
	}

      nbuckets = byte_get (nb, 4);
      nchains  = byte_get (nc, 4);

      buckets = get_dynamic_data (file, nbuckets);
      chains  = get_dynamic_data (file, nchains);

      if (buckets == NULL || chains == NULL)
	return 0;
    }

  if (do_syms
      && dynamic_info[DT_HASH] && do_using_dynamic && dynamic_strings != NULL)
    {
      int    hn;
      int    si;

      printf (_("\nSymbol table for image:\n"));
      printf (_("  Num Buc:    Value  Size   Type   Bind Ot Ndx Name\n"));

      for (hn = 0; hn < nbuckets; hn++)
	{
	  if (! buckets [hn])
	    continue;

	  for (si = buckets [hn]; si; si = chains [si])
	    {
	      Elf_Internal_Sym * psym;

	      psym = dynamic_symbols + si;

	      printf ("  %3d %3d: %8lx %5ld %6s %6s %2d ",
		      si, hn,
		      (unsigned long) psym->st_value,
		      (unsigned long) psym->st_size,
		      get_symbol_type (ELF_ST_TYPE (psym->st_info)),
		      get_symbol_binding (ELF_ST_BIND (psym->st_info)),
		      psym->st_other);

	      printf ("%3.3s", get_symbol_index_type (psym->st_shndx));

	      printf (" %s\n", dynamic_strings + psym->st_name);
	    }
	}
    }
  else if (do_syms && !do_using_dynamic)
    {
      unsigned int     i;

      for (i = 0, section = section_headers;
	   i < elf_header.e_shnum;
	   i++, section++)
	{
	  unsigned int          si;
	  char *                strtab;
	  Elf_Internal_Sym *    symtab;
	  Elf_Internal_Sym *    psym;


	  if (   section->sh_type != SHT_SYMTAB
	      && section->sh_type != SHT_DYNSYM)
	    continue;

	  printf (_("\nSymbol table '%s' contains %d entries:\n"),
		  SECTION_NAME (section),
		  section->sh_size / section->sh_entsize);
	  fputs (_("  Num:    Value  Size Type    Bind   Ot  Ndx Name\n"),
		 stdout);

	  symtab = get_elf_symbols (file, section->sh_offset,
				    section->sh_size / section->sh_entsize);
	  if (symtab == NULL)
	    continue;

	  if (section->sh_link == elf_header.e_shstrndx)
	    strtab = string_table;
	  else
	    {
	      Elf32_Internal_Shdr * string_sec;

	      string_sec = section_headers + section->sh_link;

	      GET_DATA_ALLOC (string_sec->sh_offset, string_sec->sh_size,
			      strtab, char *, "string table");
	    }

	  for (si = 0, psym = symtab;
	       si < section->sh_size / section->sh_entsize;
	       si ++, psym ++)
	    {
	      printf ("  %3d: %8lx %5ld %-7s %-6s %2d ",
		      si,
		      (unsigned long) psym->st_value,
		      (unsigned long) psym->st_size,
		      get_symbol_type (ELF_ST_TYPE (psym->st_info)),
		      get_symbol_binding (ELF_ST_BIND (psym->st_info)),
		      psym->st_other);

	      if (psym->st_shndx == 0)
		fputs (" UND", stdout);
	      else if ((psym->st_shndx & 0xffff) == 0xfff1)
		fputs (" ABS", stdout);
	      else if ((psym->st_shndx & 0xffff) == 0xfff2)
		fputs (" COM", stdout);
	      else
		printf ("%4x", psym->st_shndx);

	      printf (" %s", strtab + psym->st_name);

	      if (section->sh_type == SHT_DYNSYM &&
		  version_info [DT_VERSIONTAGIDX (DT_VERSYM)] != 0)
		{
		  unsigned char   data[2];
		  unsigned short  vers_data;
		  unsigned long   offset;
		  int             is_nobits;
		  int             check_def;

		  offset = version_info [DT_VERSIONTAGIDX (DT_VERSYM)]
		    - loadaddr;

		  GET_DATA (offset + si * sizeof (vers_data), data,
			    "version data");

		  vers_data = byte_get (data, 2);

		  is_nobits = psym->st_shndx < SHN_LORESERVE ?
		    (section_headers [psym->st_shndx].sh_type == SHT_NOBITS)
		    : 0;

		  check_def = (psym->st_shndx != SHN_UNDEF);

		  if ((vers_data & 0x8000) || vers_data > 1)
		    {
		      if (is_nobits || ! check_def)
			{
			  Elf_External_Verneed  evn;
			  Elf_Internal_Verneed  ivn;
			  Elf_Internal_Vernaux  ivna;

			  /* We must test both.  */
			  offset = version_info
			    [DT_VERSIONTAGIDX (DT_VERNEED)] - loadaddr;

			  GET_DATA (offset, evn, "version need");

			  ivn.vn_aux  = BYTE_GET (evn.vn_aux);
			  ivn.vn_next = BYTE_GET (evn.vn_next);

			  do
			    {
			      unsigned long  vna_off;

			      vna_off = offset + ivn.vn_aux;

			      do
				{
				  Elf_External_Vernaux  evna;

				  GET_DATA (vna_off, evna,
					    "version need aux (3)");

				  ivna.vna_other = BYTE_GET (evna.vna_other);
				  ivna.vna_next  = BYTE_GET (evna.vna_next);
				  ivna.vna_name  = BYTE_GET (evna.vna_name);

				  vna_off += ivna.vna_next;
				}
			      while (ivna.vna_other != vers_data
				     && ivna.vna_next != 0);

			      if (ivna.vna_other == vers_data)
				break;

			      offset += ivn.vn_next;
			    }
			  while (ivn.vn_next != 0);

			  if (ivna.vna_other == vers_data)
			    {
			      printf ("@%s (%d)",
				      strtab + ivna.vna_name, ivna.vna_other);
			      check_def = 0;
			    }
			  else if (! is_nobits)
			    error (_("bad dynamic symbol"));
			  else
			    check_def = 1;
			}

		      if (check_def)
			{
			  if (vers_data != 0x8001)
			    {
			      Elf_Internal_Verdef     ivd;
			      Elf_Internal_Verdaux    ivda;
			      Elf_External_Verdaux  evda;
			      unsigned long           offset;

			      offset =
				version_info [DT_VERSIONTAGIDX (DT_VERDEF)]
				- loadaddr;

			      do
				{
				  Elf_External_Verdef   evd;

				  GET_DATA (offset, evd, "version def");

				  ivd.vd_ndx  = BYTE_GET (evd.vd_ndx);
				  ivd.vd_aux  = BYTE_GET (evd.vd_aux);
				  ivd.vd_next = BYTE_GET (evd.vd_next);

				  offset += ivd.vd_next;
				}
			      while (ivd.vd_ndx != (vers_data & 0x7fff)
				     && ivd.vd_next != 0);

			      offset -= ivd.vd_next;
			      offset += ivd.vd_aux;

			      GET_DATA (offset, evda, "version def aux");

			      ivda.vda_name = BYTE_GET (evda.vda_name);

			      if (psym->st_name != ivda.vda_name)
				printf ((vers_data & 0x8000)
					? "@%s" : "@@%s",
					strtab + ivda.vda_name);
			    }
			}
		    }
		}

	      putchar ('\n');
	    }

	  free (symtab);
	  if (strtab != string_table)
	    free (strtab);
	}
    }
  else if (do_syms)
    printf
      (_("\nDynamic symbol information is not available for displaying symbols.\n"));

  if (do_histogram)
    {
      int *lengths;
      int *counts;
      int hn;
      int si;
      int maxlength = 0;

      printf (_("\nHistogram for bucket list length (total of %d buckets):\n"),
	      nbuckets);
      printf (_(" Length  Number\n"));

      lengths = (int *) calloc (nbuckets, sizeof (int));
      if (lengths == NULL)
	{
	  error (_("Out of memory"));
	  return 0;
	}
      for (hn = 0; hn < nbuckets; ++hn)
	{
	  if (! buckets [hn])
	    continue;

	  for (si = buckets[hn]; si; si = chains[si])
	    if (maxlength < ++lengths[hn])
	      maxlength = lengths[hn];
	}

      counts = (int *) calloc (maxlength + 1, sizeof (int));
      if (counts == NULL)
	{
	  error (_("Out of memory"));
	  return 0;
	}

      for (hn = 0; hn < nbuckets; ++hn)
	++counts[lengths[hn]];

      for (si = 0; si <= maxlength; ++si)
	printf ("%7d  %-10d (%5.1f%%)\n",
		si, counts[si], (counts[si] * 100.0) / nbuckets);

      free (counts);
      free (lengths);
    }

  if (buckets != NULL)
    {
      free (buckets);
      free (chains);
    }

  return 1;
}

static int
process_syminfo (file)
     FILE * file;
{
  int i;

  if (dynamic_syminfo == NULL
      || !do_dynamic)
    /* No syminfo, this is ok.  */
    return 1;

  /* There better should be a dynamic symbol section.  */
  if (dynamic_symbols == NULL || dynamic_strings == NULL)
    return 0;

  if (dynamic_addr)
    printf (_("\nDynamic info segment at offset 0x%x contains %d entries:\n"),
	    dynamic_syminfo_offset, dynamic_syminfo_nent);

  printf (_(" Num: Name                           BoundTo     Flags\n"));
  for (i = 0; i < dynamic_syminfo_nent; ++i)
    {
      unsigned short int flags = dynamic_syminfo[i].si_flags;

      printf ("%4d: %-30s ", i,
	      dynamic_strings + dynamic_symbols[i].st_name);

      switch (dynamic_syminfo[i].si_boundto)
	{
	case SYMINFO_BT_SELF:
	  fputs ("SELF       ", stdout);
	  break;
	case SYMINFO_BT_PARENT:
	  fputs ("PARENT     ", stdout);
	  break;
	default:
	  if (dynamic_syminfo[i].si_boundto > 0
	      && dynamic_syminfo[i].si_boundto < dynamic_size)
	    printf ("%-10s ",
		    dynamic_strings
		    + dynamic_segment[dynamic_syminfo[i].si_boundto].d_un.d_val);
	  else
	    printf ("%-10d ", dynamic_syminfo[i].si_boundto);
	  break;
	}

      if (flags & SYMINFO_FLG_DIRECT)
	printf (" DIRECT");
      if (flags & SYMINFO_FLG_PASSTHRU)
	printf (" PASSTHRU");
      if (flags & SYMINFO_FLG_COPY)
	printf (" COPY");
      if (flags & SYMINFO_FLG_LAZYLOAD)
	printf (" LAZYLOAD");

      puts ("");
    }

  return 1;
}

static int
process_section_contents (file)
     FILE * file;
{
  Elf32_Internal_Shdr *    section;
  unsigned int  i;

  if (! do_dump)
    return 1;

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum;
       i ++, section ++)
    {
#ifdef SUPPORT_DISASSEMBLY
      /* See if we need an assembly dump of this section */

      if ((i < NUM_DUMP_SECTS) && (dump_sects[i] & DISASS_DUMP))
	{
	  printf (_("\nAssembly dump of section %s\n"),
		  SECTION_NAME (section));

	  /* XXX -- to be done --- XXX */
	}
#endif
      /* See if we need a hex dump of this section.  */
      if ((i < NUM_DUMP_SECTS) && (dump_sects[i] & HEX_DUMP))
	{
	  int             bytes;
	  int             addr;
	  unsigned char * data;
	  char *          start;

	  bytes = section->sh_size;

	  if (bytes == 0)
	    {
	      printf (_("\nSection %d has no data to dump.\n"), i);
	      continue;
	    }
	  else
	    printf (_("\nHex dump of section '%s':\n"), SECTION_NAME (section));

	  addr  = section->sh_addr;

	  GET_DATA_ALLOC (section->sh_offset, bytes, start, char *,
			  "section data");

	  data = start;

	  while (bytes)
	    {
	      int j;
	      int k;
	      int lbytes;

	      lbytes = (bytes > 16 ? 16 : bytes);

	      printf ("  0x%8.8x ", addr);

	      switch (elf_header.e_ident [EI_DATA])
		{
		case ELFDATA2LSB:
		  for (j = 15; j >= 0; j --)
		    {
		      if (j < lbytes)
			printf ("%2.2x", data [j]);
		      else
			printf ("  ");

		      if (!(j & 0x3))
			printf (" ");
		    }
		  break;

		case ELFDATA2MSB:
		  for (j = 0; j < 16; j++)
		    {
		      if (j < lbytes)
			printf ("%2.2x", data [j]);
		      else
			printf ("  ");

		      if ((j & 3) == 3)
			printf (" ");
		    }
		  break;
		}

	      for (j = 0; j < lbytes; j++)
		{
		  k = data [j];
		  if (k >= ' ' && k < 0x80)
		    printf ("%c", k);
		  else
		    printf (".");
		}

	      putchar ('\n');

	      data  += lbytes;
	      addr  += lbytes;
	      bytes -= lbytes;
	    }

	  free (start);
	}
    }

  return 1;
}

static void
process_mips_fpe_exception (mask)
     int mask;
{
  if (mask)
    {
      int first = 1;
      if (mask & OEX_FPU_INEX)
	fputs ("INEX", stdout), first = 0;
      if (mask & OEX_FPU_UFLO)
	printf ("%sUFLO", first ? "" : "|"), first = 0;
      if (mask & OEX_FPU_OFLO)
	printf ("%sOFLO", first ? "" : "|"), first = 0;
      if (mask & OEX_FPU_DIV0)
	printf ("%sDIV0", first ? "" : "|"), first = 0;
      if (mask & OEX_FPU_INVAL)
	printf ("%sINVAL", first ? "" : "|");
    }
  else
    fputs ("0", stdout);
}

static int
process_mips_specific (file)
     FILE *file;
{
  Elf_Internal_Dyn *    entry;
  size_t liblist_offset = 0;
  size_t liblistno = 0;
  size_t options_offset = 0;

  /* We have a lot of special sections.  Thanks SGI!  */
  if (dynamic_segment == NULL)
    /* No information available.  */
    return 0;

  for (entry = dynamic_segment; entry->d_tag != DT_NULL; ++entry)
    switch (entry->d_tag)
      {
      case DT_MIPS_LIBLIST:
	liblist_offset = entry->d_un.d_val - loadaddr;
	break;
      case DT_MIPS_LIBLISTNO:
	liblistno = entry->d_un.d_val;
	break;
      case DT_MIPS_OPTIONS:
	options_offset = entry->d_un.d_val - loadaddr;
	break;
      default:
	break;
      }

  if (liblist_offset != 0 && liblistno != 0 && do_dynamic)
    {
      Elf32_External_Lib *elib;
      size_t cnt;

      GET_DATA_ALLOC (liblist_offset, liblistno * sizeof (Elf32_External_Lib),
		      elib, Elf32_External_Lib *, "liblist");

      printf ("\nSection '.liblist' contains %d entries:\n", liblistno);
      fputs ("     Library              Time Stamp          Checksum   Version Flags\n",
	     stdout);

      for (cnt = 0; cnt < liblistno; ++cnt)
	{
	  Elf32_Lib liblist;
	  time_t time;
	  char timebuf[20];

	  liblist.l_name = BYTE_GET (elib[cnt].l_name);
	  time = BYTE_GET (elib[cnt].l_time_stamp);
	  liblist.l_checksum = BYTE_GET (elib[cnt].l_checksum);
	  liblist.l_version = BYTE_GET (elib[cnt].l_version);
	  liblist.l_flags = BYTE_GET (elib[cnt].l_flags);

	  strftime (timebuf, 20, "%Y-%m-%dT%H:%M:%S", gmtime (&time));

	  printf ("%3d: %-20s %s %#10lx %-7ld %#lx\n", cnt,
		  dynamic_strings + liblist.l_name, timebuf,
		  liblist.l_checksum, liblist.l_version, liblist.l_flags);
	}

      free (elib);
    }

  if (options_offset != 0)
    {
      Elf_External_Options *eopt;
      Elf_Internal_Shdr *sect = section_headers;
      Elf_Internal_Options *iopt;
      Elf_Internal_Options *option;
      size_t offset;
      int cnt;

      /* Find the section header so that we get the size.  */
      while (sect->sh_type != SHT_MIPS_OPTIONS)
	++sect;

      GET_DATA_ALLOC (options_offset, sect->sh_size, eopt,
		      Elf_External_Options *, "options");

      iopt = (Elf_Internal_Options *) malloc ((sect->sh_size / sizeof (eopt))
					      * sizeof (*iopt));
      if (iopt == NULL)
	{
	  error (_("Out of memory"));
	  return 0;
	}

      offset = cnt = 0;
      option = iopt;
      while (offset < sect->sh_size)
	{
	  Elf_External_Options *eoption;

	  eoption = (Elf_External_Options *) ((char *) eopt + offset);

	  option->kind = BYTE_GET (eoption->kind);
	  option->size = BYTE_GET (eoption->size);
	  option->section = BYTE_GET (eoption->section);
	  option->info = BYTE_GET (eoption->info);

	  offset += option->size;
	  ++option;
	  ++cnt;
	}

      printf (_("\nSection '%s' contains %d entries:\n"),
	      string_table + sect->sh_name, cnt);

      option = iopt;
      while (cnt-- > 0)
	{
	  size_t len;

	  switch (option->kind)
	    {
	    case ODK_NULL:
	      /* This shouldn't happen.  */
	      printf (" NULL       %d %x", option->section, option->info);
	      break;
	    case ODK_REGINFO:
	      printf (" REGINFO    ");
	      if (elf_header.e_machine == EM_MIPS)
		{
		  /* 32bit form.  */
		  Elf32_External_RegInfo *ereg;
		  Elf32_RegInfo reginfo;

		  ereg = (Elf32_External_RegInfo *) (option + 1);
		  reginfo.ri_gprmask = BYTE_GET (ereg->ri_gprmask);
		  reginfo.ri_cprmask[0] = BYTE_GET (ereg->ri_cprmask[0]);
		  reginfo.ri_cprmask[1] = BYTE_GET (ereg->ri_cprmask[1]);
		  reginfo.ri_cprmask[2] = BYTE_GET (ereg->ri_cprmask[2]);
		  reginfo.ri_cprmask[3] = BYTE_GET (ereg->ri_cprmask[3]);
		  reginfo.ri_gp_value = BYTE_GET (ereg->ri_gp_value);

		  printf ("GPR %08lx  GP %ld\n",
			  reginfo.ri_gprmask, reginfo.ri_gp_value);
		  printf ("            CPR0 %08lx  CPR1 %08lx  CPR2 %08lx  CPR3 %08lx\n",
			  reginfo.ri_cprmask[0], reginfo.ri_cprmask[1],
			  reginfo.ri_cprmask[2], reginfo.ri_cprmask[3]);
		}
	      else
		{
		  /* 64 bit form.  */
		  Elf64_External_RegInfo *ereg;
		  Elf64_Internal_RegInfo reginfo;

		  ereg = (Elf64_External_RegInfo *) (option + 1);
		  reginfo.ri_gprmask = BYTE_GET (ereg->ri_gprmask);
		  reginfo.ri_cprmask[0] = BYTE_GET (ereg->ri_cprmask[0]);
		  reginfo.ri_cprmask[1] = BYTE_GET (ereg->ri_cprmask[1]);
		  reginfo.ri_cprmask[2] = BYTE_GET (ereg->ri_cprmask[2]);
		  reginfo.ri_cprmask[3] = BYTE_GET (ereg->ri_cprmask[3]);
		  reginfo.ri_gp_value = BYTE_GET (ereg->ri_gp_value);

		  printf ("GPR %08lx  GP %ld\n",
			  reginfo.ri_gprmask, reginfo.ri_gp_value);
		  printf ("            CPR0 %08lx  CPR1 %08lx  CPR2 %08lx  CPR3 %08lx\n",
			  reginfo.ri_cprmask[0], reginfo.ri_cprmask[1],
			  reginfo.ri_cprmask[2], reginfo.ri_cprmask[3]);
		}
	      ++option;
	      continue;
	    case ODK_EXCEPTIONS:
	      fputs (" EXCEPTIONS fpe_min(", stdout);
	      process_mips_fpe_exception (option->info & OEX_FPU_MIN);
	      fputs (") fpe_max(", stdout);
	      process_mips_fpe_exception ((option->info & OEX_FPU_MAX) >> 8);
	      fputs (")", stdout);

	      if (option->info & OEX_PAGE0)
		fputs (" PAGE0", stdout);
	      if (option->info & OEX_SMM)
		fputs (" SMM", stdout);
	      if (option->info & OEX_FPDBUG)
		fputs (" FPDBUG", stdout);
	      if (option->info & OEX_DISMISS)
		fputs (" DISMISS", stdout);
	      break;
	    case ODK_PAD:
	      fputs (" PAD       ", stdout);
	      if (option->info & OPAD_PREFIX)
		fputs (" PREFIX", stdout);
	      if (option->info & OPAD_POSTFIX)
		fputs (" POSTFIX", stdout);
	      if (option->info & OPAD_SYMBOL)
		fputs (" SYMBOL", stdout);
	      break;
	    case ODK_HWPATCH:
	      fputs (" HWPATCH   ", stdout);
	      if (option->info & OHW_R4KEOP)
		fputs (" R4KEOP", stdout);
	      if (option->info & OHW_R8KPFETCH)
		fputs (" R8KPFETCH", stdout);
	      if (option->info & OHW_R5KEOP)
		fputs (" R5KEOP", stdout);
	      if (option->info & OHW_R5KCVTL)
		fputs (" R5KCVTL", stdout);
	      break;
	    case ODK_FILL:
	      fputs (" FILL       ", stdout);
	      /* XXX Print content of info word?  */
	      break;
	    case ODK_TAGS:
	      fputs (" TAGS       ", stdout);
	      /* XXX Print content of info word?  */
	      break;
	    case ODK_HWAND:
	      fputs (" HWAND     ", stdout);
	      if (option->info & OHWA0_R4KEOP_CHECKED)
		fputs (" R4KEOP_CHECKED", stdout);
	      if (option->info & OHWA0_R4KEOP_CLEAN)
		fputs (" R4KEOP_CLEAN", stdout);
	      break;
	    case ODK_HWOR:
	      fputs (" HWOR      ", stdout);
	      if (option->info & OHWA0_R4KEOP_CHECKED)
		fputs (" R4KEOP_CHECKED", stdout);
	      if (option->info & OHWA0_R4KEOP_CLEAN)
		fputs (" R4KEOP_CLEAN", stdout);
	      break;
	    case ODK_GP_GROUP:
	      printf (" GP_GROUP  %#06x  self-contained %#06x",
		      option->info & OGP_GROUP,
		      (option->info & OGP_SELF) >> 16);
	      break;
	    case ODK_IDENT:
	      printf (" IDENT     %#06x  self-contained %#06x",
		      option->info & OGP_GROUP,
		      (option->info & OGP_SELF) >> 16);
	      break;
	    default:
	      /* This shouldn't happen.  */
	      printf (" %3d ???     %d %x",
		      option->kind, option->section, option->info);
	      break;
	    }

	  len = sizeof (*eopt);
	  while (len < option->size)
	    if (((char *) option)[len] >= ' '
		&& ((char *) option)[len] < 0x7f)
	      printf ("%c", ((char *) option)[len++]);
	    else
	      printf ("\\%03o", ((char *) option)[len++]);

	  fputs ("\n", stdout);
	  ++option;
	}

      free (eopt);
    }

  return 1;
}

static int
process_arch_specific (file)
     FILE *file;
{
  switch (elf_header.e_machine)
    {
    case EM_MIPS:
    case EM_MIPS_RS4_BE:
      return process_mips_specific (file);
      break;
    default:
      break;
    }
  return 1;
}

static int
get_file_header (file)
     FILE * file;
{
  Elf32_External_Ehdr ehdr;

  if (fread (& ehdr, sizeof (ehdr), 1, file) != 1)
    return 0;

  memcpy (elf_header.e_ident, ehdr.e_ident, EI_NIDENT);

  if (elf_header.e_ident [EI_DATA] == ELFDATA2LSB)
    byte_get = byte_get_little_endian;
  else
    byte_get = byte_get_big_endian;

  elf_header.e_entry     = BYTE_GET (ehdr.e_entry);
  elf_header.e_phoff     = BYTE_GET (ehdr.e_phoff);
  elf_header.e_shoff     = BYTE_GET (ehdr.e_shoff);
  elf_header.e_version   = BYTE_GET (ehdr.e_version);
  elf_header.e_flags     = BYTE_GET (ehdr.e_flags);
  elf_header.e_type      = BYTE_GET (ehdr.e_type);
  elf_header.e_machine   = BYTE_GET (ehdr.e_machine);
  elf_header.e_ehsize    = BYTE_GET (ehdr.e_ehsize);
  elf_header.e_phentsize = BYTE_GET (ehdr.e_phentsize);
  elf_header.e_phnum     = BYTE_GET (ehdr.e_phnum);
  elf_header.e_shentsize = BYTE_GET (ehdr.e_shentsize);
  elf_header.e_shnum     = BYTE_GET (ehdr.e_shnum);
  elf_header.e_shstrndx  = BYTE_GET (ehdr.e_shstrndx);

  return 1;
}

static void
process_file (file_name)
     char * file_name;
{
  FILE *       file;
  struct stat  statbuf;
  unsigned int i;

  if (stat (file_name, & statbuf) < 0)
    {
      error (_("Cannot stat input file %s.\n"), file_name);
      return;
    }

  file = fopen (file_name, "rb");
  if (file == NULL)
    {
      error (_("Input file %s not found.\n"), file_name);
      return;
    }

  if (! get_file_header (file))
    {
      error (_("%s: Failed to read file header\n"), file_name);
      fclose (file);
      return;
    }

  /* Initialise per file variables.  */
  for (i = NUM_ELEM (version_info); i--;)
    version_info[i] = 0;

  for (i = NUM_ELEM (dynamic_info); i--;)
    dynamic_info[i] = 0;

  /* Process the file.  */
  if (show_name)
    printf (_("\nFile: %s\n"), file_name);

  if (! process_file_header ())
    {
      fclose (file);
      return;
    }

  process_section_headers (file);

  process_program_headers (file);

  process_dynamic_segment (file);

  process_relocs (file);

  process_symbol_table (file);

  process_syminfo (file);

  process_version_sections (file);

  process_section_contents (file);

  process_arch_specific (file);

  fclose (file);

  if (section_headers)
    {
      free (section_headers);
      section_headers = NULL;
    }

  if (string_table)
    {
      free (string_table);
      string_table = NULL;
    }

  if (dynamic_strings)
    {
      free (dynamic_strings);
      dynamic_strings = NULL;
    }

  if (dynamic_symbols)
    {
      free (dynamic_symbols);
      dynamic_symbols = NULL;
    }

  if (dynamic_syminfo)
    {
      free (dynamic_syminfo);
      dynamic_syminfo = NULL;
    }
}

#ifdef SUPPORT_DISASSEMBLY
/* Needed by the i386 disassembler.  For extra credit, someone could
fix this so that we insert symbolic addresses here, esp for GOT/PLT
symbols */

void
print_address (unsigned int addr, FILE * outfile)
{
  fprintf (outfile,"0x%8.8x", addr);
}

/* Needed by the i386 disassembler. */
void
db_task_printsym (unsigned int addr)
{
  print_address (addr, stderr);
}
#endif

int
main (argc, argv)
     int     argc;
     char ** argv;
{
  parse_args (argc, argv);

  if (optind < (argc - 1))
    show_name = 1;

  while (optind < argc)
    process_file (argv [optind ++]);

  return 0;
}
