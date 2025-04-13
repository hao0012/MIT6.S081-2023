## trace

> 在此作业中，您将添加系统调用跟踪功能，该功能可以在调试后续实验时为您提供帮助。 您将创建一个新的跟踪系统调用来控制跟踪。 函数含有一个参数int mask，其bit位指定要跟踪哪些系统调用。 例如，要跟踪 fork 系统调用，程序调用trace(1 << SYS_fork)，其中SYS_fork 是来自kernel/syscall.h 的系统调用号。 如果掩码中设置了系统调用的编号，则必须修改 xv6 内核，以便在每个系统调用即将返回时打印一行。 该行应包含进程 ID、系统调用名称和返回值； 您不需要打印系统调用参数。 跟踪系统调用应该启用对调用它的进程及其随后派生的任何子进程的跟踪，但不应影响其他进程。

分别在各个部分添加上系统调用的声明：

> Run make qemu and you will see that the compiler cannot compile `user/trace.c`, because the user-space stubs for the system call don't exist yet: add a prototype for the system call to `user/user.h`, a stub to `user/usys.pl`, and a syscall number to `kernel/syscall.h`. The Makefile invokes the perl script `user/usys.pl`, which produces `user/usys.S`, the actual system call stubs, which use the RISC-V `ecall` instruction to transition to the kernel. Once you fix the compilation issues, run trace 32 grep hello README; it will fail because you haven't implemented the system call in the kernel yet.

在`proc`中添加`int mask`：

```cpp
// Per-process state
struct proc {
    
    ...    
    int mask;
};
```

在`fork`中添加：

```cpp
np->mask = p->mask;
```

在`kernel/syscall.c`中添加：

```cpp
// Prototypes for the functions that handle system calls.
...
extern uint64 sys_trace(void);

...
static char* syscall_name[32] = {
    "fork",
    "exit",
    "wait",
    "pipe",
    "read",
    "kill",
    "exec",
    "fstat",
    "chdir",
    "dup",
    "getpid",
    "sbrk",
    "sleep",
    "uptime",
    "open",
    "write",
    "mknod",
    "unlink",
    "link",
    "mkdir",
    "close",
    "trace",
};
```

修改`syscall`函数如下：

```cpp
void syscall(void) {
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
    // 判断这个系统调用是否是我们跟踪的那个//
    if (((p->mask >> num) & 0x1) == 1) {
      printf("%d: syscall %s -> %d\n", p->pid, syscall_name[num - 1], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

## sysinfo

> 添加一个系统调用 sysinfo，用于收集有关正在运行的系统的信息。该系统调用有一个参数`struct sysinfo*`（请参阅 kernel/sysinfo.h）。 内核应填写此结构体的字段：`freemem`字段应设置为可用内存的字节数，`nproc` 字段应设置为状态不是 UNUSED 的进程数。 我们提供了一个测试程序sysinfotest； 如果打印出“sysinfotest: OK”，则说明您通过了此作业。

和trace类似，在各个部分添加上sysinfo的声明，然后再实现：

### 空闲内存大小

在`kernel/kalloc.c`中实现读取空闲内存的函数`kgetmem`：

```cpp
int kgetmem(void) {
    struct run* head = kmem.freelist;
    int count = 0;
    while (head) {
        count++;
        head = head->next;
    }
    return count * PGSIZE;
}
```

浏览`kalloc.c`，发现`kmem.freelist`即为存放空闲内存的链表，只需遍历它即可。

### 进程数

在`kernel/proc.c`中实现读取`state != UNUSED`的进程：

```cpp
int procnum(void) {
    int count = 0;
    for (struct proc* p = proc; p < &proc[NPROC]; p++) {
        if (p->state != UNUSED) {
            count++;
        }
    }
    return count;
}
```

浏览`proc.c`，发现所有的进程都存放在`proc[NPROC]`数组中，只需遍历它即可。

### sys_sysinfo

在`sysproc.c`中实现`sys_sysinfo`函数：

```cpp
uint64 sys_sysinfo(void) {
    uint64 addr; 
    argaddr(0, &addr); // 获取在用户空间的sysinfo*
    struct sysinfo* info = (struct sysinfo*) addr;
    int free_mem = kgetmem();
    int pnum = procnum();

    struct proc* p = myproc();
    // 复制到用户空间
    if(copyout(p->pagetable, (uint64)(&(info->freemem)), (char *)(&free_mem), sizeof(free_mem)) < 0)
        return -1;

    if(copyout(p->pagetable, (uint64)(&(info->nproc)), (char *)(&pnum), sizeof(pnum)) < 0)
        return -1;
    return 0;
}
```

看一下`copyout`的函数注释以及在其他文件的使用学一下怎么用就行了，将得到的空闲内存数和进程数用`copyout`分别复制出去就行了。

