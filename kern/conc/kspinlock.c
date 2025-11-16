// Mutual exclusion spin locks.
/*originally taken from xv6-x86 OS
 * USED ONLY FOR PROTECTION IN MULTI-CORE
 * Not designed for protection in a single core
 * */
#include "kspinlock.h"

#include "inc/x86.h"
#include "inc/memlayout.h"
#include "inc/mmu.h"
#include "inc/string.h"
#include "inc/environment_definitions.h"
#include "inc/assert.h"
#include "../cpu/cpu.h"
#include "../proc/user_environment.h"

// Function declarations for functions defined in cpu.c
void pushcli(void);
void popcli(void);
struct cpu* mycpu(void);
struct Env* get_cpu_proc(void);

void init_kspinlock(struct kspinlock *lk, char *name)
{
	strcpy(lk->name, name);
	lk->locked = 0;
	lk->cpu = 0;
}

void acquire_kspinlock(struct kspinlock *lk)
{
	if(holding_kspinlock(lk))
		panic("acquire_spinlock: lock \"%s\" is already held by the same CPU.", lk->name);

	/*disable interrupts to avoid deadlock (in case if interrupted from a higher priority (or event handler)
	 * just after holding the lock => the handler will stuck in busy-waiting and prevent the other from resuming)
	 */
	pushcli();

	int envID = 0;
	struct Env *e = get_cpu_proc() ;
	if (e) envID = e->env_id;
	//cprintf("[%d] try to acquire spinlock [%s]\n", envID, lk->name);

	while(xchg(&lk->locked, 1) != 0) ;


	__sync_synchronize();

	// Record info about lock acquisition for debugging.
	lk->cpu = mycpu();
	getcallerpcs(&lk, lk->pcs);

}

// Release the lock.
void release_kspinlock(struct kspinlock *lk)
{
	if(!holding_kspinlock(lk))
	{
		printcallstack(lk);
		panic("release: lock \"%s\" is either not held or held by another CPU!", lk->name);
	}
	lk->pcs[0] = 0;
	lk->cpu = 0;


	__sync_synchronize();


	asm volatile("movl $0, %0" : "+m" (lk->locked) : );

	int envID = 0;
	struct Env *e = get_cpu_proc() ;
	if (e) envID = e->env_id;
	//cprintf("[%d] release spinlock [%s]\n", envID, lk->name);

	popcli();

}

// Record the current call stack in pcs[] by following the %ebp chain.
int getcallerpcs(void *v, uint32 pcs[])
{
	uint32 *ebp;
	int i;
	struct Env* p = get_cpu_proc();
	struct cpu* c = mycpu();
	ebp = (uint32*)v - 2;
	for(i = 0; i < 10; i++)
	{
		//cprintf("old ebp = %x\n", ebp);
		if	(	ebp == 0 || (ebp < (uint32*) USER_LIMIT) ||
				(ebp >= (uint32*)(c->stack + KERNEL_STACK_SIZE) && ebp < (uint32*)(c->stack + KERNEL_STACK_SIZE + PAGE_SIZE)) ||
				(p && ebp >= (uint32*) (p->kstack + KERNEL_STACK_SIZE)))
			break;
		pcs[i] = ebp[1];     // saved %eip
		ebp = (uint32*)ebp[0]; // saved %ebp
		//		cprintf("new ebp = %x\n", ebp);
		//		cprintf("pc[%d] = %x\n", i, pcs[i]);
	}
	int length = i ;
	for(; i < 10; i++)
		pcs[i] = 0;
	return length ;
}

void printcallstack(struct kspinlock *lk)
{
	cprintf("\nCaller Stack:\n");
	int stacklen = 	getcallerpcs(&lk, lk->pcs);
	for (int i = 0; i < stacklen; ++i) {
		cprintf("  PC[%d] = %x\n", i, lk->pcs[i]);
	}
}

// Check whether this cpu is holding the lock.
int holding_kspinlock(struct kspinlock *lock)
{
	int r;
	pushcli();
	r = lock->locked && lock->cpu == mycpu();
	popcli();
	return r;
}
