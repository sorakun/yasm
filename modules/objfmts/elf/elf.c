/*
 * ELF object format helpers
 *
 *  Copyright (C) 2003  Michael Urman
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND OTHER CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR OTHER CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <util.h>
/*@unused@*/ RCSID("$IdPath$");

#define YASM_LIB_INTERNAL
#define YASM_EXPR_INTERNAL
#include <libyasm.h>
#define YASM_OBJFMT_ELF_INTERNAL
#include "elf.h"

/* reloc functions */
elf_reloc_entry *
elf_reloc_entry_new(yasm_symrec *sym,
		    elf_address addr,
		    elf_relocation_type rtype)
{
    elf_reloc_entry *entry = yasm_xmalloc(sizeof(elf_reloc_entry));

    if (sym == NULL)
	yasm_internal_error("sym is null");

    entry->sym = sym;
    entry->addr = addr;
    entry->rtype = rtype;

    return entry;
}

void
elf_reloc_entry_delete(elf_reloc_entry *entry)
{
    yasm_xfree(entry);
}

elf_reloc_head *
elf_relocs_new()
{
    elf_reloc_head *head = yasm_xmalloc(sizeof(elf_reloc_head));
    STAILQ_INIT(head);
    return head;
}

void
elf_reloc_delete(elf_reloc_head *relocs)
{
    if (relocs == NULL)
	yasm_internal_error("relocs is null");

    if (!STAILQ_EMPTY(relocs)) {
	elf_reloc_entry *e1, *e2;
	e1 = STAILQ_FIRST(relocs);
	while (e1 != NULL) {
	    e2 = STAILQ_NEXT(e1, qlink);
	    elf_reloc_entry_delete(e1);
	    e1 = e2;
	}
    }
}

/* strtab functions */
elf_strtab_entry *
elf_strtab_entry_new(const char *str)
{
    elf_strtab_entry *entry = yasm_xmalloc(sizeof(elf_strtab_entry));
    entry->str = yasm__xstrdup(str);
    entry->index = 0;
    return entry;
}

elf_strtab_head *
elf_strtab_new()
{
    elf_strtab_head *strtab = yasm_xmalloc(sizeof(elf_strtab_head));
    elf_strtab_entry *entry = yasm_xmalloc(sizeof(elf_strtab_entry));

    STAILQ_INIT(strtab);
    entry->index = 0;
    entry->str = yasm__xstrdup("");

    STAILQ_INSERT_TAIL(strtab, entry, qlink);
    return strtab;
}

elf_strtab_entry *
elf_strtab_append_str(elf_strtab_head *strtab, const char *str)
{
    elf_strtab_entry *last, *entry;

    if (strtab == NULL)
	yasm_internal_error("strtab is null");
    if (STAILQ_EMPTY(strtab))
	yasm_internal_error("strtab is missing initial dummy entry");

    last = STAILQ_LAST(strtab, elf_strtab_entry, qlink);

    entry = elf_strtab_entry_new(str);
    entry->index = last->index + strlen(last->str) + 1;

    STAILQ_INSERT_TAIL(strtab, entry, qlink);
    return entry;
}

void
elf_strtab_delete(elf_strtab_head *strtab)
{
    elf_strtab_entry *s1, *s2;

    if (strtab == NULL)
	yasm_internal_error("strtab is null");
    if (STAILQ_EMPTY(strtab))
	yasm_internal_error("strtab is missing initial dummy entry");

    s1 = STAILQ_FIRST(strtab);
    while (s1 != NULL) {
	s2 = STAILQ_NEXT(s1, qlink);
	yasm_xfree(s1->str);
	yasm_xfree(s1);
	s1 = s2;
    }
    yasm_xfree(strtab);
}

unsigned long
elf_strtab_output_to_file(FILE *f, elf_strtab_head *strtab)
{
    unsigned long size = 0;
    elf_strtab_entry *entry;

    if (strtab == NULL)
	yasm_internal_error("strtab is null");

    /* consider optimizing tables here */
    STAILQ_FOREACH(entry, strtab, qlink) {
	size_t len = 1 + strlen(entry->str);
	fwrite(entry->str, len, 1, f);
	size += len;
    }
    return size;
}



