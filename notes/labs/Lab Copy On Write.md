# Lab Copy On Write

## 任务

推迟分配和复制物理内存页，直到实际需要时才分配。

fork() 只为子进程创建一个页表，PTE 指向父进程的物理页。将父级和子级中的所有用户 PTE 标记为只读。当任一进程尝试写入其中一个COW的页时，CPU将发生page fault。

页面错误处理程序检测到这种page fault，为错误进程分配物理内存页面，将原始页面复制到新页面，并修改错误进程中的相关 PTE 以指向新页面，且 PTE 标记为可写。当页面错误处理程序返回时，用户进程将能够进行写入。

实现COW的fork()会让释放物理页变得有点棘手。给定的物理页可能被多个进程的页表引用，并且仅当最后一个引用消失时才应释放。

## 代码实现

在`uvmcopy`中不为子进程申请新的页，使父子进程共享页面，要注意的是只读页面不需要实现COW，只需在`PTE_W == 1`的可写页面上实现COW即可，父子进程的对应页表项都要标记上`PTE_COW`并清除`PTE_W`（为了触发write pagefault）。

### PTE_COW

```c
// riscv.h
#define PTE_COW (1L << 7)
```

### uvmcopy

在页表拷贝时，将子进程的页表项指向父进程的页：

```c
// vm.c
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE) {
    // ...
    // 物理页地址
    pa = PTE2PA(*pte);
    if (*pte & PTE_W) {
      *pte |= PTE_COW;
      *pte &= ~PTE_W;
    }
    flags = PTE_FLAGS(*pte);
    // 指向物理页
    if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0) {
      goto err;
    }
    acquire(&ref_lock);
    ref_incr(pa); // 增加pa的引用计数
    release(&ref_lock);
  }
  return 0;
  // ...
}
```

### write page fault中断处理

在`usertrap`中，对于由write page fault引起的中断，检查是否为COW页，如果是那么申请新页面并修改页表项和页面计数。

发生write page fault时，scause为15：

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150034925.png" style="zoom: 33%;" />

发生page fault的虚拟地址va存储在stval寄存器中。

检查这一页是否是COW页：

```c
// trap.c
int is_cow(uint64 va) {
  struct proc* p = myproc();
  if (va >= p->sz) return 0;
  pte_t* pte;
  if ((pte = walk(p->pagetable, va, 0)) == 0) // 如果未分配或不在页表中那么也不是COW页
    return 0;
  return (*pte & PTE_COW) && (*pte & PTE_V); 
}
```

然后处理COW：

1. 分配新页面
2. 将COW页的内容复制到新页面上
3. 设置新页面的flag
4. 使用`kfree`进行旧页面引用递减，如果引用变为0，那么释放
5. 删除页表中的旧页表项，建立新页面的页表项

```c
int cow_pagefault(uint64 va) {
  struct proc* p = myproc();
  pte_t* pte = walk(p->pagetable, va, 0);
  uint64 oldpa = PTE2PA(*pte);

  // 1. 分配新页面
  uint64 newpage;
  if ((newpage = (uint64)kalloc()) == 0) {
    kill(p->pid);
    return -1;
  }
  // 2. 复制页面内容
  va = PGROUNDDOWN(va);
  // copy old page to new page
  copyin(p->pagetable, (char *)newpage, va, PGSIZE);

  // 3. 设置flag
  int flags = PTE_FLAGS(*pte);
  flags &= ~PTE_COW; 
  flags |= PTE_W;
  // 4. 解除旧map
  uvmunmap(p->pagetable, va, 1, 0);
  kfree((void *)oldpa); // oldpa仅减引用，不一定会释放
  // 5. 创建新map
  if (mappages(p->pagetable, va, PGSIZE, newpage, flags) < 0) {
    uvmfree(p->pagetable, 0);
    return -1;
  }
  return 0;
}
```

在`usertrap`中使用上述函数：

```c
void usertrap(void) {
	// ...
  p->trapframe->epc = r_sepc();
  
  int cause = r_scause();
	// ...
  else if (cause == 15) {
    uint64 va = r_stval();
    if (is_cow(va) == 1) 
      cow_pagefault(va);
    else 
      kill(p->pid);
    
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }
  // ...
}
```

### 页面引用计数

数组记录页面的计数：

```c
// kalloc.c
#define PA2PG(pa) (PGROUNDDOWN(pa) / PGSIZE)

int ref_count[PHYSTOP / PGSIZE];
struct spinlock ref_lock;

void ref_incr(uint64 pa) {
  ref_count[PA2PG(pa)]++;
}

void ref_decr(uint64 pa) {
  int num = PA2PG(pa);
  if (ref_count[num] == 0) return; // 防止kernel启动时freerange把计数减成负数
  ref_count[num]--;
}
```

在分配和释放页面时进行引用更改：

```c
void kfree(void *pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref_lock);
  ref_decr((uint64)pa);
  // 如果还有进程在使用pa，那么直接返回
  if (ref_count[PA2PG((uint64)pa)] > 0) {
    release(&ref_lock);
    return;
  }
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  struct run * r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);

  release(&ref_lock);
}

void * kalloc(void) {
  acquire(&kmem.lock);
  struct run * r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  acquire(&ref_lock);
  ref_count[PA2PG((uint64)r)] = 1;
  release(&ref_lock);

  return (void*)r;
}
```

### copyout

从内核态写出到用户态的页面可能也是COW页，先检查页是否为COW页，如果是那么进行与usertrap中一样的处理：

```c
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  while(len > 0){
    uint64 va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    // 检查cow
    if (is_cow(va0)) {
      if (cow_pagefault(va0) == -1) 
        return -1;
    }
    pte_t *pte = walk(pagetable, va0, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1;
    uint64 pa0 = PTE2PA(*pte);
    uint64 n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

OK啦。

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150045348.png" alt="image-20231221130057890" style="zoom:50%;" />

