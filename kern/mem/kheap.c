#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include "memory_manager.h"
#include "../conc/kspinlock.h"
#include <inc/queue.h>
#include <inc/assert.h>
#include <inc/x86.h>


//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
//Remember to initialize locks (if any)

// [PROJECT'25.GM#2] KERNEL HEAP - Structure for tracking free page-sized blocks
struct FreePageBlock
{
	uint32 start_va;
	uint32 size;
	LIST_ENTRY(FreePageBlock) prev_next_info;
};

// [PROJECT'25.GM#2] KERNEL HEAP - Structure for tracking allocated page-sized blocks
struct PageAlloc
{
	uint32 start_va;
	uint32 size;
	LIST_ENTRY(PageAlloc) prev_next_info;
};

// [PROJECT'25.GM#2] KERNEL HEAP - Lists for page allocator
LIST_HEAD(PageAllocHead, PageAlloc) allocated_page_list;
LIST_HEAD(FreePageBlockHead, FreePageBlock) free_page_list;


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

	// [PROJECT'25.GM#2] KERNEL HEAP - Initialize page allocator lists
	LIST_INIT(&allocated_page_list);
	LIST_INIT(&free_page_list);
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	// using allocate_frame and map_frame
	struct FrameInfo *ptr_frame_info = NULL;
	int ret = allocate_frame(&ptr_frame_info);
	if (ret < 0)
		panic("get_page: allocate_frame failed");

	ret = map_frame(ptr_page_directory, ptr_frame_info, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE);
	if (ret < 0)
	{
		free_frame(ptr_frame_info); // cleans up if map_frame fails
		panic("get_page: map_frame failed");
	}

	// allows kheap_virtual_address to find it later in O(1).
	//in (tst kheap cf kvirtaddr to check le elly ha ytest ba3dy) dont change it!!!!!
	ptr_frame_info->proc = (struct Env*)ROUNDDOWN((uint32)va, PAGE_SIZE);

	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	uint32 rva = ROUNDDOWN((uint32)va, PAGE_SIZE);

	// unmap the virtual address
	unmap_frame(ptr_page_directory, rva);

	invlpg((void*)rva);
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

// helper function to map pages for a page-level allocation
static void map_pages(uint32 start_va, uint32 size)
{
	uint32 va;
	for (va = start_va; va < start_va + size; va += PAGE_SIZE)
	{
		get_page((void*)va);
	}
}

// helper function to unmap pages for a page-level free
static void unmap_pages(uint32 start_va, uint32 size)
{
	uint32 va;
	for (va = start_va; va < start_va + size; va += PAGE_SIZE)
	{
		return_page((void*)va);
	}
}

// helper function to insert and merge a new free page block
static void insert_and_merge_free_page(struct FreePageBlock* new_block)
{
	struct FreePageBlock *entry, *prev_entry = NULL;

	// find the correct sorted position and check for merge with (elly 2blha) previous block
	LIST_FOREACH(entry, &free_page_list)
	{
		if (entry->start_va > new_block->start_va)
			break;
		prev_entry = entry;
	}

	bool merged = 0;

	// checking the merge with previous block
	if (prev_entry != NULL && (prev_entry->start_va + prev_entry->size) == new_block->start_va)
	{
		prev_entry->size += new_block->size;
		free_block(new_block);
		new_block = prev_entry; //  merged block is now the one we check next
		merged = 1;
	}

	// Check merge with next block
	if (entry != NULL && (new_block->start_va + new_block->size) == entry->start_va)
	{
		new_block->size += entry->size;
		LIST_REMOVE(&free_page_list, entry);
		free_block(entry);
	}

	// if not merged with previous block, insert the new block in the sorted position
	if (!merged)
	{
		if (prev_entry != NULL)
			LIST_INSERT_AFTER(&free_page_list, prev_entry, new_block);
		else
			LIST_INSERT_HEAD(&free_page_list, new_block);
	}

	// check if we can shrink the break
	if ((new_block->start_va + new_block->size) == kheapPageAllocBreak)
	{
		kheapPageAllocBreak = new_block->start_va;
		LIST_REMOVE(&free_page_list, new_block);
		free_block(new_block);
	}
}

