#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"



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
  if(n < 0)
    n = 0;
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
uint64 sys_mmap(void) {
  int len; argint(1, &len);
  len = PGROUNDUP(len);
  int prot; argint(2, &prot);
  int flags; argint(3, &flags);
  int fd; argint(4, &fd);
  struct proc* p = myproc();
  if (p == 0) return -1;
  int i = 0;
  while (i < 16) {
    if (p->vmas[i].valid == 0) break;
    i++;
  }
  struct file* f = p->ofile[fd];
  if (i == 16 || !f) return -1;
  if (flags & MAP_SHARED) {
    if ((!f->writable) && (prot & PROT_WRITE) != 0) return -1;
  }
  
  p->vmas[i].valid = 1;
  p->vmas[i].f = p->ofile[fd];
  filedup(p->ofile[fd]);
  p->vmas[i].len = len;
  p->vmas[i].prot = prot;
  p->vmas[i].flags = flags;
  p->vma_top -= len;
  p->vmas[i].start = p->vma_top;
  p->vmas[i].len = len;
  for (uint64 j = p->vmas[i].start; j < p->vmas[i].start + len; j += PGSIZE) {
    pte_t* pte = walk(p->pagetable, j, 0);
    *pte = PTE_A;
  }
  return p->vmas[i].start;
}

/*
  return 0 if there still has page exist else 1
*/
int check(pagetable_t p, struct vma v) {
  for (uint64 i = v.start; i < v.start + v.len; i += PGSIZE) {
    pte_t* pte = walk(p, i, 0);
    if ((*pte) == PTE_A || ((*pte) & PTE_V)) return 0;
  }
  return 1;
}

uint64 sys_munmap(void) {
  uint64 va; argaddr(0, &va);
  int len; argint(1, &len);
  int i = 0;
  struct vma v;
  struct proc* p = myproc();
  for (; i < 16; i++) {
    v = p->vmas[i];
    if (v.start <= va && va <= v.start + v.len) {
      if (va + len > v.start + v.len) {
        continue;
      }
      break;
    }
  }
  if (i == 16) return -1;
  for (uint64 i = va; i < va + len; i += PGSIZE) {
    pte_t* pte = walk(p->pagetable, i, 0);
    if (*pte == 0 || *pte == PTE_A) {
      *pte = 0;
      continue;
    }
    if (munmap_filewrite(v, i, PGSIZE) == -1) 
      return -1;
    uvmunmap(p->pagetable, i, 1, 1);
  }
  
  if (check(p->pagetable, v)) {
    fileclose(v.f);
    p->vmas[i].valid = 0;
  }
  return 0;
}