#include <inc/memlayout.h>
#include "shared_memory_manager.h"

#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/queue.h>
#include <inc/environment_definitions.h>

#include <kern/proc/user_environment.h>
#include <kern/trap/syscall.h>
#include "kheap.h"
#include "memory_manager.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] INITIALIZE SHARES:
//===========================
//Initialize the list and the corresponding lock
void sharing_init()
{
#if USE_KHEAP
	LIST_INIT(&AllShares.shares_list) ;
	init_kspinlock(&AllShares.shareslock, "shares lock");
	//init_sleeplock(&AllShares.sharessleeplock, "shares sleep lock");
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//=========================
// [2] Find Share Object:
//=========================
//Search for the given shared object in the "shares_list"
//Return:
//	a) if found: ptr to Share object
//	b) else: NULL
struct Share* find_share(int32 ownerID, char* name)
{
#if USE_KHEAP
	struct Share * ret = NULL;
	bool wasHeld = holding_kspinlock(&(AllShares.shareslock));
	if (!wasHeld)
	{
		acquire_kspinlock(&(AllShares.shareslock));
	}
	{
		struct Share * shr ;
		LIST_FOREACH(shr, &(AllShares.shares_list))
		{
			//cprintf("shared var name = %s compared with %s\n", name, shr->name);
			if(shr->ownerID == ownerID && strcmp(name, shr->name)==0)
			{
				//cprintf("%s found\n", name);
				ret = shr;
				break;
			}
		}
	}
	if (!wasHeld)
	{
		release_kspinlock(&(AllShares.shareslock));
	}
	return ret;
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}

//==============================
// [3] Get Size of Share Object:
//==============================
int size_of_shared_object(int32 ownerID, char* shareName)
{
	// This function should return the size of the given shared object
	// RETURN:
	//	a) If found, return size of shared object
	//	b) Else, return E_SHARED_MEM_NOT_EXISTS
	//
	struct Share* ptr_share = find_share(ownerID, shareName);
	if (ptr_share == NULL)
		return E_SHARED_MEM_NOT_EXISTS;
	else
		return ptr_share->size;

	return 0;
}
//===========================================================


//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=====================================
// [1] Alloc & Initialize Share Object:
//=====================================
//Allocates a new shared object and initialize its member
//It dynamically creates the "framesStorage"
//Return: allocatedObject (pointer to struct Share) passed by reference
struct Share* alloc_share(int32 ownerID, char* shareName, uint32 size, uint8 isWritable)
{
	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #1 alloc_share
	//Your code is here
	//Comment the following line
	//panic("alloc_share() is not implemented yet...!!");
	struct Share* newo = (struct Share*)kmalloc(sizeof(struct Share));
	if (newo == NULL) {
		return NULL;
	}

	//Intilization
	newo->isWritable = isWritable;
	newo->references = 1;
	newo->size = size;
	newo->ownerID = ownerID;
	newo->ID = (uint32) newo & 0x7FFFFFFF;
	strcpy(newo->name, shareName);
	uint32 num_frames = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	//7gz mkan ll frames
	struct FrameInfo **frames_List=(struct FrameInfo**)kmalloc(num_frames * sizeof(struct FrameInfo*)); //ne frameinfo
	if (frames_List == NULL) {
		kfree(newo);
		cprintf("creating frames list failed");
		return NULL;
		}
	for (int i =0;i<num_frames;i++){  //intilize el frames b NULL zy ma el doc 2al
		frames_List[i]=NULL;
	}
	newo->framesStorage=frames_List;

return newo;
}


//=========================
// [4] Create Share Object:
//=========================
int create_shared_object(int32 ownerID, char* shareName, uint32 size, uint8 isWritable, void* virtual_address)
{
	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #3 create_shared_object
	//Your code is here
	//Comment the following line
	//panic("create_shared_object() is not implemented yet...!!");

	struct Env* myenv = get_cpu_proc(); //The calling environment

	// This function should create the shared object at the given virtual address with the given size
	// and return the ShareObjectID
	// RETURN:
	//	a) ID of the shared object (its VA after masking out its msb) if success
	//	b) E_SHARED_MEM_EXISTS if the shared object already exists
	//	c) E_NO_SHARE if failed to create a shared object

// mnsa4 el locks b3d ma a5ls
		//Some of checks
		if (((uint32)virtual_address % PAGE_SIZE) != 0)
	    return E_NO_SHARE;

		if (shareName == NULL || size == 0 || virtual_address == NULL) {
			return E_NO_SHARE;
		}

		acquire_kspinlock(&AllShares.shareslock);
		// Check if shared object already exists
		if (find_share(ownerID, shareName) != NULL) {
			release_kspinlock(&AllShares.shareslock);
			return E_SHARED_MEM_EXISTS;
		}

		//  b3ml Allocate ll share object
		struct Share* new_share = alloc_share(ownerID, shareName, size, isWritable);
		if (new_share == NULL) {
				release_kspinlock(&AllShares.shareslock);
				return E_NO_SHARE;
			}
		// b7sb number of frames
			uint32 num_frames = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
		// Allocate and map physical frames (72e2y mem)
		for (uint32 i = 0; i < num_frames; i++) {
		int alloc_ret=allocate_frame(&new_share->framesStorage[i]);
			if(alloc_ret!=0){
			   kfree((void*) new_share);
			    release_kspinlock(&AllShares.shareslock);
			    return E_NO_SHARE;
		}

		   int perm = PERM_PRESENT | PERM_USER | PERM_WRITEABLE;

				// b3ml Map ll frame to virtual address
				struct FrameInfo * shared_frame_pointer = new_share->framesStorage[i];
				int map_return=map_frame(myenv->env_page_directory, shared_frame_pointer, (uint32)virtual_address + i * PAGE_SIZE, perm);

				if (map_return != 0) {
					kfree((void*) new_share);
					release_kspinlock(&AllShares.shareslock);
					return E_NO_SHARE;
					}

				}

				//b7otha b2a fel list
				LIST_INSERT_HEAD(&AllShares.shares_list, new_share);  //nr
				release_kspinlock(&AllShares.shareslock);

				return new_share->ID;

}


//======================
// [5] Get Share Object:
//======================
int get_shared_object(int32 ownerID, char* shareName, void* virtual_address)
{
	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #5 get_shared_object
	//Your code is here
	//Comment the following line
	//panic("get_shared_object() is not implemented yet...!!");

	struct Env* myenv = get_cpu_proc(); //The calling environment

	// 	This function should share the required object in the heap of the current environment
	//	starting from the given virtual_address with the specified permissions of the object: read_only/writable
	// 	and return the ShareObjectID
	// RETURN:
	//	a) ID of the shared object (its VA after masking out its msb) if success
	//	b) E_SHARED_MEM_NOT_EXISTS if the shared object is not exists

  // ab7s 3no
		struct Share* share = find_share(ownerID, shareName);
		if (share == NULL) {

			return E_SHARED_MEM_NOT_EXISTS;
		}
		// a7sb number of frames lw l2eto
		uint32 num_frames = ROUNDUP(share->size, PAGE_SIZE) / PAGE_SIZE;

		// a4yr el frame m3 el current process
		for (uint32 i = 0; i < num_frames; i++) {
			struct FrameInfo* frame_info = share->framesStorage[i];
			if (frame_info == NULL) {
				return E_NO_SHARE;
			}
			int perm = PERM_PRESENT | PERM_USER;
			if (share->isWritable) {
				perm |= PERM_WRITEABLE;
			}
			// h3ml b2a Map ll existing frame to the new virtual address
			int map_return =map_frame(myenv->env_page_directory, frame_info, (uint32)virtual_address + i * PAGE_SIZE, perm);
				if (map_return != 0) {
					kfree((void*) share);
					return E_NO_SHARE;
				}
		}

		share->references++;
		return share->ID;

}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//
//=========================
// [1] Delete Share Object:
//=========================
//delete the given shared object from the "shares_list"
//it should free its framesStorage and the share object itself
void free_share(struct Share* ptrShare)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - free_share
	//Your code is here
	//Comment the following line
	panic("free_share() is not implemented yet...!!");
}


//=========================
// [2] Free Share Object:
//=========================
int delete_shared_object(int32 sharedObjectID, void *startVA)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - delete_shared_object
	//Your code is here
	//Comment the following line
	panic("delete_shared_object() is not implemented yet...!!");

	struct Env* myenv = get_cpu_proc(); //The calling environment

	// This function should free (delete) the shared object from the User Heapof the current environment
	// If this is the last shared env, then the "frames_store" should be cleared and the shared object should be deleted
	// RETURN:
	//	a) 0 if success
	//	b) E_SHARED_MEM_NOT_EXISTS if the shared object is not exists

	// Steps:
	//	1) Get the shared object from the "shares" array (use get_share_object_ID())
	//	2) Unmap it from the current environment "myenv"
	//	3) If one or more table becomes empty, remove it
	//	4) Update references
	//	5) If this is the last share, delete the share object (use free_share())
	//	6) Flush the cache "tlbflush()"

}
