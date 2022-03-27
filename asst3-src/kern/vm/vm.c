#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <proc.h>
#include <spl.h>

/* Place your page table functions here */

page_table_t page_table_init(){
    int ***pt = kmalloc(sizeof(int**) * (1<<PAGE_TABLE_SIZE1));
    if (pt == NULL){
        return NULL;
    }
    return pt;
}

page_table_t page_table_copy(page_table_t old){
    int ***new = kmalloc(sizeof(int**) * (1<<PAGE_TABLE_SIZE1));
    if (new == NULL){
        return NULL;
    }

    // these loops copies the frame table over
	// we always NULL everything pre-emptively in case ENOMEM is returned
	// this is so as_destroy() will know which pointers are valid
	for (int i=0; i<(1<<PAGE_TABLE_SIZE1); i++){
		new[i] = NULL;
	}

	for (int i=0; i<(1<<PAGE_TABLE_SIZE1); i++){
		if (old[i] == NULL){
			continue;
		}
		// allocate second level of page table if needed
		new[i] = kmalloc(sizeof(int *) * (1 << PAGE_TABLE_SIZE2));
		if (new[i] == NULL){
			kfree(new);
			return NULL;
		}

		// set everything in our new pointer to be NULL
		for (int j=0; j<(1<<PAGE_TABLE_SIZE2); j++){
			new[i][j] = NULL;
		}

		for (int j=0; j<(1<<PAGE_TABLE_SIZE2); j++){
			if (old[i][j] == NULL){
				continue;
			}
			// allocate third level of page table if needed
			new[i][j] = kmalloc(sizeof(int) * (1 << PAGE_TABLE_SIZE3));
			if (new[i][j] == NULL){
				kfree(new);
				return NULL;
			}

			// set everything in the pointer to be -1 (unused page)
			// this is to prevent errors with as_destroy 
			for (int k=0; k<(1<<PAGE_TABLE_SIZE3); k++){
				new[i][j][k] = PAGE_TABLE_UNUSED;
			}

			for (int k=0; k<(1<<PAGE_TABLE_SIZE3); k++){
				if (old[i][j][k] == PAGE_TABLE_UNUSED){
					continue;
				}

				// using COW, we just use the same frame and increase the reference count
				new[i][j][k] = old[i][j][k];
				frame_add(new[i][j][k]);
			}
		}
	}

    return new;
}

void page_table_free(page_table_t page_table){
    if (page_table == NULL){
        return;
    }
    for (int i=0; i<(1<<PAGE_TABLE_SIZE1); i++){
		if (page_table[i] == NULL) continue;
		for (int j=0; j<(1<<PAGE_TABLE_SIZE2); j++){
			if (page_table[i][j] == NULL) continue;
			for (int k=0; k<(1<<PAGE_TABLE_SIZE3); k++){
				if (page_table[i][j][k] == PAGE_TABLE_UNUSED) continue;
				
				// free the frame
				free_frame(page_table[i][j][k]);
			}
			kfree(page_table[i][j]);
		}
		kfree(page_table[i]);
	}
}

int page_table_set(page_table_t page_table, int page, int frame){
	int index1 = page >> (PAGE_TABLE_SIZE2 + PAGE_TABLE_SIZE3);
	int index2 = (page >> 6) & ((1 << PAGE_TABLE_SIZE2) - 1);
	int index3 = page & ((1 << PAGE_TABLE_SIZE3) - 1);

	if (page_table[index1] == NULL){
		page_table[index1] = kmalloc(sizeof(int *) * (1 << PAGE_TABLE_SIZE2));
		if (page_table[index1] == NULL) return ENOMEM;
		for (int i=0; i<(1<<PAGE_TABLE_SIZE2); i++){
			page_table[index1][i] = NULL;
		}
	}
	if (page_table[index1][index2] == NULL){
		page_table[index1][index2] = kmalloc(sizeof(int) * (1 << PAGE_TABLE_SIZE3));
		if (page_table[index1][index2] == NULL) return ENOMEM;
		for (int i=0; i<(1<<PAGE_TABLE_SIZE3); i++){
			page_table[index1][index2][i] = PAGE_TABLE_UNUSED;
		}
	}
	page_table[index1][index2][index3] = frame;
	return 0;
}

int page_table_get(page_table_t page_table, int page){
	int index1 = page >> (PAGE_TABLE_SIZE2 + PAGE_TABLE_SIZE3);
	// check: potential error
	int index2 = (page >> 6) & ((1 << PAGE_TABLE_SIZE2) - 1);
	// check: potential error
	int index3 = page & ((1 << PAGE_TABLE_SIZE3) - 1);

	if (page_table[index1] == NULL){
		return PAGE_TABLE_UNUSED;
	}
	if (page_table[index1][index2] == NULL){
		return PAGE_TABLE_UNUSED;
	}
	return page_table[index1][index2][index3];
}

void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
	vaddr_t new_frame = alloc_kpages(1);

	// zero out new frame
	char *kptr = (char*)new_frame;
	for (int i=0; i<PAGE_SIZE; i++){
		kptr[i] = 0;
	}

	zero_frame = KVADDR_TO_PADDR(new_frame) / PAGE_SIZE;

	// we assume allocating the zero frame does not fail
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	
    struct addrspace *as = proc_getas();
    int page = faultaddress / PAGE_SIZE;
	int err = 0;
	int dirty = 0;
    
	int r,w;
	err = as_region_check(as->asr, faultaddress, &r, &w);

	if (err){
		return err;
	}

	// invalid permissions
	if ((faulttype == VM_FAULT_READ && r == 0) || (faulttype == VM_FAULT_WRITE && w == 0) || (faulttype == VM_FAULT_READONLY && w == 0)){
		return EFAULT;
	}

	int frame = page_table_get(as->page_table, page);
    
    if (frame == PAGE_TABLE_UNUSED){
        // set new entry in page table to the zero frame, return error as necessary
        err = page_table_set(as->page_table, page, zero_frame);
        if (err){
            return err;
        }

		// increase reference count in zero_frame
		frame_add(zero_frame);

		// physical frame address
        frame = zero_frame;
    }
	if (faulttype != VM_FAULT_READ){
		int new_frame = get_write_frame(frame);
		if (new_frame == -1){
			return ENOMEM;
		}
		dirty = 1;
		if (new_frame != frame){
			err = page_table_set(as->page_table, page, new_frame);
			if (err){
				free_frame(new_frame);
				return err;
			}
		}
		frame = new_frame;
	}

    // set values of entryhi and entrylo
    uint32_t entryhi, entrylo;
    entryhi = page << 12;
    entrylo = frame << 12;

    // according to comments in tlb.h, we need to set the dirty bit to 1 to allow writes.
	entrylo |= dirty << 10;

    // set valid bit to 1
    entrylo |= 1 << 9;

    // according to comments/ASST3 video, we dont need to worry about other bits of entrylo or entryhi
    
    
	int spl = splhigh();
	if (faulttype == VM_FAULT_READONLY){
		// we need to invalidate the old tlb readonly entry
		int ind = tlb_probe(page * PAGE_SIZE, 0);
		
		// we need an if statement here: it is possible there was a thread switch during this call to vm_fault, 
		//  removing the readonly entry in the tlb. 
		if (ind >=0){
			tlb_write(TLBHI_INVALID(ind), TLBLO_INVALID(), ind);
		}
	}
	// we use tlb_random to load new tlb entries
	tlb_random(entryhi, entrylo);
    splx(spl);

    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}