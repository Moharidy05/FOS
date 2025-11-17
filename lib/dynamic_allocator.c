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
#include <inc/queue.h>

extern int get_page(void* va);
extern void return_page(void* va);


//==================================================================================//
//============================== given functions ===================================//
//==================================================================================//
//==================================
//==================================
// [1] get page va:
//==================================
__inline__ uint32 calculate_page_address(struct PageInfoElement *pageMetadataPtr)
{
	if (pageMetadataPtr < &pageBlockInfoArr[0] || pageMetadataPtr >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE/PAGE_SIZE])
			panic("calculate_page_address called with invalid pageMetadataPtr");
	//get start va of the page from the corresponding page info pointer
	int pageArrayIndex = (pageMetadataPtr - pageBlockInfoArr);
	return dynAllocStart + (pageArrayIndex << PGSHIFT);
}

//==================================
// [2] get page info of page va:
//==================================
__inline__ struct PageInfoElement * extract_page_metadata(uint32 virtualAddress)
{
	int pageArrayIndex = (virtualAddress - dynAllocStart) >> PGSHIFT;
	if (pageArrayIndex < 0 || pageArrayIndex >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("extract_page_metadata called with invalid pa");
	return &pageBlockInfoArr[pageArrayIndex];
}

//==================================================================================//
//============================ required functions ==================================//
//==================================================================================//

//==================================
// [1] initialize dynamic allocator:
//==================================
bool initialization_flag = 0;
void initialize_dynamic_allocator(uint32 allocatorStartAddr, uint32 allocatorEndAddr)
{
	//==================================================================================
	//don't change these lines==========================================================
	//==================================================================================
	{
		assert(allocatorEndAddr <= allocatorStartAddr + DYN_ALLOC_MAX_SIZE);
		initialization_flag = 1;
	}
	//==================================================================================
	//==================================================================================
	//todo: [project'25.gm#1] dynamic allocator - #1 initialize_dynamic_allocator

	// store the boundary addresses
	dynAllocStart = allocatorStartAddr;
	dynAllocEnd = allocatorEndAddr;

	// set up all free block collections
	int totalBlockLists = LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1;
	for (int listCounter = 0; listCounter < totalBlockLists; listCounter++) {
		LIST_INIT(&freeBlockLists[listCounter]);
	}

	// initialize the available pages collection
	LIST_INIT(&freePagesList);

	// block_size = 0 indicates the page is unused
	for (int pageIndex = 0; pageIndex < (DYN_ALLOC_MAX_SIZE / PAGE_SIZE); pageIndex++) {
		pageBlockInfoArr[pageIndex].block_size = 0;
		pageBlockInfoArr[pageIndex].num_of_free_blocks = 0;

		// clear the list node pointers (NULL)
		pageBlockInfoArr[pageIndex].prev_next_info.le_next = NULL;
		pageBlockInfoArr[pageIndex].prev_next_info.le_prev = NULL;
	}

	for (uint32 currentAddr = allocatorStartAddr; currentAddr < allocatorEndAddr; currentAddr += PAGE_SIZE)
	{
		struct PageInfoElement* pageMetadata = extract_page_metadata(currentAddr);
		LIST_INSERT_TAIL(&freePagesList, pageMetadata);
	}
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)

//===========================
// [2] get block size:
//===========================
__inline__ uint32 get_block_size(void *virtualAddress)
{
	//todo: [project'25.gm#1] dynamic allocator - #2 get_block_size

	struct PageInfoElement* pageMetadata = extract_page_metadata((uint32)virtualAddress);

	return pageMetadata->block_size;
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)

//===========================================
// helper functions
//========================================

//========================================
// power of two exponent (LOG base 2)
//========================================
unsigned int compute_binary_exponent(unsigned int value) {
    unsigned int powerCount = 0;
    while (value > 1) {
    	value = value / 2;
        powerCount++;
    }
    return powerCount;
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)

//=========================
// align size to 2^power
//=========================
unsigned int align_to_power_of_two(unsigned int requestedSize) {
    if (requestedSize <= DYN_ALLOC_MIN_BLOCK_SIZE) {
        return DYN_ALLOC_MIN_BLOCK_SIZE;
    }

    unsigned int alignedSize = 1;
    while (alignedSize < requestedSize) {
    	alignedSize = alignedSize * 2;
    }

    return alignedSize;
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)

//==========================
// acquiring the free block
//==========================
struct BlockElement* acquire_available_block(int requiredBlockSize){

	int targetListIndex = compute_binary_exponent(requiredBlockSize / DYN_ALLOC_MIN_BLOCK_SIZE);
	if (!LIST_EMPTY(&freeBlockLists[targetListIndex]))
	{
		struct BlockElement* availableBlock = LIST_FIRST(&freeBlockLists[targetListIndex]);
		LIST_REMOVE(&freeBlockLists[targetListIndex], availableBlock);

		struct PageInfoElement* pageData = extract_page_metadata((uint32)availableBlock);
		pageData->num_of_free_blocks--;
		return availableBlock;
	}
	else
	{
		return NULL;
	}
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)
//===========================
// 3) allocate block:
//===========================
void *alloc_block(uint32 requestedSize)
{
	//==================================================================================
	//don't change these lines==========================================================
	//==================================================================================
	{
		assert(requestedSize <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//todo: [project'25.gm#1] dynamic allocator - #3 alloc_block

	if (requestedSize == 0){
		return NULL;
	}

	unsigned int alignedBlockSize = align_to_power_of_two(requestedSize);
	int targetListIndex = compute_binary_exponent(alignedBlockSize / DYN_ALLOC_MIN_BLOCK_SIZE);

	// allocation cases follows the same logical order in the video of this module of dr ahmed
	// the allocation cases (exact block -> free page -> larger blocks -> failure)

	// case 1 Check for exact size block availability
	if (!LIST_EMPTY(&freeBlockLists[targetListIndex]))
	{
		return acquire_available_block(alignedBlockSize);
	}

	// case 2: No exact block, but free page exists
	else if (!LIST_EMPTY(&freePagesList))
	{
		struct PageInfoElement* newPageMetadata = LIST_FIRST(&freePagesList);
		LIST_REMOVE(&freePagesList, newPageMetadata);
		uint32 newPageAddress = calculate_page_address(newPageMetadata);

		// acquire and initialize the page
		get_page((void*)newPageAddress);
		memset((void*)newPageAddress, 0, PAGE_SIZE);

		// configure page metadata
		newPageMetadata->block_size = alignedBlockSize;
		int blocksPerPage = PAGE_SIZE / alignedBlockSize;
		newPageMetadata->num_of_free_blocks = blocksPerPage;

		// put free list with all blocks from new page
		for (int blockIndex = 0; blockIndex < blocksPerPage; blockIndex++) {
			struct BlockElement* currentBlock = (struct BlockElement*)(newPageAddress + blockIndex * alignedBlockSize);
			LIST_INSERT_HEAD(&freeBlockLists[targetListIndex], currentBlock);
		}

		// obtain one block from the newly prepared page
		return acquire_available_block(alignedBlockSize);
	}//end of this case (DO NOT EDIT IT AT ALL!!!!!, by M)

	// case 3: No exact block or free page, search larger blocks
	else
	{
		int startingListIndex = compute_binary_exponent(alignedBlockSize / DYN_ALLOC_MIN_BLOCK_SIZE);
		int maximumListIndex = LOG2_MAX_SIZE - LOG2_MIN_SIZE;

		// scan larger block lists
		for (int searchIndex = startingListIndex + 1; searchIndex <= maximumListIndex; searchIndex++)
		{
		    if (!LIST_EMPTY(&freeBlockLists[searchIndex]))
		    {
		        // found the block
		        struct BlockElement* largerBlock = LIST_FIRST(&freeBlockLists[searchIndex]);
		        LIST_REMOVE(&freeBlockLists[searchIndex], largerBlock);

		        struct PageInfoElement* pageMeta = extract_page_metadata((uint32)largerBlock);
		        pageMeta->num_of_free_blocks--;

		        return largerBlock;
		    }
		}
	}//end of this case (DO NOT EDIT IT AT ALL!!!!!, by M)

	// case 4: Allocation failure - insufficient memory
	return NULL;
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)

//===========================
// [4] free block:
//===========================
void free_block(void *virtualAddress)
{
	//==================================================================================
	//don't change these lines==========================================================
	//==================================================================================
	{
		assert((uint32)virtualAddress >= dynAllocStart && (uint32)virtualAddress < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//todo: [project'25.gm#1] dynamic allocator - #4 free_block

	// extract metadata and determine block size
	struct PageInfoElement* pageMetadata = extract_page_metadata((uint32)virtualAddress);
	uint32 blockSizeValue = pageMetadata->block_size;

	if (blockSizeValue == 0) {
		panic("free_block: attempting to free invalid block");
	}

	// determine appropriate list index using bit shift
	int listIndex = compute_binary_exponent(blockSizeValue / DYN_ALLOC_MIN_BLOCK_SIZE);

	// return block to free list
	struct BlockElement* blockToRelease = (struct BlockElement*)virtualAddress;
	LIST_INSERT_HEAD(&freeBlockLists[listIndex], blockToRelease);

	// update free block count
	pageMetadata->num_of_free_blocks++;

	// check if page became completely free
	int totalBlocksInPage = PAGE_SIZE / blockSizeValue;
	if (pageMetadata->num_of_free_blocks == totalBlocksInPage) {

		// page is now unused - clean up
		uint32 pageBaseAddress = calculate_page_address(pageMetadata);

		// remove all blocks from this page from free lists
		for (int blockCounter = 0; blockCounter < totalBlocksInPage; blockCounter++) {
			struct BlockElement* currentBlock = (struct BlockElement*)(pageBaseAddress + blockCounter * blockSizeValue);

			if (currentBlock->prev_next_info.le_prev != NULL ||
			    currentBlock->prev_next_info.le_next != NULL ||
			    LIST_FIRST(&freeBlockLists[listIndex]) == currentBlock)
			{
				LIST_REMOVE(&freeBlockLists[listIndex], currentBlock);
			}
		}

		// return page memory to system
		return_page((void*)pageBaseAddress);

		// reset page metadata for reusability
		pageMetadata->block_size = 0;
		pageMetadata->num_of_free_blocks = 0;

		// add page back to available pages
		LIST_INSERT_HEAD(&freePagesList, pageMetadata);
	}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)

//==================================================================================//
//============================== bonus functions ===================================//
//==================================================================================//

//===========================
// [1] reallocate block:
//===========================
void *realloc_block(void* virtualAddress, uint32 newSize)
{
		// null pointer case ( allocate new block )
		if (virtualAddress == NULL) {
			return alloc_block(newSize);
		}

		// zero size case ( free existing block )
		if (newSize == 0) {
			free_block(virtualAddress);
			return NULL;
		}

		// determine current block dimensions
		uint32 currentBlockSize = get_block_size(virtualAddress);

		// same size or smaller  ( return original pointer )
		if (newSize <= currentBlockSize) {
			return virtualAddress;
		}

		// larger size required  ( allocate new block )
		if (newSize > currentBlockSize) {
			void *newBlockPointer = alloc_block(newSize);

			if (newBlockPointer == NULL) {
				return NULL;
			}

			// transfer data from old to new block
			memcpy(newBlockPointer, virtualAddress, currentBlockSize);

			// release old block
			free_block(virtualAddress);

			return newBlockPointer;
		}

		return NULL;
}//end of this function (DO NOT EDIT IT AT ALL!!!!!, by M)