/* symtab functions */
elf_symtab_entry *
elf_symtab_entry_new(elf_strtab_entry *name,
		     yasm_symrec *sym)
{
    elf_symtab_entry *entry = yasm_xmalloc(sizeof(elf_symtab_entry));
    entry->sym = sym;
    entry->sect = NULL;
    entry->name = name;
    entry->value = 0;

    entry->xsize = NULL;
    entry->size = 0;
    entry->index = 0;
    entry->bind = 0;
    entry->type = STT_NOTYPE;

    return entry;
}

void
elf_symtab_entry_delete(elf_symtab_entry *entry)
{
    if (entry == NULL)
	yasm_internal_error("symtab entry is null");

    if (entry->xsize)
	yasm_expr_delete(entry->xsize);
    yasm_xfree(entry);
}

void
elf_symtab_entry_print(FILE *f, int indent_level, elf_symtab_entry *entry)
{
    if (entry == NULL)
	yasm_internal_error("symtab entry is null");

    fprintf(f, "%*sbind=", indent_level, "");
    switch (entry->bind) {
	case STB_LOCAL:		fprintf(f, "local\n");	break;
	case STB_GLOBAL:	fprintf(f, "global\n"); break;
	case STB_WEAK:		fprintf(f, "weak\n");	break;
	default:		fprintf(f, "undef\n");	break;
    }
    fprintf(f, "%*stype=", indent_level, "");
    switch (entry->type) {
	case STT_NOTYPE:	fprintf(f, "notype\n");	break;
	case STT_OBJECT:	fprintf(f, "object\n"); break;
	case STT_FUNC:		fprintf(f, "func\n");	break;
	case STT_SECTION:	fprintf(f, "section\n");break;
	case STT_FILE:		fprintf(f, "file\n");	break;
	default:		fprintf(f, "undef\n");	break;
    }
    fprintf(f, "%*ssize=", indent_level, "");
    if (entry->xsize)
	yasm_expr_print(f, entry->xsize);
    else
	fprintf(f, "%ld", entry->size);
    fprintf(f, "\n");
}

elf_symtab_head *
elf_symtab_new()
{
    elf_symtab_head *symtab = yasm_xmalloc(sizeof(elf_symtab_head));
    elf_symtab_entry *entry = yasm_xmalloc(sizeof(elf_symtab_entry));

    STAILQ_INIT(symtab);
    entry->sym = NULL;
    entry->sect = NULL;
    entry->name = NULL;
    entry->value = 0;
    entry->xsize = NULL;
    entry->size = 0;
    entry->index = SHN_UNDEF;
    entry->bind = 0;
    entry->type = 0;
    entry->symindex = 0;
    STAILQ_INSERT_TAIL(symtab, entry, qlink);
    return symtab;
}

elf_symtab_entry *
elf_symtab_append_entry(elf_symtab_head *symtab, elf_symtab_entry *entry)
{
    if (symtab == NULL)
	yasm_internal_error("symtab is null");
    if (entry == NULL)
	yasm_internal_error("symtab entry is null");
    if (STAILQ_EMPTY(symtab))
	yasm_internal_error(N_("symtab is missing initial dummy entry"));

    STAILQ_INSERT_TAIL(symtab, entry, qlink);
    return entry;
}

elf_symtab_entry *
elf_symtab_insert_local_sym(elf_symtab_head *symtab,
			    elf_strtab_head *strtab,
			    yasm_symrec *sym)
{
    elf_strtab_entry *name = strtab
	? elf_strtab_append_str(strtab, yasm_symrec_get_name(sym))
	: NULL;
    elf_symtab_entry *entry = elf_symtab_entry_new(name, sym);
    elf_symtab_entry *after = STAILQ_FIRST(symtab);
    elf_symtab_entry *before = NULL;

    while (after && (after->bind == STB_LOCAL)) {
	before = after;
	if (before->type == STT_FILE) break;
	after = STAILQ_NEXT(after, qlink);
    }
    STAILQ_INSERT_AFTER(symtab, before, entry, qlink);

    return entry;
}

