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
	// when the block_size equals 0, it mens the page is not used
	for (int i = 0; i < (DYN_ALLOC_MAX_SIZE / PAGE_SIZE); i++) {
		pageBlockInfoArr[i].block_size = 0;
		pageBlockInfoArr[i].num_of_free_blocks = 0;
	}

	for (uint32 va = daStart; va < daEnd; va += PAGE_SIZE)
	{
		struct PageInfoElement* page_info = to_page_info(va);
		LIST_INSERT_TAIL(&freePagesList, page_info);
	}
}/*




 */

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
}

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
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block

	// find the smallest power of 2 block size that fits the request
	uint32 actual_size = DYN_ALLOC_MIN_BLOCK_SIZE; // starts at 8
	while (actual_size < size) {
		actual_size *= 2;
	}

	// find the index for the free list
	int list_index = 0;
	uint32 temp_size = actual_size;
	while (temp_size > DYN_ALLOC_MIN_BLOCK_SIZE) {
		temp_size /= 2;
		list_index++;
	}

	// check if there's a free block in the list
	if (!LIST_EMPTY(&freeBlockLists[list_index])) {

		struct BlockElement* free_block = LIST_FIRST(&freeBlockLists[list_index]);
		// *** FIX 1 *** (I passed wrong No of params bec i didn't read the queue.h :( )
		LIST_REMOVE(&freeBlockLists[list_index], free_block);

		// update the page info
		struct PageInfoElement* page_info = to_page_info((uint32)free_block);
		page_info->num_of_free_blocks--;

		return free_block;
	}

	// if no free block, we need a new page
	struct PageInfoElement* new_page_info = NULL;
	uint32 new_page_va = 0;

	// check if we have an empty page ready to use
	if (!LIST_EMPTY(&freePagesList)) {
		new_page_info = LIST_FIRST(&freePagesList);
		// *** FIX 2 *** (I passed wrong No of params in List_remove bec i didn't read the queue.h :( )
		LIST_REMOVE(&freePagesList, new_page_info);
		new_page_va = to_page_va(new_page_info);

		if (new_page_info->block_size == 0)
		{
			if (get_page((void*)new_page_va) != 0) {
				LIST_INSERT_HEAD(&freePagesList, new_page_info);
				return NULL;
			}
		}

	} else {
		// no empty pages so ask the kernel for new page
		// find a va that is not used

		for (uint32 va = dynAllocStart; va < dynAllocEnd; va += PAGE_SIZE) {
			struct PageInfoElement* info = to_page_info(va);
			// block size = 0 means this page is not used by the allocator
			if (info->block_size == 0) {
				// try to get this page from kernel
				if (get_page((void*)va) == 0) {
					new_page_va = va;
					new_page_info = info;
					break;
				}
			}
		}
		return NULL;
	}  /*



	 */

	// if we don't have a page this means that we ran out of memory :(
	if (new_page_info == NULL) {
		return NULL;
	}

	// setup the new page
	new_page_info->block_size = actual_size;
	int num_blocks = PAGE_SIZE / actual_size;
	new_page_info->num_of_free_blocks = num_blocks;

	// add all the new blocks from this page to the free list
	for (int i = 0; i < num_blocks; i++) {
		struct BlockElement* new_block = (struct BlockElement*)(new_page_va + i * actual_size);
		LIST_INSERT_HEAD(&freeBlockLists[list_index], new_block);
	}

	// now we can allocate for sure
	struct BlockElement* free_block = LIST_FIRST(&freeBlockLists[list_index]);
	// *** FIX 3 *** (I passed wrong No of params bec i didn't read the queue.h :( )
	LIST_REMOVE(&freeBlockLists[list_index], free_block);
	new_page_info->num_of_free_blocks--; // update count

	return free_block;

	//Comment the following line
	//panic("alloc_block() Not implemented yet");



} /*




 */
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
		// trying to free a block on a page that is already free?
		// this is bad
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
		// page is all free!
		// remove all blocks from this page from the free list
		uint32 page_va = to_page_va(page_info);
		for (int i = 0; i < total_blocks; i++) {
			struct BlockElement* block = (struct BlockElement*)(page_va + i * block_size);
			// *** FIX 4 *** (I passed wrong No of params bec i didn't read the queue.h :( )
			LIST_REMOVE(&freeBlockLists[list_index], block);
		}

		// add this page to the freePagesList soit can be reused
		LIST_INSERT_HEAD(&freePagesList, page_info);

	}

	//Comment the following line
	//panic("free_block() Not implemented yet");
}/*


 */



//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
		// if the pointer is null, just make a new block
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

			// copy all the data from old block to new block, we copy only the amount that fits in old block
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
