/*
 * dynamic_allocator.c
 *
 * Created on: Sep 21, 2023
 * Author: HP
 * edited by: the FCIS KB :) on NOV 2025
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"


extern int get_page(void* va);
extern void return_page(void* va);


//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	if (ptrPageInfo < &pageBlockInfoArr[0] || ptrPageInfo >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE/PAGE_SIZE])
			panic("to_page_va called with invalid pageInfoPtr");
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================
// [2] GET PAGE INFO OF PAGE VA:
//==================================
__inline__ struct PageInfoElement * to_page_info(uint32 va)
{
	int idxInPageInfoArr = (va - dynAllocStart) >> PGSHIFT;
	if (idxInPageInfoArr < 0 || idxInPageInfoArr >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("to_page_info called with invalid pa");
	return &pageBlockInfoArr[idxInPageInfoArr];
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [11] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator

	// save the start and end addresses
	dynAllocStart = daStart;
	dynAllocEnd = daEnd;

	// initialize all the free block lists
	int num_lists = LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1;
	for (int i = 0; i < num_lists; i++) {
		LIST_INIT(&freeBlockLists[i]);
	}

	// initialize the list of free pages
	LIST_INIT(&freePagesList);

	// initialize all the page info elements
	// when the block_size equals 0 it mens the page is not used

	for (int i = 0; i < (DYN_ALLOC_MAX_SIZE / PAGE_SIZE); i++) {
		pageBlockInfoArr[i].block_size = 0;
		pageBlockInfoArr[i].num_of_free_blocks = 0;
		// Initialize the list node by setting pointers to NULL
		pageBlockInfoArr[i].prev_next_info.le_next = NULL;
		pageBlockInfoArr[i].prev_next_info.le_prev = NULL;
	}

	for (uint32 va = daStart; va < daEnd; va += PAGE_SIZE)
	{
		struct PageInfoElement* page_info = to_page_info(va);
		LIST_INSERT_TAIL(&freePagesList, page_info);
	}
}//end of this function (DO NOT EDIT IT!, by M)


//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size

	// get the page info for this address
	struct PageInfoElement* page_info = to_page_info((uint32)va);

	// return the block size stored in the page info
	return page_info->block_size;

	//Comment the following line
	//panic("get_block_size() Not implemented yet");
}//end of this function (DO NOT EDIT IT!, by M)

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
    //==================================================================================
    //DON'T CHANGE THESE LINES==========================================================
    //==================================================================================
    {
        assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
    }
    //==================================================================================
    //==================================================================================
    // find the smallest power-of-2 block size that fits the request
    uint32 actual_size = DYN_ALLOC_MIN_BLOCK_SIZE;
    while (actual_size < size) actual_size *= 2;

    // free-list index
    int list_index = 0;
    for (uint32 ts = actual_size; ts > DYN_ALLOC_MIN_BLOCK_SIZE; ts /= 2) list_index++;

    // grab a block from the free list
    if (!LIST_EMPTY(&freeBlockLists[list_index])) {
        struct BlockElement *blk = LIST_FIRST(&freeBlockLists[list_index]);
        LIST_REMOVE(&freeBlockLists[list_index], blk);
        struct PageInfoElement *pi = to_page_info((uint32)blk);
        pi->num_of_free_blocks--;
        return blk;
    }

    struct PageInfoElement *new_page_info = NULL;
    uint32           new_page_va   = 0;

    // pop a page from freePagesList
    if (!LIST_EMPTY(&freePagesList)) {
        new_page_info = LIST_FIRST(&freePagesList);
        LIST_REMOVE(&freePagesList, new_page_info);
        new_page_va = to_page_va(new_page_info);

        // Try to get a physical frame for this page
        if (new_page_info->block_size == 0 && get_page((void *)new_page_va) != 0) {
            // get_page() failed (out of physical memory?)
            // Put the page back on the list and signal failure
            LIST_INSERT_HEAD(&freePagesList, new_page_info);
            new_page_info = NULL;
        }
    }

    // force-reclaim the page with the fewest free blocks
    if (!new_page_info) {
        uint32 min_free = PAGE_SIZE; // Initialize with a value larger than any possible free count
        struct PageInfoElement *victim = NULL;
        for (uint32 va = dynAllocStart; va < dynAllocEnd; va += PAGE_SIZE) {
            struct PageInfoElement *pi = to_page_info(va);
            if (pi->block_size != 0 && pi->num_of_free_blocks < min_free) {
                min_free = pi->num_of_free_blocks;
                victim = pi;
            }
        }

        if (victim) {
            uint32 v_va = to_page_va(victim);
            uint32 bsz  = victim->block_size;
            int v_idx = 0;
            for (uint32 t = bsz; t > DYN_ALLOC_MIN_BLOCK_SIZE; t /= 2) v_idx++;

            // remove all victim blocks from free list
            int total = PAGE_SIZE / bsz;
            for (int i = 0; i < total; i++) {
                struct BlockElement *b = (struct BlockElement *)(v_va + i * bsz);

                if (b->prev_next_info.le_prev != NULL ||
                    b->prev_next_info.le_next != NULL ||
                    LIST_FIRST(&freeBlockLists[v_idx]) == b)
                    LIST_REMOVE(&freeBlockLists[v_idx], b);
            }
            return_page((void *)v_va);
            victim->block_size = 0;
            victim->num_of_free_blocks = 0;

            // re-allocate a physical page for this VA
            if (get_page((void *)v_va) != 0)
                panic("alloc_block: reclaim failed to get a new page");

            new_page_info = victim;
            new_page_va = v_va;
        }
    }


    // if we still have nothing we are out of memory
    if (!new_page_info)
        panic("alloc_block: out of memory");

    // initialise the new page
    new_page_info->block_size = actual_size;
    int num_blocks = PAGE_SIZE / actual_size;
    new_page_info->num_of_free_blocks = num_blocks - 1; // We are about to allocate one

    // put all but one block on the free list
    for (int i = 1; i < num_blocks; i++) {
        struct BlockElement *nb = (struct BlockElement *)(new_page_va + i * actual_size);
        LIST_INSERT_HEAD(&freeBlockLists[list_index], nb);
    }

    // hand the first block to the caller
    return (void *)new_page_va;
}//end of this function (DO NOT EDIT IT!, by M)
//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block

	// get page info and block size
	struct PageInfoElement* page_info = to_page_info((uint32)va);
	uint32 block_size = page_info->block_size;

	if (block_size == 0) {
		// panic when trying to free a block on a page that is already free

		panic("free_block: trying to free invalid block");
	}

	// find the correct list index
	int list_index = 0;
	uint32 temp_size = block_size;
	while (temp_size > DYN_ALLOC_MIN_BLOCK_SIZE) {
		temp_size /= 2;
		list_index++;
	}

	// add the block back to the list
	struct BlockElement* block_to_free = (struct BlockElement*)va;
	LIST_INSERT_HEAD(&freeBlockLists[list_index], block_to_free);

	// increment the free count
	page_info->num_of_free_blocks++;

	// check if the page is now completely free
	int total_blocks = PAGE_SIZE / block_size;
	if (page_info->num_of_free_blocks == total_blocks) {

		// page is free
		// remove all blocks from this page from the free list

		uint32 page_va = to_page_va(page_info);
		for (int i = 0; i < total_blocks; i++) {
			struct BlockElement* block = (struct BlockElement*)(page_va + i * block_size);

			if (block->prev_next_info.le_prev != NULL || block->prev_next_info.le_next != NULL || LIST_FIRST(&freeBlockLists[list_index]) == block)
			{
				LIST_REMOVE(&freeBlockLists[list_index], block);
			}
		}

		// Return the page to the kernel frame allocator
		return_page((void*)page_va);

		// reset the page info so it can be re-used by alloc_block
		page_info->block_size = 0;
		page_info->num_of_free_blocks = 0;

		// add the page back to free pages list
		LIST_INSERT_HEAD(&freePagesList, page_info);
	}

	//Comment the following line
	//panic("free_block() Not implemented yet");

}//end of this function (DO NOT EDIT IT!, by M)


//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
		// if the pointer is null, make a new block
		if (va == NULL) {
			return alloc_block(new_size);
		}

		// if new size is 0, free the block and return null
		if (new_size == 0) {
			free_block(va);
			return NULL;
		}

		// how big the current block is ?
		uint32 old_size = get_block_size(va);

		// if new size is same as old size, return the same pointer
		if (new_size == old_size) {
			return va;
		}

		if (new_size < old_size) {
			return va;
		}

		// if new size is bigger than old size, we need a bigger block
		if (new_size > old_size) {
			void *new_block = alloc_block(new_size);

			if (new_block == NULL) {
				return NULL;
			}

			// copy all the data from old block to new block we copy only the amount that fits in old block
			memcpy(new_block, va, old_size);

			// free the old block don't need it anymore
			free_block(va);

			// returnew block
			return new_block;
		} ///

		// ISA we don't reach this case
		return NULL;

		//Comment the following line
		//panic("realloc_block() Not implemented yet");
}