void
elf_symtab_delete(elf_symtab_head *symtab)
{
    elf_symtab_entry *s1, *s2;

    if (symtab == NULL)
	yasm_internal_error("symtab is null");
    if (STAILQ_EMPTY(symtab))
	yasm_internal_error(N_("symtab is missing initial dummy entry"));

    s1 = STAILQ_FIRST(symtab);
    while (s1 != NULL) {
	s2 = STAILQ_NEXT(s1, qlink);
	elf_symtab_entry_delete(s1);
	s1 = s2;
    }
    yasm_xfree(symtab);
}

unsigned long
elf_symtab_assign_indices(elf_symtab_head *symtab)
{
    elf_symtab_entry *entry, *prev=NULL;
    unsigned long last_local=0;

    if (symtab == NULL)
	yasm_internal_error("symtab is null");
    if (STAILQ_EMPTY(symtab))
	yasm_internal_error(N_("symtab is missing initial dummy entry"));

    STAILQ_FOREACH(entry, symtab, qlink) {
	if (prev)
	    entry->symindex = prev->symindex + 1;
	if (entry->bind == STB_LOCAL)
	    last_local = entry->symindex;
	prev = entry;
    }
    return last_local + 1;
}

unsigned long
elf_symtab_write_to_file(FILE *f, elf_symtab_head *symtab)
{
    unsigned char buf[16], *bufp;
    elf_symtab_entry *entry, *prev;
    unsigned long size = 0;

    if (!symtab)
	yasm_internal_error(N_("symtab is null"));

    prev = NULL;
    STAILQ_FOREACH(entry, symtab, qlink) {

	const yasm_intnum *size_intn;
	bufp = buf;

	/* get size (if specified); expr overrides stored integer */
	if (entry->xsize) {
	    size_intn = yasm_expr_get_intnum(&entry->xsize,
					     yasm_common_calc_bc_dist);
	    if (size_intn)
		entry->size = yasm_intnum_get_uint(size_intn);
	    else
		yasm__error(entry->xsize->line,
			    N_("size specifier not an integer expression"));
	}
	
	/* ? refer to nasty coff_objfmt_output_symtab code - add it */
	YASM_WRITE_32_L(bufp, entry->name ? entry->name->index : 0);
	YASM_WRITE_32_L(bufp, entry->value);
	YASM_WRITE_32_L(bufp, entry->size);
	YASM_WRITE_8(bufp, ELF32_ST_INFO(entry->bind, entry->type));
	YASM_WRITE_8(bufp, 0);
	if (entry->sect) {
	    elf_secthead *shead = yasm_section_get_of_data(entry->sect);
	    if (!shead)
		yasm_internal_error(
		    N_("symbol references section without data"));
	    YASM_WRITE_16_L(bufp, shead->index);
	} else {
	    YASM_WRITE_16_L(bufp, entry->index);
	}
	fwrite(buf, 16, 1, f);
	size += 16;
	prev = entry;
    }
    return size;
}

void elf_symtab_set_nonzero(elf_symtab_entry *entry,
			    yasm_section *sect,
			    elf_section_index sectidx,
			    elf_symbol_binding bind,
			    elf_symbol_type type,
			    yasm_expr *xsize,
			    elf_address value)
{
    if (!entry)
	yasm_internal_error("NULL entry");
    if (sect) entry->sect = sect;
    if (sectidx) entry->index = sectidx;
    if (bind) entry->bind = bind;
    if (type) entry->type = type;
    if (xsize) entry->xsize = xsize;
    if (value) entry->value = value;
}


