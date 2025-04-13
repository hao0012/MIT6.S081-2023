# Lab Mmap

## mmap

### 作用

调用mmap可以将文件的内容映射到线程的用户空间中，这样在读取文件时，不需要read/write系统调用，而是直接访问即可，当使用read访问文件时，文件内容首先从disk拷贝到kernel，再从kernel拷贝到用户空间，使用mmap可以直接将文件从disk拷贝到user space。

### 实现

mmap函数如下：

```c
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
```

其中：

1. addr: 用户指定将文件拷贝到addr处，这个参数一般为空，表示由操作系统决定拷贝到哪里。
2. len: 要映射的数据的长度。
3. prot: 内容的访问控制符，包括PROT_READ和PROT_WRITE，分别表示可读和可写。
4. flags: 当内容被修改后是否应该写回disk的标识符，包括MAP_SHARED（需要写回）和MAP_PRIVATE（不用写回）。
5. fd: 要进行映射的文件的文件描述符。
6. offset: 表示从文件的offset处开始映射。

在mmap中我们要实现的功能是**告诉线程有一个打开的文件被mmap映射了**。

要注意的是，这里采用的是懒加载的方式，也就是说在mmap中不需要真的读取文件并写入user space，而是把这一步延迟，当实际访问被映射地址并发生中断时，线程会知道这是一个mmap映射的地址，此时在中断处理中才进行文件的映射。

线程需要保存映射信息，由于lab中addr、offset并没有用到，这里直接没有保存。除此之外，还要在user space中找一段空间用于mmap映射文件，我选择的是TRAPFRAME下面的空间。其实一开始我是直接在heap中进行映射，但是一直报错，可能是由于kernel会在其他地方操作heap导致的，这里还是没有弄得太明白。

```c
// proc.h
struct vma {
  struct file* f;
  uint64 start;
  uint64 len;
  int prot; 
  int flags;
  int valid; // is the vma in use?
};

// Per-process state
struct proc {
  // ...
  struct vma vmas[16];
  uint64 vma_top;
};
```

sys_mmap中记录读入的数据即可，要注意的是如果flag为`MAP_SHARED`，文件必须是可写的：

```c
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
```

这里定义了一个PTE_A，如果页表项设置了PTE_A表示这个内存地址是一个mmap映射的地址但是还没有被访问过，这个标志位是为了防止munmap时的一个问题，后面再说。

当线程第一次访问mmap映射的地址时会发生缺页中断，此时进入usertrap进行页面的分配和文件的映射，遍历线程的vmas，检查发生中断的地址是否是一个mmap映射的地址，如果是，那么进行处理：

```c
void usertrap(void) {
  // ...
  int cause = r_scause();
  // ...
  else if (cause == 13 || cause == 15) { // read/write page fault
    int i = 0;
    struct vma v;
    for (; i < 16; i++) {
      v = p->vmas[i];
      if (v.valid == 0) continue;
      if (v.start <= va && va <= v.start + v.len) break;
    }
    if (i == 16) { // 不是mmap映射的地址
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      setkilled(p);
      goto tail;
    }
    int f = PTE_U;
    if (v.prot & PROT_READ) f = f | PTE_R;
    if (v.prot & PROT_WRITE) f = f | PTE_W;
    if (v.prot & PROT_EXEC) f = f | PTE_X;
    // allocate physical memory
    uint64 pa = (uint64) kalloc();
    // read file from disk
    begin_op();
    ilock(v.f->ip);
    readi(v.f->ip, 0, pa, PGROUNDDOWN(va - v.start), PGSIZE);
    iunlock(v.f->ip);
    end_op();
    if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, pa, f) == -1) {
      printf("mappages for mmap page in trap failed\n");
      setkilled(p);
      goto tail;
    }
  // ...
  }
}
```

## munmap

munmap函数如下：

```c
int munmap(void *addr, size_t len);
```

从内存中释放从addr开始长度为len的映射文件，如果该段内存修改过且为MAP_SHARED，那么需要写回文件，如果vmas[i]中所有映射的内存都被释放，那么将释放该vmas[i]且将映射文件计数减一。

这里就涉及到区分已释放内存和未使用内存的问题，举一个例子，如果在mmap中对文件进行了映射，映射的空间为0-100，访问0-50范围的数据，然后调用munmap释放0-50范围的内存，在释放后，页表中0-100都为nullptr，munmap会直接将整个vmas[i]回收，因为此时范围内的内存都无效，但实际上50-100的内存还没有被使用过，不应该释放，因此用一个`PTE_A`来标识内存是否使用过。

```c
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
```

exit中需要把所有仍然有效的vmas[i]和对应的映射文件都释放：

```c
void exit(int status) {
  // ...
  for (int i = 0; i < 16; i++) {
    struct vma v = p->vmas[i];
    if (v.valid == 0) continue;
    // unmap the page
    for (uint64 j = v.start; j < v.start + v.len; j += PGSIZE) {
      pte_t* pte = walk(p->pagetable, j, 0);
      if (*pte == 0 || *pte == PTE_A) {
        *pte = 0;
        continue;
      }
      if ((v.flags & MAP_SHARED) && v.f->writable) {
        if (munmap_filewrite(v, j, PGSIZE) == -1) 
          panic("munmap_filewrite fail");
      }
      uvmunmap(p->pagetable, j, 1, 1);
    }
    fileclose(v.f);
    v.valid = 0;
  }
  // ...
}
```

fork：

```c
int fork(void) {
  // ...
  for (int i = 0; i < 16; i++) {
    if (p->vmas[i].valid == 0) continue;
    np->vmas[i].valid = 1;
    np->vmas[i].start = p->vmas[i].start;
    np->vmas[i].len = p->vmas[i].len;
    np->vmas[i].prot = p->vmas[i].prot;
    np->vmas[i].flags = p->vmas[i].flags;
    np->vmas[i].f = p->vmas[i].f;
    filedup(np->vmas[i].f);
  }
  // ...
}
```

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150116915.png" alt="image-20240528222215050" style="zoom:50%;" />

<img src="C:\Users\70970\AppData\Roaming\Typora\typora-user-images\image-20240528222144384.png" alt="image-20240528222144384" style="zoom:50%;" />



