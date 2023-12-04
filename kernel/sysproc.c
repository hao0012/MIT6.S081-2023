#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int sys_pgaccess(void) {
  uint64 va; argaddr(0, &va);

  struct proc* p = myproc();
  pagetable_t pgt = p->pagetable;
  
  // find pagetable
  for (int l = 2; l > 0; l--) {
    pte_t *pte = &pgt[PX(l, va)];
    if (*pte & PTE_V) { // valid pte
      pgt = (pagetable_t)PTE2PA(*pte);
    } else {
      printf("invalid pte\n");
      return -1;
    }
  }
  int num; argint(1, &num);
  uint64 bitmask = 0;
  for (uint64 i = 0; i < num; i++) {
    uint64 idx = PX(0, va) + i;
    if (idx >= 512) {
      printf("out of range.\n");
      return -1;
    }
    pte_t *pte = &pgt[idx];
    if (((*pte & PTE_V) == 1) && ((*pte & PTE_A) >> 6) == 1) {
      bitmask |= (1L << i);
    }
    *pte = (*pte & (~PTE_A)); // PTE_A = 0
  }
  uint64 store; argaddr(2, &store);
  copyout(p->pagetable, store, (char *)(&bitmask), sizeof(bitmask));
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}