elf_secthead *
elf_secthead_new(elf_strtab_entry	*name,
		 elf_section_type	 type,
		 elf_section_flags	 flags,
		 elf_section_index	 idx,
		 elf_address		 offset,
		 elf_size		 size)
{
    elf_secthead *esd = yasm_xmalloc(sizeof(elf_secthead));

    esd->type = type;
    esd->flags = flags;
    esd->addr = 0;
    esd->offset = offset;
    esd->size = size;
    esd->link = 0;
    esd->info = 0;
    esd->align = 0;
    esd->entsize = 0;
    esd->index = idx;

    esd->sym = NULL;
    esd->name = name;
    esd->index = 0;
    esd->relocs = NULL;
    esd->rel_name = NULL;
    esd->rel_index = idx;
    esd->rel_offset = 0;
    esd->nreloc = 0;

    if (name && (strcmp(name->str, ".symtab") == 0)) {
	esd->align = 4;
	esd->entsize = 16;
    }

    return esd;
}

void
elf_secthead_delete(elf_secthead *shead)
{
    if (shead == NULL)
	yasm_internal_error(N_("shead is null"));

    if (shead->relocs) {
	elf_reloc_entry *r1, *r2;
	r1 = STAILQ_FIRST(shead->relocs);
	while (r1 != NULL) {
	    r2 = STAILQ_NEXT(r1, qlink);
	    yasm_xfree(r1);
	    r1 = r2;
	}
    }
    yasm_xfree(shead);
}

void elf_secthead_print(FILE *f, int indent_level, elf_secthead *sect)
{
    fprintf(f, "%*sname=%s\n", indent_level, "",
	    sect->name ? sect->name->str : "<undef>");
    fprintf(f, "%*ssym=\n", indent_level, "");
    yasm_symrec_print(f, indent_level+1, sect->sym);
    fprintf(f, "%*sindex=0x%x\n", indent_level, "", sect->index);
    fprintf(f, "%*sflags=", indent_level, "");
    if (sect->flags & SHF_WRITE)
	fprintf(f, "WRITE ");
    if (sect->flags & SHF_ALLOC)
	fprintf(f, "ALLOC ");
    if (sect->flags & SHF_EXECINSTR)
	fprintf(f, "EXEC ");
    /*if (sect->flags & SHF_MASKPROC)
	fprintf(f, "PROC-SPECIFIC"); */
    fprintf(f, "\n%*saddr=0x%lx\n", indent_level, "", sect->addr);
    fprintf(f, "%*soffset=0x%lx\n", indent_level, "", sect->offset);
    fprintf(f, "%*ssize=0x%lx\n", indent_level, "", sect->size);
    fprintf(f, "%*slink=0x%x\n", indent_level, "", sect->link);
    fprintf(f, "%*salign=%ld\n", indent_level, "", sect->align);
    fprintf(f, "%*snreloc=%ld\n", indent_level, "", sect->nreloc);
    if (sect->nreloc) {
	elf_reloc_entry *reloc;

	fprintf(f, "%*sreloc:\n", indent_level, "");
	fprintf(f, "%*sname=%s\n", indent_level+1, "",
		sect->rel_name ? sect->rel_name->str : "<undef>");
	fprintf(f, "%*sindex=0x%x\n", indent_level+1, "", sect->rel_index);
	fprintf(f, "%*soffset=0x%lx\n", indent_level+1, "", sect->rel_offset);
	STAILQ_FOREACH(reloc, sect->relocs, qlink) {
	    fprintf(f, "%*s%s at 0x%lx\n", indent_level+2, "",
		    yasm_symrec_get_name(reloc->sym), reloc->addr);
	}
    }
}

unsigned long
elf_secthead_write_to_file(FILE *f, elf_secthead *shead,
			   elf_section_index sindex)
{
    unsigned char buf[40], *bufp = buf;
    shead->index = sindex;

    if (shead == NULL)
	yasm_internal_error("shead is null");

    YASM_WRITE_32_L(bufp, shead->name ? shead->name->index : 0);
    YASM_WRITE_32_L(bufp, shead->type);
    YASM_WRITE_32_L(bufp, shead->flags);
    YASM_WRITE_32_L(bufp, shead->addr);

    YASM_WRITE_32_L(bufp, shead->offset);
    YASM_WRITE_32_L(bufp, shead->size);
    YASM_WRITE_32_L(bufp, shead->link);
    YASM_WRITE_32_L(bufp, shead->info);

    YASM_WRITE_32_L(bufp, shead->align);
    YASM_WRITE_32_L(bufp, shead->entsize);

    if (fwrite(buf, 40, 1, f))
	return 40;
    yasm_internal_error(N_("Failed to write an elf section header"));
    return 0;
}