//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void* kmalloc(unsigned int size)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc

	if (size == 0)
		return NULL;

	// small allocation (block allocator)
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		return alloc_block(size);
	}

	// large allocation (page allocator)
	uint32 aligned_size = ROUNDUP(size, PAGE_SIZE);
	uint32 alloc_start_va;

	// [custom fit = worst fit]
	// find the worst fit (largest) free block
	struct FreePageBlock *best_fit_block = NULL;
	struct FreePageBlock *entry;
	LIST_FOREACH(entry, &free_page_list)
	{
		if (entry->size >= aligned_size)
		{
			if (best_fit_block == NULL || entry->size > best_fit_block->size)
			{
				best_fit_block = entry;
			}
		}
	}


	// if a suitable free block is found
	if (best_fit_block != NULL)
	{
		alloc_start_va = best_fit_block->start_va;

		// remove it from the free list
		LIST_REMOVE(&free_page_list, best_fit_block);

		// if ther is remaining space create a new free block for the remaining
		if (best_fit_block->size > aligned_size)
		{
			struct FreePageBlock* remainder_block = (struct FreePageBlock*)alloc_block(sizeof(struct FreePageBlock));
			if (remainder_block == NULL)
				panic("kmalloc: out of metadata memory!");

			remainder_block->start_va = best_fit_block->start_va + aligned_size;
			remainder_block->size = best_fit_block->size - aligned_size;

			// insert the remained block bak again
			insert_and_merge_free_page(remainder_block);
		}

		free_block(best_fit_block);
	}
	// if no block found allocate from the break
	else
	{
		alloc_start_va = kheapPageAllocBreak;

		if (aligned_size > (KERNEL_HEAP_MAX - alloc_start_va))
		{
			return NULL;
		}

		// increase the break boundaries (hanerfa3ha 4waya 3shan na5od mesa7a extra)
		kheapPageAllocBreak += aligned_size;
	}

	// map the physical pages for the new allocation
	map_pages(alloc_start_va, aligned_size);

	// create metadata for the new allocation
	struct PageAlloc* new_alloc = (struct PageAlloc*)alloc_block(sizeof(struct PageAlloc));
	if (new_alloc == NULL)
		panic("kmalloc: out of metadata memory!");

	new_alloc->start_va = alloc_start_va;
	new_alloc->size = aligned_size;

	// insert into allocated list
	LIST_INSERT_HEAD(&allocated_page_list, new_alloc);

	return (void*)alloc_start_va;

}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree

	if (virtual_address == NULL)
		return;

	uint32 va = (uint32)virtual_address;

	// freeing a page allocation
	if (va >= kheapPageAllocStart)
	{
		// find the metadata for this allocation
		struct PageAlloc *alloc_entry = NULL;
		LIST_FOREACH(alloc_entry, &allocated_page_list)
		{
			if (alloc_entry->start_va == va)
				break;
		}

		// if not found
		if (alloc_entry == NULL)
			panic("kfree: attempt to free invalid page-level address!");

		// get size
		uint32 size = alloc_entry->size;
		// unmap physical page
		unmap_pages(va, size);

		// remove from allocated list,
		// and free metadata
		LIST_REMOVE(&allocated_page_list, alloc_entry);
		free_block(alloc_entry);


		// create a new free block metadata
		struct FreePageBlock* new_free_block = (struct FreePageBlock*)alloc_block(sizeof(struct FreePageBlock));
		if (new_free_block == NULL)
			panic("kfree: out of metadata memory!");

		new_free_block->start_va = va;
		new_free_block->size = size;

		// insert into free list
		insert_and_merge_free_page(new_free_block);
	}
	// freeing a block allocation
	else if (va >= KERNEL_HEAP_START && va < kheapPageAllocStart)
	{
		free_block(virtual_address);
	}
	// invalid address
	else
	{
		panic("kfree: attempt to free address outside kernel heap!");
	}
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address

	// convert physical address to pointer [O(1)]
	struct FrameInfo *ptr_frame_info = to_frame_info(physical_address);

	// Check if the frame is valid and currently allocated
	// (if references == 0 then it's a free frame so it has no valid VA)
	if (ptr_frame_info->references == 0)
	{
		return 0;
	}

	// retrieval of the Virtual Address

	uint32 va_page = (uint32)ptr_frame_info->proc;

	// if va_page is 0, the mapping wasn't recorded
	if (va_page == 0)
	{
		return 0;
	}

	// calculate the offset within the page (lower 12 bits of PA)
	uint32 offset = physical_address & 0xFFF;

	// page start VA + offset
	return va_page + offset;
}
//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address
	// implementation [O(1)]
	uint32 *pte_ptr = NULL;
	get_page_table(ptr_page_directory, virtual_address, &pte_ptr);

	if (pte_ptr == NULL)
	{
		return 0; // no page table
	}

	uint32 pte = pte_ptr[PTX(virtual_address)];

	if ((pte & PERM_PRESENT) == 0)
	{
		return 0; // page not present
	}

	uint32 pa_page = pte & 0xFFFFF000;
	uint32 pa_offset = virtual_address & 0x00000FFF;

	return pa_page + pa_offset;

}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc

	// call with (virtual_address = null) = kmalloc()
	//(sort of it does the same job 34an tefham mnen ywady 3la feen :) )
	if (virtual_address == NULL)
	{
		return kmalloc(new_size);
	}

	// A call with (new_size = zero) = kfree() (sort of it does the same job)
	if (new_size == 0)
	{
		kfree(virtual_address);
		return NULL;
	}

	uint32 va = (uint32)virtual_address;
	uint32 old_size;


	// in case of block allocation
	if (va >= KERNEL_HEAP_START && va < kheapPageAllocStart)
	{
		old_size = get_block_size(virtual_address);
	}
	// in case of page allocation
	else if (va >= kheapPageAllocStart)
	{
		struct PageAlloc *alloc_entry = NULL;
		LIST_FOREACH(alloc_entry, &allocated_page_list)
		{
			if (alloc_entry->start_va == va)
			{
				old_size = alloc_entry->size;
				break;
			}
		}
		if (alloc_entry == NULL)
			panic("krealloc: invalid page-level address!");
	}
	else
	{
		panic("krealloc: invalid address!");
	}

	// if new size is same as old size (or fits in the same block/page size)
	if (new_size == old_size)
		return virtual_address;

	// inpage allocations, if new size rounds up to the same old size
	if (va >= kheapPageAllocStart && ROUNDUP(new_size, PAGE_SIZE) == old_size)
		return virtual_address;

	// block allocations
	if (va >= KERNEL_HEAP_START && va < kheapPageAllocStart)
	{
		return realloc_block(virtual_address, new_size);
	}

	// in page allocations we must do a new alloc then copy and free

	void* new_va = kmalloc(new_size);
	if (new_va == NULL)
		return NULL;

	// copy data from old location to new location
	uint32 copy_size = (new_size < old_size) ? new_size : old_size;
	memcpy(new_va, virtual_address, copy_size);

	// free the old allocation
	kfree(virtual_address);

	return new_va;
}
