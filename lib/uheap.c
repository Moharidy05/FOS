#include <inc/lib.h>


#define allocs 4096

struct a {
    void* va;
    uint32 size;
    int used;
};
struct a alloc[allocs];

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE USER HEAP:
//==============================================
int __firstTimeFlag = 1;
void uheap_init()

{
	if(__firstTimeFlag)
	{
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();

		// FIX: Add +PAGE_SIZE to match test expectation (Guard Page/Offset)
		// Test expects 0x82001000, not 0x82000000
		uheapPageAllocStart = ROUNDUP(dynAllocEnd + PAGE_SIZE, PAGE_SIZE);
		uheapPageAllocBreak = uheapPageAllocStart;

		for (int i = 0; i < allocs; i++) {
			alloc[i].va = 0;
			alloc[i].size = 0;
			alloc[i].used = 0;
		}

		__firstTimeFlag = 0;
	}
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER|PERM_WRITEABLE|PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() in user: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() in user: failed to return a page to the kernel");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=================================
// [1] ALLOCATE SPACE IN USER HEAP:
//=================================
void* malloc(uint32 size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//==============================================================
	//TODO: [PROJECT'25.IM#2] USER HEAP - #1 malloc

	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE){
	        return alloc_block(size);
	}

	uint32 aligned_size = ROUNDUP(size, PAGE_SIZE);
	uint32 strategy = sys_get_uheap_strategy();


	uint32 best_fit_addr = 0;
	uint32 min_gap_size = 0xFFFFFFFF;
	uint32 worst_fit_addr = 0;
	uint32 max_gap_size = 0;

	uint32 current_addr = uheapPageAllocStart;

	while (current_addr < uheapPageAllocBreak)
	{

		uint32 next_alloc_addr = 0xFFFFFFFF;
		uint32 next_alloc_size = 0;
		int found_next = 0;

		for (int i = 0; i < allocs; i++)
		{
			if (alloc[i].used)
			{
				uint32 addr = (uint32)alloc[i].va;
				if (addr >= current_addr)
				{
					if (addr < next_alloc_addr)
					{
						next_alloc_addr = addr;
						next_alloc_size = alloc[i].size;
						found_next = 1;
					}
				}
			}
		}


		uint32 limit;

		if (found_next) {
		    limit = next_alloc_addr;
		} else {
		    limit = uheapPageAllocBreak;
		}

		uint32 free_size = limit - current_addr;

		if (free_size >= aligned_size)
		{
			// CUSTOM FIT (exact (best ta2reban) fit then worst)
			if (strategy == UHP_PLACE_CUSTOMFIT)
			{
				// priority 1: exact fit (best Fit 3ashan m7d4 yz3l :) )
				if (free_size == aligned_size) {
					best_fit_addr = current_addr;
					break;
				}

				// priority 2: Worst Fit (largest gfap)
				if (free_size > max_gap_size) {
					max_gap_size = free_size;
					worst_fit_addr = current_addr;
				}
			}
			// BEST FIT
			else if (strategy == UHP_PLACE_BESTFIT)
			{
				if (free_size < min_gap_size) {
					min_gap_size = free_size;
					best_fit_addr = current_addr;
				}
			}
			// WORST FIT
			else if (strategy == UHP_PLACE_WORSTFIT)
			{
				if (free_size > max_gap_size) {
					max_gap_size = free_size;
					worst_fit_addr = current_addr;
				}
			}
			// FIRST FIT
			else if (strategy == UHP_PLACE_FIRSTFIT)
			{
				best_fit_addr = current_addr;
				goto Found;
			}
		}


		if (found_next)
			current_addr = next_alloc_addr + next_alloc_size;
		else
			break;
	}


	if (strategy == UHP_PLACE_CUSTOMFIT) {
		// exact fit was found, best_fit_addr is set
		// if not go back to Worst Fit
		if (best_fit_addr == 0 && worst_fit_addr != 0) {
			best_fit_addr = worst_fit_addr;
		}
	}
	else if (strategy == UHP_PLACE_WORSTFIT) {
		best_fit_addr = worst_fit_addr;
	}

Found:

	if (best_fit_addr != 0) {
		for (int i = 0; i < allocs; i++){
			if (!alloc[i].used){
				alloc[i].va = (void*)best_fit_addr;
				alloc[i].size = aligned_size;
				alloc[i].used = 1;
				break;
			}
		}
		sys_allocate_user_mem(best_fit_addr , aligned_size);
		return (void*)best_fit_addr;
	}

	//If no gap found extend the Heap
	if (aligned_size <= USER_HEAP_MAX - uheapPageAllocBreak)
	{
		uint32 alloc_start = uheapPageAllocBreak;
		uheapPageAllocBreak += aligned_size;

		for (int i = 0; i < allocs; i++){
			    if (!alloc[i].used){
			        alloc[i].va = (void*)alloc_start;
			        alloc[i].size = aligned_size;
			        alloc[i].used = 1;
			        break;
			    }
		}
		sys_allocate_user_mem(alloc_start, aligned_size);
		return (void*)alloc_start;
	}

	return NULL;
}

//=================================
// [2] FREE SPACE FROM USER HEAP:
//=================================
void free(void* virtual_address)
{
	//TODO: [PROJECT'25.IM#2] USER HEAP - #3 free

	 uint32 va = (uint32)virtual_address;
	    if (virtual_address == 0)
	        return;

	    if (va >= USER_HEAP_START && va < uheapPageAllocStart)
	    {
	        free_block(virtual_address);
	        return;
	    }

	    if (va >= uheapPageAllocStart && va < uheapPageAllocBreak)
	    {
	        int block_size = 0;
	        int ii = -1;


	        for (int i = 0; i < allocs; i++)
	        {
	        	if (alloc[i].used && alloc[i].va == virtual_address)
	            {
	                block_size = alloc[i].size;
	                ii = i;
	                break;
	            }
	        }

	        if (ii == -1)
	            panic("free(): invalid address in page allocator");


	        alloc[ii].used = 0;
	        alloc[ii].va = 0;
	        alloc[ii].size = 0;

	        sys_free_user_mem(va, block_size);


	        if (va + block_size == uheapPageAllocBreak)
	        {
	            uint32 new_break = uheapPageAllocStart;
	            for (int i = 0; i < allocs; i++)
	            {
	                if (alloc[i].used && (uint32)alloc[i].va + alloc[i].size > new_break)
	                {
	                    new_break = (uint32)alloc[i].va + alloc[i].size;
	                }
	            }
	            uheapPageAllocBreak = new_break;
	        }

	        return;
	    }

	    panic("free(): address not within user heap");
}

//=================================
// [3] ALLOCATE SHARED VARIABLE:
//=================================
void* smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	//Your code is here
	//Comment the following line
	panic("smalloc() is not implemented yet...!!");
}

//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void* sget(int32 ownerEnvID, char *sharedVarName)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #4 sget
	//Your code is here
	//Comment the following line
	panic("sget() is not implemented yet...!!");
}


//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//


//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}


//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void* virtual_address)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	//Your code is here
	//Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}


//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