int
elf_secthead_append_reloc(elf_secthead *shead, elf_reloc_entry *reloc)
{
    int new_sect = 0;

    if (shead == NULL)
	yasm_internal_error("shead is null");
    if (reloc == NULL)
	yasm_internal_error("reloc is null");

    if (!shead->relocs)
    {
	shead->relocs = elf_relocs_new();
	new_sect = 1;
    }
    shead->nreloc++;
    STAILQ_INSERT_TAIL(shead->relocs, reloc, qlink);

    return new_sect;
}

unsigned long
elf_secthead_write_rel_to_file(FILE *f, elf_section_index symtab_idx,
			       elf_secthead *shead, elf_section_index sindex)
{
    unsigned char buf[40], *bufp = buf;

    if (shead == NULL)
	yasm_internal_error("shead is null");

    if (!shead->relocs)	/* no relocations, no .rel.* section header */
	return 0;

    shead->rel_index = sindex;

    YASM_WRITE_32_L(bufp, shead->rel_name ? shead->rel_name->index : 0);
    YASM_WRITE_32_L(bufp, SHT_REL);
    YASM_WRITE_32_L(bufp, 0);
    YASM_WRITE_32_L(bufp, 0);

    YASM_WRITE_32_L(bufp, shead->rel_offset);
    YASM_WRITE_32_L(bufp, 8 * shead->nreloc);	/* size */
    YASM_WRITE_32_L(bufp, symtab_idx);		/* link: symtab index */
    YASM_WRITE_32_L(bufp, shead->index);	/* info: relocated's index */

    YASM_WRITE_32_L(bufp, 4);			/* align */
    YASM_WRITE_32_L(bufp, 8);			/* entity size */

    if (fwrite(buf, 40, 1, f))
	return 40;
    yasm_internal_error(N_("Failed to write an elf section header"));
    return 0;
}

unsigned long
elf_secthead_write_relocs_to_file(FILE *f, elf_secthead *shead)
{
    elf_reloc_entry *reloc;
    unsigned char buf[8], *bufp;
    unsigned long size = 0;
    long pos;

    if (shead == NULL)
	yasm_internal_error("shead is null");

    if (shead->relocs == NULL || STAILQ_EMPTY(shead->relocs))
	return 0;

    /* first align section to multiple of 4 */
    pos = ftell(f);
    if (pos == -1)
	yasm__error(0, N_("couldn't read position on output stream"));
    pos = (pos + 3) & ~3;
    if (fseek(f, pos, SEEK_SET) < 0)
	yasm__error(0, N_("couldn't seek on output stream"));
    shead->rel_offset = (unsigned long)pos;


    STAILQ_FOREACH(reloc, shead->relocs, qlink) {
	yasm_sym_vis vis;
	unsigned char r_type, r_sym;
	elf_symtab_entry *esym;

	esym = yasm_symrec_get_of_data(reloc->sym);
	if (esym)
	    r_sym = esym->symindex;
	else
	    r_sym = STN_UNDEF;

	vis = yasm_symrec_get_visibility(reloc->sym);
	r_type = R_386_32;
	if (vis & YASM_SYM_EXTERN) {	/* XXX: this can't be why. */
	    r_type = R_386_PC32;
	}

	bufp = buf;
	YASM_WRITE_32_L(bufp, reloc->addr);
	YASM_WRITE_32_L(bufp, ELF32_R_INFO(r_sym, r_type));
	fwrite(buf, 8, 1, f);
	size += 8;
    }
    return size;
}

elf_section_type
elf_secthead_get_type(elf_secthead *shead)
{
    return shead->type;
}

elf_size
elf_secthead_get_size(elf_secthead *shead)
{
    return shead->size;
}

yasm_symrec *
elf_secthead_get_sym(elf_secthead *shead)
{
    return shead->sym;
}

