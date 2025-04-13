# Page Tables

## Speed up system calls

通过使用内核和用户都可以访问到的页面，我们在使用系统调用时可以不进行状态的切换而得到需要的数据，可以加快系统调用速度。

每个进程的内存空间中分配一个用户可以访问的页面，其中存储进程的id，数据结构如下：

```cpp
#define USYSCALL (TRAPFRAME - PGSIZE)
struct usyscall {
    int pid;  // Process ID
};
```

任务：在进程的生命周期中，对这个页面进行管理，类似地可以看老师对于`trapframe`页面的处理。

在`proc`中添加一个指向该页面的指针：

```cpp
struct proc {
  // ...
  struct usyscall* usyscall;
};
```

创建进程时就要分配这个页面，并将id存到里面：

```cpp
static struct proc* allocproc(void) {
  // ...

found:
  // ...
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  
  if ((p->usyscall = (struct usyscall *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid = p->pid;

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  // ...
  return p;
}
```

同样，在释放进程时也要释放这个页面：

```cpp
static void freeproc(struct proc *p) {
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if (p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  // ...
}
```

`p->usyscall`存放的是这个页面的逻辑地址，这个页面所在的物理地址是`USYSCALL`（kernel/memlayout.h），要在页表中建立2者的映射：

```cpp
pagetable_t proc_pagetable(struct proc *p) {
  // ...
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0) {
    uvmunmap(pagetable, USYSCALL, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
  return pagetable;
}
```



在删除页表时也要删除这个映射：

```cpp
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmunmap(pagetable, USYSCALL, 1, 0);
  uvmfree(pagetable, sz);
}
```

OK啦。

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150132136.png" alt="image-20231127155544144" style="zoom:50%;" />

## Print a page table

要我们打印出页表。

采用递归的方法，参考一下老师写的`walk`对页表的递归。

要注意的是，1、2级页表中的pte（非叶节点）的标志位是VWRX，而3级页表中的pte（叶节点）的标志位只有V。

我在老师的方法基础上改进了一下对pte的判断逻辑，感觉老师写得不好，哈哈，请看：

```cpp
int vmprint(pagetable_t p, int deepth) {
  if (deepth == 1) {
    printf("page table %p\n", p);
  }
  // 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = p[i];
    if ((pte & PTE_V) == 0) continue;

    for (int j = 0; j < deepth; j++) {
      printf(" ..");
    }
    uint64 pa = PTE2PA(pte);
    printf("%d: pte %p pa %p\n", i, pte, pa);

    if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) { // branch
      vmprint((pagetable_t)pa, deepth + 1);
    }
  }
  return 0;
}
```

OK啦。

![image-20231129195027258](https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150135760.png)

## Detect which pages have been accessed

> 任务：实现 `pgaccess()`，这是一个报告哪些页面已被访问的系统调用。 系统调用需要三个参数。 首先，它需要检查第一个用户页面的起始虚拟地址。 其次，需要检查页数。 最后，它将用户地址发送到缓冲区，以将结果存储到位掩码（每页使用一位的数据结构，其中第一页对应于最低有效位）。 如果 pgaccess 测试用例在运行 pgtbltest 时通过，您将获得这部分实验的全部学分。

这个函数可以如下定义：

```cpp
int pgaccess(void* va_start, int page_num, uint64* bits);
```

- va_start为要检查的连续若干页的第一页的虚拟地址
- page_num为要检查的页数
- bits的每一bit存放1页的结果

在[RISC-V privileged architecture manual](https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMFDQC-and-Priv-v1.11/riscv-privileged-20190608.pdf)的4.3.1节中介绍了A标志位：

> Each leaf PTE contains an accessed (A) and dirty (D) bit. The A bit indicates the virtual page has been read, written, or fetched from since the last time the A bit was cleared

![image-20231130152946228](https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150139069.png)

先用类似`walk`的方法找到`va_start`对应的第3级页表中的页表项，然后从这个页表项开始检查`page_num`个页表项的`PTE_A`标志位，如果为1，则`bits`对应位置为1。

`riscv.h`中定义PTE_A：

```cpp
#define PTE_A (1L << 6)
```

`sysproc.h`中实现`pgaccess`：

```cpp
int sys_pgaccess(void) {
  uint64 va; argaddr(0, &va); // 第一个要检查的页的虚拟地址

  struct proc* p = myproc();
  pagetable_t pgt = p->pagetable;

  // 找到第3级页表，存放在pgt中
  for (int l = 2; l > 0; l--) {
    pte_t *pte = &pgt[PX(l, va)];
    if (*pte & PTE_V) { // valid pte
      pgt = (pagetable_t)PTE2PA(*pte);
    } else {
      printf("invalid pte\n");
      return -1;
    }
  }
  int num; argint(1, &num); // num为要检查的页数
  uint64 bitmask = 0; // 存放检查结果
  for (uint64 i = 0; i < num; i++) {
    uint64 idx = PX(0, va) + i;
    if (idx >= 512) {
      printf("out of range.\n");
      return -1;
    }
    pte_t *pte = &pgt[idx];
    // 如果页有效，且PTE_A == 1
    if (((*pte & PTE_V) == 1) && ((*pte & PTE_A) >> 6) == 1) {
      bitmask |= (1L << i);
    }
    // 及时清空
    *pte = (*pte & (~PTE_A)); // PTE_A = 0
  }
  uint64 store; argaddr(2, &store);
  // 将结果存到用户空间中
  copyout(p->pagetable, store, (char *)(&bitmask), sizeof(bitmask));
  return 0;
}
```

要注意的是，上面的实现中没有考虑要检查的若干页表项可能跨页表的情况，但是也通过了测试：

<img src="C:\Users\70970\AppData\Roaming\Typora\typora-user-images\image-20231201160129469.png" alt="image-20231201160129469" style="zoom:50%;" />

