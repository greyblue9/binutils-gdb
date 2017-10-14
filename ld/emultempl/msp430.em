# This shell script emits a C file. -*- C -*-
# It does some substitutions.
fragment <<EOF
/* This file is is generated by a shell script.  DO NOT EDIT! */

/* Emulate the original gld for the given ${EMULATION_NAME}
   Copyright (C) 2014-2017 Free Software Foundation, Inc.
   Written by Steve Chamberlain steve@cygnus.com
   Extended for the MSP430 by Nick Clifton  nickc@redhat.com

   This file is part of the GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#define TARGET_IS_${EMULATION_NAME}

#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"

#include "ld.h"
#include "getopt.h"
#include "ldmain.h"
#include "ldmisc.h"
#include "ldexp.h"
#include "ldlang.h"
#include "ldfile.h"
#include "ldemul.h"
#include "libiberty.h"
#include <ldgram.h>

enum regions
{
  REGION_NONE = 0,
  REGION_LOWER,
  REGION_UPPER,
  REGION_EITHER = 3,
};

enum either_placement_stage
{
  LOWER_TO_UPPER,
  UPPER_TO_LOWER,
};

enum { ROM, RAM };

static int data_region = REGION_NONE;
static int code_region = REGION_NONE;
static bfd_boolean disable_sec_transformation = FALSE;

#define MAX_PREFIX_LENGTH 7

EOF

# Import any needed special functions and/or overrides.
#
if test -n "$EXTRA_EM_FILE" ; then
  source_em ${srcdir}/emultempl/${EXTRA_EM_FILE}.em
fi

if test x"$LDEMUL_BEFORE_PARSE" != xgld"$EMULATION_NAME"_before_parse; then
fragment <<EOF

static void
gld${EMULATION_NAME}_before_parse (void)
{
#ifndef TARGET_			/* I.e., if not generic.  */
  ldfile_set_output_arch ("`echo ${ARCH}`", bfd_arch_unknown);
#endif /* not TARGET_ */

  /* The MSP430 port *needs* linker relaxtion in order to cope with large
     functions where conditional branches do not fit into a +/- 1024 byte range.  */
  if (!bfd_link_relocatable (&link_info))
    TARGET_ENABLE_RELAXATION;
}

EOF
fi

if test x"$LDEMUL_GET_SCRIPT" != xgld"$EMULATION_NAME"_get_script; then
fragment <<EOF

static char *
gld${EMULATION_NAME}_get_script (int *isfile)
EOF

if test x"$COMPILE_IN" = xyes
then
# Scripts compiled in.

# sed commands to quote an ld script as a C string.
sc="-f stringify.sed"