elf_address
elf_secthead_set_addr(elf_secthead *shead, elf_address addr)
{
    return shead->addr = addr;
}

elf_address
elf_secthead_set_align(elf_secthead *shead, elf_address align)
{
    return shead->align = align;
}

elf_section_info
elf_secthead_set_info(elf_secthead *shead, elf_section_info info)
{
    return shead->info = info;
}

elf_section_index
elf_secthead_set_index(elf_secthead *shead, elf_section_index sectidx)
{
    return shead->index = sectidx;
}

elf_section_index
elf_secthead_set_link(elf_secthead *shead, elf_section_index link)
{
    return shead->link = link;
}

elf_section_index
elf_secthead_set_rel_index(elf_secthead *shead, elf_section_index sectidx)
{
    return shead->rel_index = sectidx;
}

elf_strtab_entry *
elf_secthead_set_rel_name(elf_secthead *shead, elf_strtab_entry *entry)
{
    return shead->rel_name = entry;
}

yasm_symrec *
elf_secthead_set_sym(elf_secthead *shead, yasm_symrec *sym)
{
    return shead->sym = sym;
}

unsigned long
elf_secthead_add_size(elf_secthead *shead, unsigned long size)
{
    return shead->size += size;
}

long
elf_secthead_set_file_offset(elf_secthead *shead, long pos)
{
    unsigned long align = shead->align;

    if (align == 0 || align == 1) {
	shead->offset = (unsigned long)pos;
	return pos;
    }
    else if (align & (align - 1))
	yasm_internal_error(
	    N_("alignment %d for section `%s' is not a power of 2"));
	    /*, align, sect->name->str);*/

    shead->offset = (unsigned long)((pos + align - 1) & ~(align - 1));
    return (long)shead->offset;
}

unsigned long
elf_proghead_write_to_file(FILE *f,
			   elf_offset secthead_addr,
			   unsigned long secthead_count,
			   elf_section_index shstrtab_index)
{
    char buf[52], *bufp = buf;

    YASM_WRITE_8(bufp, ELFMAG0);		/* ELF magic number */
    YASM_WRITE_8(bufp, ELFMAG1);
    YASM_WRITE_8(bufp, ELFMAG2);
    YASM_WRITE_8(bufp, ELFMAG3);

    YASM_WRITE_8(bufp, ELFCLASS32);		/* elf class :: 64? */
    YASM_WRITE_8(bufp, ELFDATA2LSB);		/* data encoding :: MSB? */
    YASM_WRITE_8(bufp, EV_CURRENT);		/* elf version */
    while (bufp-buf < EI_NIDENT)		/* e_ident padding */
	YASM_WRITE_8(bufp, 0);

    YASM_WRITE_16_L(bufp, ET_REL);		/* e_type - object file */
    YASM_WRITE_16_L(bufp, EM_386);		/* e_machine - also others */
    YASM_WRITE_32_L(bufp, EV_CURRENT);		/* elf version */
    YASM_WRITE_32_L(bufp, 0);			/* e_entry exection startaddr */
    YASM_WRITE_32_L(bufp, 0);			/* e_phoff program header off */
    YASM_WRITE_32_L(bufp, secthead_addr);	/* e_shoff section header off */
    YASM_WRITE_32_L(bufp, 0);			/* e_flags also by arch */
    YASM_WRITE_16_L(bufp, EHDR_SIZE);		/* e_ehsize */
    YASM_WRITE_16_L(bufp, 0);			/* e_phentsize */
    YASM_WRITE_16_L(bufp, 0);			/* e_phnum */
    YASM_WRITE_16_L(bufp, 40);			/* e_shentsize */
    YASM_WRITE_16_L(bufp, secthead_count);	/* e_shnum */
    YASM_WRITE_16_L(bufp, shstrtab_index);	/* e_shstrndx */

    if (bufp - buf != 52)
	yasm_internal_error(N_("ELF program header is not 52 bytes long"));

    if (fwrite(buf, 52, 1, f))
	return 52;
    yasm_internal_error(N_("Failed to write ELF program header"));
    return 0;
}
