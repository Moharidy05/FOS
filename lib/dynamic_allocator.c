/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
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
uint32 dynAllocEnd;
//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
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
	//Your code is here
	dynAllocEnd = daEnd;

		// iterate over the entire pageblockinfo[] to init all pages
		int total_pages = DYN_ALLOC_MAX_SIZE / PAGE_SIZE;
		for (int i = 0; i < total_pages; i++)
		{
			struct PageInfoElement* pageInfo = &pageBlockInfoArr[i];
			uint32 va = to_page_va(pageInfo);

			// check if the page's VA is within the free range
			if (va >= daStart && va < daEnd)
			{
				// this page is initially free
				pageInfo->ref_count = 0;
				pageInfo->num_pages = 0;
			}
			else
			{
				// this page is outside the free range
				// marked as allocated
				pageInfo->ref_count = 1;
				pageInfo->num_pages = 0;
			}
		}

		//Comment the following line
		//panic("initialize_dynamic_allocator() Not implemented yet");
	}


//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here

	// get the pageinfo for the page containing the VA
		struct PageInfoElement* pageInfo = to_page_info((uint32)va);

		// if the page is free it is not part of any block
		if (pageInfo->ref_count == 0)
		{
			return 0;
		}

		// the start page is the first allocated page that has num_pages > 0.
		struct PageInfoElement* startPage = pageInfo;
		while (startPage > pageBlockInfoArr && startPage->num_pages == 0)
		{
			startPage--;
			// if we scan back and found a free page, the original VA was invalid
			if (startPage->ref_count == 0)
			{
				return 0;
			}
		}

		// Now startPage points to the first page of the block
		return startPage->num_pages * PAGE_SIZE;
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
	//Your code is here
	if (size == 0)
		{
			return NULL;
		}

		//the number of pages needed (ceiling)
		uint32 num_pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;

		//use first-fit to find a contiguous block of free pages
		uint32 free_count = 0;
		struct PageInfoElement* start_page_info = NULL;

		//iterate by VA from the start to the end of the heap
		uint32 va_iterator = dynAllocStart;
		while (va_iterator < dynAllocEnd)
		{
			struct PageInfoElement* current_page_info = to_page_info(va_iterator);

			if (current_page_info->ref_count == 0) // Page is free
			{
				if (free_count == 0) //start of a new potential block(maybe yes maybe no , who knows?)
				{
					start_page_info = current_page_info;
				}
				free_count++;

				if (free_count == num_pages_needed) //a block is found
				{
					//the block is now allocated
					start_page_info->ref_count = 1;
					start_page_info->num_pages = num_pages_needed;

					//mark all pages in this block
					for (int i = 1; i < num_pages_needed; i++)
					{
						struct PageInfoElement* subsequent_page = start_page_info + i;
						subsequent_page->ref_count = 1;
						subsequent_page->num_pages = 0; // Mark as "not a start page"
					}

					return (void*)to_page_va(start_page_info);
				}
			}
			else // resetting the search as now the page is allocated
			{
				free_count = 0;
				start_page_info = NULL;
			}

			va_iterator += PAGE_SIZE;
		}

		// no contiguous block of sufficient size was found so returning a null
		return NULL;
	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

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
	//Your code is here
	//making sure that the VA is page-aligned
		if ((uint32)va % PAGE_SIZE != 0)
		{
			panic("free_block: Address is not page-aligned!");
		}

		struct PageInfoElement* start_page_info = to_page_info((uint32)va);

		//check if it is free
		if (start_page_info->ref_count == 0)
		{
			panic("free_block: Block is already free!");
		}

		//check if the VA is the start of a block.
		// If num_pages is 0, it's a subsequent page which is an invalid VA to free
		if (start_page_info->num_pages == 0)
		{
			panic("free_block: Address is not the start of an allocated block!");
		}

		uint32 num_pages_to_free = start_page_info->num_pages;

		// Iterate and free all pages in this block
		for (int i = 0; i < num_pages_to_free; i++)
		{
			struct PageInfoElement* current_page = start_page_info + i;

			// Safety check for metadata corruption
			if (current_page->ref_count == 0)
			{
				panic("free_block: Block metadata corruption detected!");
			}

			current_page->ref_count = 0;
			current_page->num_pages = 0;
		}
	//Comment the following line
	//panic("free_block() Not implemented yet");
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
	//Your code is here
	// --- exceptional cases ---
		if (va == NULL)
		{
			// if va is nul, realloc() is equivalent to alloc_block()
			return alloc_block(new_size);
		}

		if (new_size == 0)
		{
			// If new_size is 0, realloc() is equivalent to free_block()
			free_block(va);
			return NULL;
		}

		// --- getting the block Info ---
		struct PageInfoElement* start_page_info = to_page_info((uint32)va);
		if (start_page_info->ref_count == 0 || start_page_info->num_pages == 0)
		{
			panic("realloc_block: Invalid va (not start of an allocated block)");
		}

		uint32 old_num_pages = start_page_info->num_pages;
		uint32 old_size = old_num_pages * PAGE_SIZE;
		uint32 new_num_pages = (new_size + PAGE_SIZE - 1) / PAGE_SIZE;

		// --- case 1 shrinking or same size ---
		if (new_num_pages <= old_num_pages)
		{
			// Free the remaining pages that are no longer needed
			for (int i = new_num_pages; i < old_num_pages; i++)
			{
				struct PageInfoElement* page_to_free = start_page_info + i;
				page_to_free->ref_count = 0;
				page_to_free->num_pages = 0;
			}
			// update the page count for the start page
			start_page_info->num_pages = new_num_pages;
			return va;
		}

		// --- ase 2 growing ---
		uint32 pages_to_add = new_num_pages - old_num_pages;

		// check if we can extend the current block (if pages which are after the current are free)
		bool can_extend = 1;
		for (int i = 0; i < pages_to_add; i++)
		{
			struct PageInfoElement* next_page = start_page_info + old_num_pages + i;
			//check if next page is outside the heap or if it is already allocated
			if (to_page_va(next_page) >= dynAllocEnd || next_page->ref_count != 0)
			{
				can_extend = 0;
				break;
			}
		}

		// --- case 3  extending the block in place ---
		if (can_extend)
		{
			// checkthe new pages as allocated
			for (int i = 0; i < pages_to_add; i++)
			{
				struct PageInfoElement* new_page = start_page_info + old_num_pages + i;
				new_page->ref_count = 1;
				new_page->num_pages = 0; // Mark as subsequent page
			}
			// updating the total page count on the start page
			start_page_info->num_pages = new_num_pages;
			return va;
		}

		// --- case moving the block ---
		// ALLOCATION OF a new block of the new size
		void* new_va = alloc_block(new_size);
		if (new_va == NULL)
		{
			// if the aLLOCATION IS FAILED
			return NULL;
		}

		// COPYING THE DATA FROM OLD BLOCK TO NEW BLOCK WIth copying the original size
		memcpy(new_va, va, old_size);

		// fre the old mem block
		free_block(va);

		// return new VA
		return new_va;
	//Comment the following line
	//panic("realloc_block() Not implemented yet");
}
