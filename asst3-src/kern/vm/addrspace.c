/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct as_regions *as_region_add(struct as_regions *cur, struct as_regions *new, int *code){
	if (cur == NULL){
		new->next = NULL;
		return new;
	}
	if (new->end <= cur->start){
		new->next = cur;
		return new;
	}
	if (new->start >= cur->end){
		cur->next = as_region_add(cur->next, new, code);
		return cur;
	}

	// region overlaps with existing region, return failure
	*code = 1;
	return cur;
}

int as_region_check(struct as_regions *cur, vaddr_t addr, int *r, int *w){
	if (cur == NULL || addr < cur->start) return EFAULT;
	if (addr < cur->end){
		*r = cur->r;
		*w = cur->w;
		return 0;
	}
	return as_region_check(cur->next, addr, r, w);
}

struct as_regions *as_region_copy(struct as_regions *cur, int *code){
	if (cur == NULL) return NULL;
	struct as_regions *new = kmalloc(sizeof(struct as_regions));
	if (new == NULL){
		*code = 1;
		return NULL;
	}
	*new = *cur;
	new->next = as_region_copy(cur->next, code);
	return new;
}

void as_region_free(struct as_regions *cur){
	if (cur == NULL) return;
	as_region_free(cur->next);
	kfree(cur);
}

void as_region_load(struct as_regions *cur, int prepare){
	if (cur == NULL) return;
	if (prepare == 1 && cur->r > 0 && cur->w == 0){
		cur->r_change = 1;
		cur->w = 2;
	}
	else if (prepare == 0 && cur->r_change == 1){
		KASSERT(cur->r > 0 && cur->w > 0);
		cur->r_change = 0;
		cur->w = 0;
	}
	as_region_load(cur->next, prepare);
}

struct addrspace *
as_create(void)
{	
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->page_table = page_table_init();
	if (as->page_table == NULL){
		kfree(as);
		return NULL;
	}
	
	for (int i=0; i<(1<<PAGE_TABLE_SIZE1); i++){
		as->page_table[i] = NULL;
	}

	as->asr = kmalloc(sizeof(struct as_regions));
	if (as->asr == NULL){
		kfree(as->page_table);
		kfree(as);
		return NULL;
	}

	// define invalid region for NULL so that it never becomes valid
	as->asr->start = 0;
	as->asr->end = PAGE_SIZE;
	as->asr->r = 0;
	as->asr->w = 0;
	as->asr->e = 0;
	as->asr->r_change = 0;
	as->asr->region_type = REGION_REGULAR;
	as->asr->next = NULL;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{	
	struct addrspace *newas;

	newas = kmalloc(sizeof(struct addrspace));
	if (newas==NULL) {
		return ENOMEM;
	}

	newas->page_table = page_table_copy(old->page_table);
	if (newas->page_table == NULL){
		return ENOMEM;
	}

	int code = 0;
	newas->asr = as_region_copy(old->asr, &code);
	if (code){
		as_destroy(newas);
		return ENOMEM;
	}

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	page_table_free(as->page_table);

	as_region_free(as->asr);

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	int spl = splhigh();
	for (int i=0; i<NUM_TLB; i++){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	int spl = splhigh();
	for (int i=0; i<NUM_TLB; i++){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	// invalid region, end goes past maximum allowed address space
	if (vaddr + memsize > 0x80000000u){
		return EFAULT;
	}

	struct as_regions *new_asr = kmalloc(sizeof(struct as_regions));
	if (new_asr == NULL){
		return ENOMEM;
	}

	new_asr->start = vaddr;
	new_asr->end = vaddr + memsize;
	new_asr->r = readable;
	new_asr->w = writeable;
	new_asr->e = executable;
	new_asr->r_change = 0;
	new_asr->region_type = REGION_REGULAR;
	new_asr->next = NULL;
	int code = 0;
	as->asr = as_region_add(as->asr, new_asr, &code);

	// region supplied is invalid
	if (code){
		kfree(new_asr);
		return EFAULT;
	}

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{	
	as_region_load(as->asr, 1);
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	as_region_load(as->asr, 0);
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	// from answer on ed (https://edstem.org/courses/5289/discussion/431782) it is a good idea to set executable to 0. 
	int err = as_define_region(as, USERSTACK - STACK_PAGES * PAGE_SIZE, STACK_PAGES * PAGE_SIZE, 4, 2, 0);
	if (err){
		return err;
	}

	// find the first region before the stack (last region in the region linked list)
	// the stack is directly above the heap.
	struct as_regions *prev = NULL;
	struct as_regions *cur = proc_getas()->asr;
	while(cur->next != NULL){
		prev = cur;
		cur = cur->next;
	}
	// there should be at least the NULL region
	KASSERT(prev != NULL);

	// define heap region
	struct as_regions *hr = kmalloc(sizeof(struct as_regions));

	// aligning the heap start
	hr->start = prev->end + (4096 - prev->end % 4096) % 4096;
	hr->end = prev->end + (4096 - prev->end % 4096) % 4096;
	hr->r = 4;
	hr->w = 2;
	hr->e = 0;
	hr->r_change = 0;
	hr->region_type = REGION_HEAP;
	hr->next = cur;
	
	prev->next = hr;

	return 0;
}

int sys_sbrk(int a0, int *retval){
	struct as_regions *hr = proc_getas()->asr;
	while(hr->region_type != REGION_HEAP){
		// there should be a heap region?
		KASSERT(hr != NULL);

		hr = hr->next;
	}
	// there should be a stack region?
	KASSERT(hr->next != NULL);
	*retval = -1;
	if (hr->end + a0 < hr->start || a0 % PAGE_SIZE != 0){
		return EINVAL;
	}
	if (hr->end + a0 > hr->next->start){
		return ENOMEM;
	}
	*retval = hr->end;
	hr->end = hr->end + a0;
	return 0;
}