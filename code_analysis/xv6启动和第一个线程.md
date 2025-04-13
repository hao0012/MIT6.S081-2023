# xv6启动和第一个线程

## xv6启动

1. 开启电源，执行boot loader（位于只读内存位置），boot loader将xv6内核代码载入内存的0x80000000处，此时：
   1. 处于machine mode
   2. 页表还没有启用，虚拟地址与物理地址一致

0x0000000-0x80000000用于设备寻址。

QEMU中，pc被设置为0x80000000，开始执行entry.S

2. 执行kernel/entry.S，设置每个CPU的栈指针sp以支持在创建第一个线程前的代码执行

```assembly
# qemu -kernel loads the kernel at 0x80000000
# and causes each hart (i.e. CPU) to jump there.
# kernel.ld causes the following code to
# be placed at 0x80000000.
.section .text
.global _entry
_entry:
# set up a stack for C.
# stack0 is declared in start.c,
# with a 4096-byte stack per CPU.
# sp = stack0 + (hartid * 4096)
la sp, stack0
li a0, 1024*4
csrr a1, mhartid
addi a1, a1, 1
mul a0, a0, a1
add sp, sp, a0
# jump to start() in start.c
call start

spin:
j spin
```

其中stack0是一个固定的全局数组，用作第一个线程的栈，位于.bss：

```c++
// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];
```

3. 执行kernel/start.c，进行一些计算机配置、开启时钟中断，然后通过调用mret将模式转为privileged mode并将pc改为main

```c++
// entry.S jumps here in machine mode on stack0.
void start() {
  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);

  // disable paging for now.
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

#ifdef KCSAN
  // allow supervisor to read cycle counter register
  w_mcounteren(r_mcounteren()|0x3);
#endif
  
  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}
```

4. 执行kernel/main.c，main中进行一些操作系统初始化工作，然后通过userinit创建第一个线程，最后scheduler进行线程切换再返回用户态：

```c++
// start() jumps here in supervisor mode on all CPUs.
void main() {
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk  
    userinit();      // first user process
#ifdef KCSAN
    kcsaninit();
#endif
    __sync_synchronize();
    started = 1;
  } else {
    while(atomic_read4((int *) &started) == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();
}
```

## 第一个控制台进程

1. 通过userinit创建第一个进程，在uvmfirst中设置执行代码为initcode.S：

```c++
void userinit(void) {
  struct proc *p = allocproc();
  initproc = p;
  // allocate one user page and copy initcode's instructions and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

void uvmfirst(pagetable_t pagetable, uchar *src, uint sz) {
  char *mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}
```

2. 执行user/initcode.S，其中调用exec将进程内容替换为user/init.c，exec返回后进程执行init.c

```assembly
# Initial process that execs /init. This code runs in user space.

#include "syscall.h"

# exec(init, argv)
.globl start
start:
        la a0, init
        la a1, argv
        li a7, SYS_exec
        ecall
# ...
```

3. init.c中创建一个控制台设备文件，启动shell

# 创建内核地址空间

该部分是main函数中执行的一部分功能。

## 内核页表

在xv6启动后执行main时，调用kvminit创建了内核地址空间。kvmmake在内核页表中进行了如下直接映射：

1. 设备地址，包括uart virtio PLIC等
2. .text段
3. .data段
4. trampoline页

最后为所有进程提前分配内核栈页

```c++
void kvminit(void) {
  kernel_pagetable = kvmmake();
}

pagetable_t kvmmake(void) {
  pagetable_t kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  return kpgtbl;
}

void proc_mapstacks(pagetable_t kpgtbl) {
  for(struct proc *p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}
```

注意，此时还没有启用页表机制，后续调用kvminithart才开始启用页表。

## 启用内核页表

kvminithart中将内核页表的指针存入stap寄存器中，该寄存器专门存放页表的物理地址。

```c++
void kvminithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  // flush stale entries from the TLB.
  sfence_vma();
}
```

这里的sfence_vma用于刷新TLB，让之前的TLB失效：

```c++
static inline void sfence_vma() {
  // the zero, zero means flush all TLB entries.
  asm volatile("sfence.vma zero, zero");
}
```

在trampoline中切换页表时也存在这个汇编调用。

### 为进程配置内核栈

```c++
// initialize the proc table.
void procinit(void) {
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(struct proc * p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int) (p - proc));
  }
}
```

