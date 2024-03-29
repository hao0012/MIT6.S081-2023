// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

int blocknum(uint64 pa) {
  uint64 blocksize = ((uint64)PHYSTOP - (uint64)end) / NCPU;
  return (pa - (uint64)end) / blocksize;
}

struct run {
  struct run *next;
};

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem.lock[i], "kmem");
  }
  
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int cpuid = blocknum((uint64)pa);

  acquire(&kmem.lock[cpuid]);
  r->next = kmem.freelist[cpuid];
  kmem.freelist[cpuid] = r;
  release(&kmem.lock[cpuid]);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu = cpuid();
  pop_off();

  // alloc page from current cpu freelist
  acquire(&kmem.lock[cpu]);
  r = kmem.freelist[cpu];
  if(r)
    kmem.freelist[cpu] = r->next;
  release(&kmem.lock[cpu]);
  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }

  // if there is no free page, check other cpu freelist
  for (int i = 0; i < NCPU; i++) {
    acquire(&kmem.lock[i]);
    r = kmem.freelist[i];
    if (r) 
      kmem.freelist[i] = r->next;
    release(&kmem.lock[i]);
    if (r) {
      memset((char*)r, 5, PGSIZE); // fill with junk
      return (void*) r;
    }
  }
  return 0;
}
