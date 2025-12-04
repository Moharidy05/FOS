/*
 * fault_handler.c
 *
 * Created on: Oct 12, 2022
 * Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>
#include <inc/memlayout.h>

#ifndef FOS_BOOL_H
#define FOS_BOOL_H

#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif // __cplusplus

#endif

uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

void fault_handler_init()
{
	setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}

/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;

void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	uint32 fault_va = rcr2();
	struct Env* cur_env = get_cpu_proc();


	uint32 current_eip = (uint32)tf->tf_eip;

	if (last_fault_va == fault_va && last_faulted_env == cur_env && last_faulted_env != NULL)
	{
	    num_repeated_fault++;
	    if (num_repeated_fault == 3)
	    {
	        print_trapframe(tf);
	        panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n",
	              before_last_fault_va, before_last_eip, fault_va);
	    }
	}
	else
	{
	    before_last_fault_va = last_fault_va;
	    before_last_eip = last_eip;
	    num_repeated_fault = 1;
	}

	last_eip = current_eip;
	last_fault_va = fault_va;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}

	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			if (fault_va >= USER_TOP)
				env_exit();

			int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			if ((perms & PERM_PRESENT))
			{
				if ((tf->tf_err & 0x02) && !(perms & PERM_WRITEABLE))
					env_exit();
			}

			if (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)
			{
				if (perms == -1 || !(perms & PERM_AVAILABLE))
					env_exit();
			}
		}

		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;

		faulted_env->pageFaultsCounter ++ ;

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}
	}

	tlbflush();
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	uint32 *current_ws = kmalloc(sizeof(uint32) * maxWSSize);
	if (current_ws == NULL) panic("get_optimal_num_faults: kheap is full!");

	int current_ws_size = 0;
	int faults = 0;

	struct WorkingSetElement *wse_ptr;
	LIST_FOREACH(wse_ptr, initWorkingSet)
	{
		if (current_ws_size < maxWSSize)
		{
			current_ws[current_ws_size++] = ROUNDDOWN(wse_ptr->virtual_address, PAGE_SIZE);
		}
	}

	struct PageRefElement *ref_ptr;
	LIST_FOREACH(ref_ptr, pageReferences)
	{
		uint32 current_page_va = ROUNDDOWN(ref_ptr->virtual_address, PAGE_SIZE);
		int found = 0;

		for (int i = 0; i < current_ws_size; i++)
		{
			if (current_ws[i] == current_page_va)
			{
				found = 1;
				break;
			}
		}

		if (found) continue;

		faults++;

		if (current_ws_size < maxWSSize)
		{
			current_ws[current_ws_size++] = current_page_va;
		}
		else
		{
			int vindex = -1;
			int max_distance = -1;

			for (int i = 0; i < current_ws_size; i++)
			{
				int distance = 0;
				int caught = 0;
				uint32 page_in_memory = current_ws[i];

				struct PageRefElement *future_ptr = LIST_NEXT(ref_ptr);

				while (future_ptr != NULL)
				{
					distance++;
					if (ROUNDDOWN(future_ptr->virtual_address, PAGE_SIZE) == page_in_memory)
					{
						caught = 1;
						break;
					}
					future_ptr = LIST_NEXT(future_ptr);
				}

				if (!caught)
				{
					vindex = i;
					break;
				}

				if (distance > max_distance)
				{
					max_distance = distance;
					vindex = i;
				}
			}
			current_ws[vindex] = current_page_va;
		}
	}

	kfree(current_ws);
	return faults;
}


void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	if (isPageReplacmentAlgorithmOPTIMAL())
	{
		struct PageRefElement *ptr_ref = kmalloc(sizeof(struct PageRefElement));
		ptr_ref->virtual_address = ROUNDDOWN(fault_va, PAGE_SIZE);
		LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), ptr_ref);


		if (LIST_SIZE(&(faulted_env->ALTWS)) == 0 && LIST_SIZE(&(faulted_env->page_WS_list)) > 0)
		{
			struct WorkingSetElement *initial_wse;
			LIST_FOREACH(initial_wse, &(faulted_env->page_WS_list))
			{
				pt_set_page_permissions(faulted_env->env_page_directory, initial_wse->virtual_address, 0, PERM_PRESENT);

				struct WorkingSetElement *new_wse = kmalloc(sizeof(struct WorkingSetElement));
				new_wse->virtual_address = initial_wse->virtual_address;
				new_wse->time_stamp = 0;
				new_wse->sweeps_counter = 0;
				LIST_INSERT_TAIL(&(faulted_env->ALTWS), new_wse);
			}
		}

		uint32 page_va = ROUNDDOWN(fault_va, PAGE_SIZE);
		bool foundInWS = 0;
		struct WorkingSetElement *wse = NULL;

		LIST_FOREACH(wse, &(faulted_env->ALTWS))
		{
			if (ROUNDDOWN(wse->virtual_address, PAGE_SIZE) == page_va)
			{
				foundInWS = 1;
				break;
			}
		}

		if (foundInWS)
		{
			pt_set_page_permissions(faulted_env->env_page_directory, page_va, PERM_PRESENT, 0);
		}
		else
		{
			if (LIST_SIZE(&(faulted_env->ALTWS)) >= faulted_env->page_WS_max_size)
			{
				struct WorkingSetElement *elm;
				while ((elm = LIST_FIRST(&(faulted_env->ALTWS))) != NULL)
				{
					pt_set_page_permissions(faulted_env->env_page_directory, elm->virtual_address, 0, PERM_PRESENT);
					LIST_REMOVE(&(faulted_env->ALTWS), elm);
					kfree(elm);
				}
			}

			struct WorkingSetElement *new_wse = kmalloc(sizeof(struct WorkingSetElement));
			new_wse->virtual_address = page_va;
			new_wse->time_stamp = 0;
			new_wse->sweeps_counter = 0;
			LIST_INSERT_TAIL(&(faulted_env->ALTWS), new_wse);

			uint32 *ptr_table = NULL;
			struct FrameInfo *ptr_fi = get_frame_info(faulted_env->env_page_directory, page_va, &ptr_table);

			if (ptr_fi != NULL)
			{
				pt_set_page_permissions(faulted_env->env_page_directory, page_va, PERM_PRESENT, 0);
			}
			else
			{
				int ret = allocate_frame(&ptr_fi);
				if (ret == E_NO_MEM) env_exit();

				map_frame(faulted_env->env_page_directory, ptr_fi, page_va, PERM_USER | PERM_WRITEABLE | PERM_PRESENT);

				ret = pf_read_env_page(faulted_env, (void*)page_va);

				if (ret == E_PAGE_NOT_EXIST_IN_PF)
				{
					if ((page_va >= USTACKBOTTOM && page_va < USTACKTOP) ||
						(page_va >= USER_HEAP_START && page_va < USER_HEAP_MAX))
					{
					}
					else
					{
						unmap_frame(faulted_env->env_page_directory, page_va);
						env_exit();
					}
				}
			}
		}
	}
	else
	{
		struct WorkingSetElement *victimWSElement = NULL;
		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));

		// ==================================================================================
		//                                 PLACEMENT
		// ==================================================================================
		if(wsSize < (faulted_env->page_WS_max_size))
		{
			uint32 page_va = ROUNDDOWN(fault_va, PAGE_SIZE);
			struct FrameInfo *ptr_frame_info = NULL;

			int ret = allocate_frame(&ptr_frame_info);
			if (ret != E_NO_MEM)
			{
				map_frame(faulted_env->env_page_directory, ptr_frame_info, page_va, PERM_USER | PERM_WRITEABLE | PERM_PRESENT);
			}
			else
			{
				env_exit();
			}

			ret = pf_read_env_page(faulted_env, (void*)page_va);

			if (ret == E_PAGE_NOT_EXIST_IN_PF)
			{
				if ((page_va >= USTACKBOTTOM && page_va < USTACKTOP) ||
					(page_va >= USER_HEAP_START && page_va < USER_HEAP_MAX))
				{
				}
				else
				{
					unmap_frame(faulted_env->env_page_directory, page_va);
					env_exit();
				}
			}

			struct WorkingSetElement *new_ws = env_page_ws_list_create_element(faulted_env, page_va);
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_ws);

			if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
			{
				if (faulted_env->page_last_WS_element == NULL)
				{
					faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
				}
			}
			else
			{
				faulted_env->page_last_WS_element = NULL;
			}
		}
		// ==================================================================================
		//                         REPLACEMENT (CLOCK ALGORITHM)
		// ==================================================================================
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
			    {
			        uint32 page_va = ROUNDDOWN(fault_va, PAGE_SIZE);
			        struct WorkingSetElement *curr_element = faulted_env->page_last_WS_element;

			        if (curr_element == NULL)
			            curr_element = LIST_FIRST(&(faulted_env->page_WS_list));

			        while (1)
			        {
			            uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory,
			                                                   curr_element->virtual_address);

			            if (perms & PERM_USED)
			            {
			                pt_set_page_permissions(faulted_env->env_page_directory,
			                                       curr_element->virtual_address, 0, PERM_USED);
			                curr_element = LIST_NEXT(curr_element);
			                if (curr_element == NULL)
			                    curr_element = LIST_FIRST(&(faulted_env->page_WS_list));
			            }
			            else
			            {
			                struct WorkingSetElement *victim = curr_element;
			                uint32 victim_va = victim->virtual_address;

			                // 1. Identify the next position for the clock hand
			                struct WorkingSetElement *next_hand = LIST_NEXT(victim);
			                if (next_hand == NULL)
			                    next_hand = LIST_FIRST(&(faulted_env->page_WS_list));

			                // 2. Write back to disk if Modified
			                uint32 *ptr_table = NULL;
			                struct FrameInfo *ptr_victim_fi = get_frame_info(faulted_env->env_page_directory,
			                                                                 victim_va, &ptr_table);
			                uint32 victim_perms = pt_get_page_permissions(faulted_env->env_page_directory,
			                                                               victim_va);

			                if (victim_perms & PERM_MODIFIED)
			                {
			                    int ret = pf_update_env_page(faulted_env, victim_va, ptr_victim_fi);
			                    if (ret == E_NO_PAGE_FILE_SPACE)
			                        panic("ERROR: No Page File Space!");
			                }

			                // 3. Unmaps victim
			                unmap_frame(faulted_env->env_page_directory, victim_va);

			                // 4. Allocate and map new page
			                struct FrameInfo *ptr_new_fi = NULL;
			                int ret = allocate_frame(&ptr_new_fi);
			                if (ret == E_NO_MEM)
			                    env_exit();

			                map_frame(faulted_env->env_page_directory, ptr_new_fi, page_va,
			                          PERM_USER | PERM_WRITEABLE | PERM_PRESENT);

			                ret = pf_read_env_page(faulted_env, (void*)page_va);
			                if (ret == E_PAGE_NOT_EXIST_IN_PF)
			                {
			                    if ((page_va >= USTACKBOTTOM && page_va < USTACKTOP) ||
			                        (page_va >= USER_HEAP_START && page_va < USER_HEAP_MAX))
			                    {
			                    }
			                    else
			                    {
			                        unmap_frame(faulted_env->env_page_directory, page_va);
			                        env_exit();
			                    }
			                }

			                // 5. UPDATE IN PLACE (Critical Fix)
			                // Do not remove the node. Just update the VA and reset the timestamp.
			                LIST_REMOVE(&(faulted_env->page_WS_list), victim);
			                // 2. Create new element for the incoming page
			                struct WorkingSetElement *new_ws = env_page_ws_list_create_element(faulted_env, page_va);
			                // 3. Insert at the position AFTER where victim was (maintaining clock order)
			                if (next_hand == LIST_FIRST(&(faulted_env->page_WS_list))) {
			                    // If next_hand is the first, insert at end
			                    LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_ws);
			                } else {
			                    // Insert before next_hand
			                    LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), next_hand, new_ws);
			                }
			                // 4. Free the old victim element
			                kfree(victim);
			                // 5. Update clock hand
			                faulted_env->page_last_WS_element = next_hand;

			                // 6. Move clock hand forward
			                faulted_env->page_last_WS_element = next_hand;

			                break;
			            }
			        }
			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
		}
	}
#endif
}
void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}