fragment <<EOF
{
  *isfile = 0;

  if (bfd_link_relocatable (&link_info) && config.build_constructors)
    return
EOF
sed $sc ldscripts/${EMULATION_NAME}.xu                 >> e${EMULATION_NAME}.c
echo '  ; else if (bfd_link_relocatable (&link_info)) return' >> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.xr                 >> e${EMULATION_NAME}.c
echo '  ; else if (!config.text_read_only) return'     >> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.xbn                >> e${EMULATION_NAME}.c
echo '  ; else if (!config.magic_demand_paged) return' >> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.xn                 >> e${EMULATION_NAME}.c
echo '  ; else return'                                 >> e${EMULATION_NAME}.c
sed $sc ldscripts/${EMULATION_NAME}.x                  >> e${EMULATION_NAME}.c
echo '; }'                                             >> e${EMULATION_NAME}.c

else
# Scripts read from the filesystem.

fragment <<EOF
{
  *isfile = 1;

  if (bfd_link_relocatable (&link_info) && config.build_constructors)
    return "ldscripts/${EMULATION_NAME}.xu";
  else if (bfd_link_relocatable (&link_info))
    return "ldscripts/${EMULATION_NAME}.xr";
  else if (!config.text_read_only)
    return "ldscripts/${EMULATION_NAME}.xbn";
  else if (!config.magic_demand_paged)
    return "ldscripts/${EMULATION_NAME}.xn";
  else
    return "ldscripts/${EMULATION_NAME}.x";
}
EOF
fi
fi

if test x"$LDEMUL_PLACE_ORPHAN" != xgld"$EMULATION_NAME"_place_orphan; then
fragment <<EOF

static unsigned int
data_statement_size (lang_data_statement_type *d)
{
  unsigned int size = 0;
  switch (d->type)
    {
    case QUAD:
    case SQUAD:
      size = QUAD_SIZE;
      break;
    case LONG:
      size = LONG_SIZE;
      break;
    case SHORT:
      size = SHORT_SIZE;
      break;
    case BYTE:
      size = BYTE_SIZE;
      break;
    default:
      einfo (_("%P: error: unhandled data_statement size\n"));
      FAIL ();
    }
  return size;
}

/* Helper function for place_orphan that computes the size
   of sections already mapped to the given statement.  */

static bfd_size_type
scan_children (lang_statement_union_type * l)
{
  bfd_size_type amount = 0;

  while (l != NULL)
    {
      switch (l->header.type)
	{
	case lang_input_section_enum:
	  if (l->input_section.section->flags & SEC_ALLOC)
	    amount += l->input_section.section->size;
	  break;

	case lang_constructors_statement_enum:
	case lang_assignment_statement_enum:
	case lang_padding_statement_enum:
	  break;

	case lang_wild_statement_enum:
	  amount += scan_children (l->wild_statement.children.head);
	  break;

	case lang_data_statement_enum:
	  amount += data_statement_size (&l->data_statement);
	  break;

	default:
	  fprintf (stderr, "msp430 orphan placer: unhandled lang type %d\n", l->header.type);
	  break;
	}

      l = l->header.next;
    }

  return amount;
}

/* Place an orphan section.  We use this to put .either sections
   into either their lower or their upper equivalents.  */

static lang_output_section_statement_type *
gld${EMULATION_NAME}_place_orphan (asection * s,
				   const char * secname,
				   int constraint)
{
  char * lower_name;
  char * upper_name;
  char * name;
  char * buf = NULL;
  lang_output_section_statement_type * lower;
  lang_output_section_statement_type * upper;

  if ((s->flags & SEC_ALLOC) == 0)
    return NULL;

  if (bfd_link_relocatable (&link_info))
    return NULL;

  /* If constraints are involved let the linker handle the placement normally.  */
  if (constraint != 0)
    return NULL;

  /* We only need special handling for .either sections.  */
  if (strncmp (secname, ".either.", 8) != 0)
    return NULL;

  /* Skip the .either prefix.  */
  secname += 7;

  /* Compute the names of the corresponding upper and lower
     sections.  If the input section name contains another period,
     only use the part of the name before the second dot.  */
  if (strchr (secname + 1, '.') != NULL)
    {
      buf = name = xstrdup (secname);

      * strchr (name + 1, '.') = 0;
    }
  else
    name = (char *) secname;

  lower_name = concat (".lower", name, NULL);
  upper_name = concat (".upper", name, NULL);

  /* Find the corresponding lower and upper sections.  */
  lower = lang_output_section_find (lower_name);
  upper = lang_output_section_find (upper_name);

  if (lower == NULL && upper == NULL)
    {
      einfo (_("%P: error: no section named %s or %s in linker script\n"),
	     lower_name, upper_name);
      goto end;
    }
  else if (lower == NULL)
    {
      lower = lang_output_section_find (name);
      if (lower == NULL)
	{
	  einfo (_("%P: error: no section named %s in linker script\n"), name);
	  goto end;
	}
    }

  /* Always place orphaned sections in lower.  Optimal placement of either
     sections is performed later, once section sizes have been finalized.  */
  lang_add_section (& lower->children, s, NULL, lower);
 end:
  free (upper_name);
  free (lower_name);
  if (buf)
    free (buf);
  return lower;
}
EOF
fi

fragment <<EOF

static bfd_boolean
change_output_section (lang_statement_union_type ** head,
		       asection *s,
		       lang_output_section_statement_type * new_output_section)
{
  asection *is;
  lang_statement_union_type * prev = NULL;
  lang_statement_union_type * curr;

  curr = *head;
  while (curr != NULL)
    {
      switch (curr->header.type)
	{
	case lang_input_section_enum:
	  is = curr->input_section.section;
	  if (is == s)
	    {
	      s->output_section = NULL;
	      lang_add_section (& (new_output_section->children), s, NULL,
				new_output_section);
	      /* Remove the section from the old output section.  */
	      if (prev == NULL)
		*head = curr->header.next;
	      else
		prev->header.next = curr->header.next;
	      return TRUE;
	    }
	  break;
	case lang_wild_statement_enum:
	  if (change_output_section (&(curr->wild_statement.children.head),
				     s, new_output_section))
	    return TRUE;
	  break;
	default:
	  break;
	}
      prev = curr;
      curr = curr->header.next;
    }
  return FALSE;
}

static void
move_prefixed_section (asection *s, char *new_name,
		       lang_output_section_statement_type * new_output_sec)
{
  s->name = new_name;
  if (s->output_section == NULL)
    lang_add_section (& (new_output_sec->children), s, NULL, new_output_sec);
  else
    {
      lang_output_section_statement_type * curr_output_sec
	= lang_output_section_find (s->output_section->name);
      change_output_section (&(curr_output_sec->children.head), s,
			     new_output_sec);
    }
}

static void
add_region_prefix (bfd *abfd, asection *s,
		   ATTRIBUTE_UNUSED void *unused)
{
  const char *curr_name = bfd_get_section_name (abfd, s);
  char * base_name;
  char * new_input_sec_name = NULL;
  char * new_output_sec_name = NULL;
  int region = REGION_NONE;

  if (strncmp (curr_name, ".text", 5) == 0)
    {
      region = code_region;
      base_name = concat (".text", NULL);
    }
  else if (strncmp (curr_name, ".data", 5) == 0)
    {
      region = data_region;
      base_name = concat (".data", NULL);
    }
  else if (strncmp (curr_name, ".bss", 4) == 0)
    {
      region = data_region;
      base_name = concat (".bss", NULL);
    }
  else if (strncmp (curr_name, ".rodata", 7) == 0)
    {
      region = data_region;
      base_name = concat (".rodata", NULL);
    }
  else
    return;

  switch (region)
    {
    case REGION_NONE:
      break;
    case REGION_UPPER:
      new_input_sec_name = concat (".upper", curr_name, NULL);
      new_output_sec_name = concat (".upper", base_name, NULL);
      lang_output_section_statement_type * upper
	= lang_output_section_find (new_output_sec_name);
      if (upper != NULL)
	{
	  move_prefixed_section (s, new_input_sec_name, upper);
	}
      else
	einfo (_("%P: error: no section named %s in linker script\n"),
	       new_output_sec_name);
      break;
    case REGION_LOWER:
      new_input_sec_name = concat (".lower", curr_name, NULL);
      new_output_sec_name = concat (".lower", base_name, NULL);
      lang_output_section_statement_type * lower
	= lang_output_section_find (new_output_sec_name);
      if (lower != NULL)
	{
	  move_prefixed_section (s, new_input_sec_name, lower);
	}
      else
	einfo (_("%P: error: no section named %s in linker script\n"),
	       new_output_sec_name);
      break;
    case REGION_EITHER:
      s->name = concat (".either", curr_name, NULL);
      break;
    default:
      /* Unreachable.  */
      FAIL ();
      break;
    }
  free (base_name);
  if (new_input_sec_name)
    {
      free (new_input_sec_name);
      free (new_output_sec_name);
    }
}

static void
msp430_elf_after_open (void)
{
  bfd *abfd;

  gld${EMULATION_NAME}_after_open ();

  /* If neither --code-region or --data-region have been passed, do not
     transform sections names.  */
  if ((code_region == REGION_NONE && data_region == REGION_NONE)
      || disable_sec_transformation)
    return;

  for (abfd = link_info.input_bfds; abfd != NULL; abfd = abfd->link.next)
    bfd_map_over_sections (abfd, add_region_prefix, NULL);
}

#define OPTION_CODE_REGION		321
#define OPTION_DATA_REGION		(OPTION_CODE_REGION + 1)
#define OPTION_DISABLE_TRANS		(OPTION_CODE_REGION + 2)

static void
gld${EMULATION_NAME}_add_options
  (int ns, char **shortopts, int nl, struct option **longopts,
   int nrl ATTRIBUTE_UNUSED, struct option **really_longopts ATTRIBUTE_UNUSED)
{
  static const char xtra_short[] = { };

  static const struct option xtra_long[] =
    {
      { "code-region", required_argument, NULL, OPTION_CODE_REGION },
      { "data-region", required_argument, NULL, OPTION_DATA_REGION },
      { "disable-sec-transformation", no_argument, NULL,
	OPTION_DISABLE_TRANS },
      { NULL, no_argument, NULL, 0 }
    };

  *shortopts = (char *) xrealloc (*shortopts, ns + sizeof (xtra_short));
  memcpy (*shortopts + ns, &xtra_short, sizeof (xtra_short));
  *longopts = (struct option *)
    xrealloc (*longopts, nl * sizeof (struct option) + sizeof (xtra_long));
  memcpy (*longopts + nl, &xtra_long, sizeof (xtra_long));
}

static void
gld${EMULATION_NAME}_list_options (FILE * file)
{
  fprintf (file, _("\
  --code-region={either,lower,upper,none}\n\
  \tTransform .text* sections to {either,lower,upper,none}.text* sections.\n\
  --data-region={either,lower,upper,none}\n\
  \tTransform .data*, .rodata* and .bss* sections to\n\
  {either,lower,upper,none}.{bss,data,rodata}* sections\n\
  --disable-sec-transformation\n\
  \tDisable transformation of .{text,data,bss,rodata}* sections to\n\
  \tadd the {either,lower,upper,none} prefixes\n"));
}

static bfd_boolean
gld${EMULATION_NAME}_handle_option (int optc)
{
  switch (optc)
    {
    case OPTION_CODE_REGION:
      if (strcmp (optarg, "upper") == 0)
	code_region = REGION_UPPER;
      else if (strcmp (optarg, "lower") == 0)
	code_region = REGION_LOWER;
      else if (strcmp (optarg, "either") == 0)
	code_region = REGION_EITHER;
      else if (strcmp (optarg, "none") == 0)
	code_region = REGION_NONE;
      else if (strlen (optarg) == 0)
	{
	  einfo (_("%P: --code-region requires an argument: \
		   {upper,lower,either,none}\n"));
	  return FALSE;
	}
      else
	{
	  einfo (_("%P: error: unrecognized argument to --code-region= option: \
		   \"%s\"\n"), optarg);
	  return FALSE;
	}
      break;

    case OPTION_DATA_REGION:
      if (strcmp (optarg, "upper") == 0)
	data_region = REGION_UPPER;
      else if (strcmp (optarg, "lower") == 0)
	data_region = REGION_LOWER;
      else if (strcmp (optarg, "either") == 0)
	data_region = REGION_EITHER;
      else if (strcmp (optarg, "none") == 0)
	data_region = REGION_NONE;
      else if (strlen (optarg) == 0)
	{
	  einfo (_("%P: --data-region requires an argument: \
		   {upper,lower,either,none}\n"));
	  return FALSE;
	}
      else
	{
	  einfo (_("%P: error: unrecognized argument to --data-region= option: \
		   \"%s\"\n"), optarg);
	  return FALSE;
	}
      break;

    case OPTION_DISABLE_TRANS:
      disable_sec_transformation = TRUE;
      break;

    default:
      return FALSE;
    }
  return TRUE;
}

static void
eval_upper_either_sections (bfd *abfd, asection *s, void *data)
{
  const char * base_sec_name;
  const char * curr_name;
  char * either_name;
  int curr_region;

  lang_output_section_statement_type * lower;
  lang_output_section_statement_type * upper;
  static bfd_size_type *lower_size = 0;
  static bfd_size_type *upper_size = 0;
  static bfd_size_type lower_size_rom = 0;
  static bfd_size_type lower_size_ram = 0;
  static bfd_size_type upper_size_rom = 0;
  static bfd_size_type upper_size_ram = 0;

  if ((s->flags & SEC_ALLOC) == 0)
    return;
  if (bfd_link_relocatable (&link_info))
    return;

  base_sec_name = (const char *) data;
  curr_name = bfd_get_section_name (abfd, s);

  /* Only concerned with .either input sections in the upper output section.  */
  either_name = concat (".either", base_sec_name, NULL);
  if (strncmp (curr_name, either_name, strlen (either_name)) != 0
      || strncmp (s->output_section->name, ".upper", 6) != 0)
    goto end;

  lower = lang_output_section_find (concat (".lower", base_sec_name, NULL));
  upper = lang_output_section_find (concat (".upper", base_sec_name, NULL));

  if (upper == NULL || upper->region == NULL)
    goto end;
  else if (lower == NULL)
    lower = lang_output_section_find (base_sec_name);
  if (lower == NULL || lower->region == NULL)
    goto end;

  if (strcmp (base_sec_name, ".text") == 0
      || strcmp (base_sec_name, ".rodata") == 0)
    curr_region = ROM;
  else
    curr_region = RAM;

  if (curr_region == ROM)
    {
      if (lower_size_rom == 0)
	{
	  lower_size_rom = lower->region->current - lower->region->origin;
	  upper_size_rom = upper->region->current - upper->region->origin;
	}
      lower_size = &lower_size_rom;
      upper_size = &upper_size_rom;
    }
  else if (curr_region == RAM)
    {
      if (lower_size_ram == 0)
	{
	  lower_size_ram = lower->region->current - lower->region->origin;
	  upper_size_ram = upper->region->current - upper->region->origin;
	}
      lower_size = &lower_size_ram;
      upper_size = &upper_size_ram;
    }

  /* Move sections in the upper region that would fit in the lower
     region to the lower region.  */
  if (*lower_size + s->size < lower->region->length)
    {
      if (change_output_section (&(upper->children.head), s, lower))
	{
	  *upper_size -= s->size;
	  *lower_size += s->size;
	}
    }
 end:
  free (either_name);
}

static void
eval_lower_either_sections (bfd *abfd, asection *s, void *data)
{
  const char * base_sec_name;
  const char * curr_name;
  char * either_name;
  int curr_region;
  lang_output_section_statement_type * output_sec;
  lang_output_section_statement_type * lower;
  lang_output_section_statement_type * upper;

  static bfd_size_type *lower_size = 0;
  static bfd_size_type lower_size_rom = 0;
  static bfd_size_type lower_size_ram = 0;

  if ((s->flags & SEC_ALLOC) == 0)
    return;
  if (bfd_link_relocatable (&link_info))
    return;

  base_sec_name = (const char *) data;
  curr_name = bfd_get_section_name (abfd, s);

  /* Only concerned with .either input sections in the lower or "default"
     output section i.e. not in the upper output section.  */
  either_name = concat (".either", base_sec_name, NULL);
  if (strncmp (curr_name, either_name, strlen (either_name)) != 0
      || strncmp (s->output_section->name, ".upper", 6) == 0)
    return;

  if (strcmp (base_sec_name, ".text") == 0
      || strcmp (base_sec_name, ".rodata") == 0)
    curr_region = ROM;
  else
    curr_region = RAM;

  output_sec = lang_output_section_find (s->output_section->name);

  /* If the output_section doesn't exist, this has already been reported in
     place_orphan, so don't need to warn again.  */
  if (output_sec == NULL || output_sec->region == NULL)
    goto end;

  /* lower and output_sec might be the same, but in some cases an .either
     section can end up in base_sec_name if it hasn't been placed by
     place_orphan.  */
  lower = lang_output_section_find (concat (".lower", base_sec_name, NULL));
  upper = lang_output_section_find (concat (".upper", base_sec_name, NULL));
  if (upper == NULL)
    goto end;

  if (curr_region == ROM)
    {
      if (lower_size_rom == 0)
	{
	  /* Get the size of other items in the lower region that aren't the
	     sections to be moved around.  */
	  lower_size_rom
	    = (output_sec->region->current - output_sec->region->origin)
	    - scan_children (output_sec->children.head);
	  if (output_sec != lower && lower != NULL)
	    lower_size_rom -= scan_children (lower->children.head);
	}
      lower_size = &lower_size_rom;
    }
  else if (curr_region == RAM)
    {
      if (lower_size_ram == 0)
	{
	  lower_size_ram
	    = (output_sec->region->current - output_sec->region->origin)
	    - scan_children (output_sec->children.head);
	  if (output_sec != lower && lower != NULL)
	    lower_size_ram -= scan_children (lower->children.head);
	}
      lower_size = &lower_size_ram;
    }
  /* Move sections that cause the lower region to overflow to the upper region.  */
  if (*lower_size + s->size > output_sec->region->length)
    change_output_section (&(output_sec->children.head), s, upper);
  else
    *lower_size += s->size;
 end:
  free (either_name);
}

/* This function is similar to lang_relax_sections, but without the size
   evaluation code that is always executed after relaxation.  */
static void
intermediate_relax_sections (void)
{
  int i = link_info.relax_pass;

  /* The backend can use it to determine the current pass.  */
  link_info.relax_pass = 0;

  while (i--)
    {
      bfd_boolean relax_again;

      link_info.relax_trip = -1;
      do
	{
	  link_info.relax_trip++;

	  lang_do_assignments (lang_assigning_phase_enum);

	  lang_reset_memory_regions ();

	  relax_again = FALSE;
	  lang_size_sections (&relax_again, FALSE);
	}
      while (relax_again);

      link_info.relax_pass++;
    }
}

static void
msp430_elf_after_allocation (void)
{
  int relax_count = 0;
  unsigned int i;
  /* Go over each section twice, once to place either sections that don't fit
     in lower into upper, and then again to move any sections in upper that
     fit in lower into lower.  */
  for (i = 0; i < 8; i++)
    {
      int placement_stage = (i < 4) ? LOWER_TO_UPPER : UPPER_TO_LOWER;
      const char * base_sec_name;
      lang_output_section_statement_type * upper;

      switch (i % 4)
	{
	default:
	case 0:
	  base_sec_name = ".text";
	  break;
	case 1:
	  base_sec_name = ".data";
	  break;
	case 2:
	  base_sec_name = ".bss";
	  break;
	case 3:
	  base_sec_name = ".rodata";
	  break;
	}
      upper = lang_output_section_find (concat (".upper", base_sec_name, NULL));
      if (upper != NULL)
	{
	  /* Can't just use one iteration over the all the sections to make
	     both lower->upper and upper->lower transformations because the
	     iterator encounters upper sections before all lower sections have
	     been examined.  */
	  bfd *abfd;

	  if (placement_stage == LOWER_TO_UPPER)
	    {
	      /* Perform relaxation and get the final size of sections
		 before trying to fit .either sections in the correct
		 ouput sections.  */
	      if (relax_count == 0)
		{
		  intermediate_relax_sections ();
		  relax_count++;
		}
	      for (abfd = link_info.input_bfds; abfd != NULL;
		   abfd = abfd->link.next)
		{
		  bfd_map_over_sections (abfd, eval_lower_either_sections,
					 (void *) base_sec_name);
		}
	    }
	  else if (placement_stage == UPPER_TO_LOWER)
	    {
	      /* Relax again before moving upper->lower.  */
	      if (relax_count == 1)
		{
		  intermediate_relax_sections ();
		  relax_count++;
		}
	      for (abfd = link_info.input_bfds; abfd != NULL;
		   abfd = abfd->link.next)
		{
		  bfd_map_over_sections (abfd, eval_upper_either_sections,
					 (void *) base_sec_name);
		}
	    }

	}
    }
  gld${EMULATION_NAME}_after_allocation ();
}

struct ld_emulation_xfer_struct ld_${EMULATION_NAME}_emulation =
{
  ${LDEMUL_BEFORE_PARSE-gld${EMULATION_NAME}_before_parse},
  ${LDEMUL_SYSLIB-syslib_default},
  ${LDEMUL_HLL-hll_default},
  ${LDEMUL_AFTER_PARSE-after_parse_default},
  msp430_elf_after_open,
  msp430_elf_after_allocation,
  ${LDEMUL_SET_OUTPUT_ARCH-set_output_arch_default},
  ${LDEMUL_CHOOSE_TARGET-ldemul_default_target},
  ${LDEMUL_BEFORE_ALLOCATION-before_allocation_default},
  ${LDEMUL_GET_SCRIPT-gld${EMULATION_NAME}_get_script},
  "${EMULATION_NAME}",
  "${OUTPUT_FORMAT}",
  ${LDEMUL_FINISH-finish_default},
  ${LDEMUL_CREATE_OUTPUT_SECTION_STATEMENTS-NULL},
  ${LDEMUL_OPEN_DYNAMIC_ARCHIVE-NULL},
  ${LDEMUL_PLACE_ORPHAN-gld${EMULATION_NAME}_place_orphan},
  ${LDEMUL_SET_SYMBOLS-NULL},
  ${LDEMUL_PARSE_ARGS-NULL},
  gld${EMULATION_NAME}_add_options,
  gld${EMULATION_NAME}_handle_option,
  ${LDEMUL_UNRECOGNIZED_FILE-NULL},
  gld${EMULATION_NAME}_list_options,
  ${LDEMUL_RECOGNIZED_FILE-NULL},
  ${LDEMUL_FIND_POTENTIAL_LIBRARIES-NULL},
  ${LDEMUL_NEW_VERS_PATTERN-NULL},
  ${LDEMUL_EXTRA_MAP_FILE_TEXT-NULL}
};
EOF
# 
# Local Variables:
# mode: c
# End:
