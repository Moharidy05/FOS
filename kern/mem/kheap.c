#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include <kern/conc/kspinlock.h> // Corrected include path

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================

// Spinlock to protect the kernel heap
struct kspinlock kheap_lock;

//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
//Remember to initialize locks (if any)
void kheap_init()
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		kheapPageAllocBreak = kheapPageAllocStart;
	}
	//==================================================================================
	//==================================================================================

	//Initialize the kernel heap spinlock
	init_kspinlock(&kheap_lock, "kheap_lock");
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void* kmalloc(unsigned int size)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	//Your code is here

	// Check if size exceeds maximum block size
	if (size > DYN_ALLOC_MAX_BLOCK_SIZE) {
		return NULL; // Cannot allocate blocks larger than maximum size
	}

	// 1. Acquire the lock to ensure thread safety
	acquire_kspinlock(&kheap_lock);

	// 2. Call the underlying dynamic allocator
	void* ptr = alloc_block(size);

	// 3. Release the lock
	release_kspinlock(&kheap_lock);

	// 4. Return the allocated pointer
	return ptr;

	//TODO: [PROJECT'25.BONUS#3] FAST PAGE ALLOCATOR
}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	//Your code is here

	// 1. Acquire the lock
	acquire_kspinlock(&kheap_lock);

	// 2. Call the underlying free function
	free_block(virtual_address);

	// 3. Release the lock
	release_kspinlock(&kheap_lock);
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address
	//Your code is here

	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */

	// NOTE: The O(1) implementation requires that 'struct FrameInfo'
	// has a field (e.g., 'va') that stores the virtual address when
	// the page is allocated. Since your 'struct FrameInfo' does not
	// seem to have this field (based on the "va could not be resolved"
	// error), this O(N) implementation is provided as a functional
	// alternative.
	// It scans the kernel heap's virtual address space.

	uint32* pte = NULL;
	unsigned int va;

	// Iterate over the entire dynamic allocator range, page by page
	for (va = KERNEL_HEAP_START; va < (KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE); va += PAGE_SIZE)
	{
		// Get the page table entry for this virtual address
		pte = NULL;

		get_page_table(ptr_page_directory, (uint32)va, &pte);

		// If the page is present...
		if (pte != NULL && (*pte & PERM_PRESENT))
		{
			// Get the physical frame address from the PTE
			unsigned int pa_frame = (*pte & ~0xFFF);

			// Check if this is the physical frame we are looking for
			if (pa_frame == (physical_address & ~0xFFF))
			{
				// Found it. Return the VA with the original offset.
				unsigned int offset = physical_address & 0xFFF;
				return va + offset;
			}
		}
	}

	// Not found
	return 0;
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address
	//Your code is here

	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */

	// 1. Get the Page Table Entry (PTE) for the given virtual address
	// get_page_table is assumed to be a function from memory_manager.h
	uint32* pte = NULL;

	get_page_table(ptr_page_directory, (uint32)virtual_address, &pte);

	// 2. Check if the address is mapped
	if (pte == NULL || (*pte & PERM_PRESENT) == 0)
	{
		return 0; // Not mapped
	}

	// 3. Extract the physical frame address from the PTE
	unsigned int pa_frame = (*pte & ~0xFFF);

	// 4. Get the offset within the page
	unsigned int offset = virtual_address & 0xFFF;

	// 5. Return the full physical address
	return pa_frame + offset;
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc
	//Your code is here

	// Case 1: virtual_address = null is equivalent to kmalloc()
	if (virtual_address == NULL)
	{
		return kmalloc(new_size);
	}

	// Case 2: new_size = zero is equivalent to kfree()
	if (new_size == 0)
	{
		kfree(virtual_address);
		return NULL;
	}

	// Check if new_size exceeds maximum block size
	if (new_size > DYN_ALLOC_MAX_BLOCK_SIZE) {
		return NULL; // Cannot reallocate to size larger than maximum block size
	}

	// Acquire the lock for the resizing operation
	acquire_kspinlock(&kheap_lock);

	// Get the size of the old block
	uint32 old_size = get_block_size(virtual_address);

	// Case 3: Shrinking or staying the same size
	// We can just return the original pointer.
	// A more complex implementation could shrink the block and free the tail,
	// but this is a valid and simple approach.
	if (new_size <= old_size)
	{
		release_kspinlock(&kheap_lock);
		return virtual_address;
	}

	// Case 4: Growing the block (new_size > old_size)
	// We must allocate a new block, copy data, and free the old one.
	// We use the underlying alloc_block/free_block functions
	// to avoid deadlocking on kmalloc/kfree.

	// 1. Allocate a new block
	void* new_ptr = alloc_block(new_size);
	if (new_ptr == NULL)
	{
		// Allocation failed, release lock and return NULL.
		// The old block at virtual_address is still valid.
		release_kspinlock(&kheap_lock);
		return NULL;
	}

	// 2. Copy data from the old block to the new one
	memcpy(new_ptr, virtual_address, old_size);

	// 3. Free the old block
	free_block(virtual_address);

	// 4. Release the lock and return the new pointer
	release_kspinlock(&kheap_lock);
	return new_ptr;
}
