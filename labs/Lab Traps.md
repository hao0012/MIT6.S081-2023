# Lab Traps

## RISC-V assembly

### 0x0

> Which registers contain arguments to functions? For example, which register holds 13 in main's call to `printf`?

a0-a7用于保存参数，a0也常用于存放返回地址。

### 0x1

> Where is the call to function `f` in the assembly code for main? Where is the call to `g`? (Hint: the compiler may inline functions.)

`f(8) + 1`在`main`中被编译器直接优化成了12，也就是说这个调用没有在汇编代码中。

`f`中`g`的调用被内联在了14-1a行处，可看下面的汇编代码：

```assembly
int g(int x) {
   0:	1141                	addi	sp,sp,-16
   2:	e422                	sd	s0,8(sp)
   4:	0800                	addi	s0,sp,16
  return x+3;
}
   6:	250d                	addiw	a0,a0,3
   8:	6422                	ld	s0,8(sp)
   a:	0141                	addi	sp,sp,16
   c:	8082                	ret

000000000000000e <f>:

int f(int x) {
   e:	1141                	addi	sp,sp,-16
  10:	e422                	sd	s0,8(sp)
  12:	0800                	addi	s0,sp,16
  return g(x);
}
  14:	250d                	addiw	a0,a0,3
  16:	6422                	ld	s0,8(sp)
  18:	0141                	addi	sp,sp,16
  1a:	8082                	ret

000000000000001c <main>:

void main(void) {
  1c:	1141                	addi	sp,sp,-16
  1e:	e406                	sd	ra,8(sp)
  20:	e022                	sd	s0,0(sp)
  22:	0800                	addi	s0,sp,16
  printf("%d %d\n", f(8)+1, 13);
  24:	4635                	li	a2,13
  26:	45b1                	li	a1,12
  28:	00000517          	auipc	a0,0x0
  2c:	7c850513          	addi	a0,a0,1992 # 7f0 <malloc+0xe8>
  30:	00000097          	auipc	ra,0x0
  34:	61a080e7          	jalr	1562(ra) # 64a <printf>
  exit(0);
  38:	4501                	li	a0,0
  3a:	00000097          	auipc	ra,0x0
  3e:	298080e7          	jalr	664(ra) # 2d2 <exit>
```

### 0x2

> At what address is the function `printf` located?

从上面的代码可以看到`printf`位于0x64a。

### 0x3

> What value is in the register `ra` just after the `jalr` to `printf` in `main`?

要先搞懂auipc和jalr的作用：

#### auipc

> AUIPC (add upper immediate to pc) is used to build pc-relative addresses and uses the U-type format. AUIPC forms a 32-bit offset from the 20-bit U-immediate, filling in the lowest 12 bits with zeros, adds this offset to the address of the AUIPC instruction, then places the result in register rd.

auipc的格式为U-type，是4种指令格式的一种，如下所示：

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150148670.png" alt="image-20231208135144527" style="zoom:50%;" />

功能用c语言表示就是：`rd = pc + (imm << 12)`，这里的rd是一个逻辑符号，在上面的汇编代码中指的是`ra`。

在上面汇编代码中的`auipc	ra,0x0`，即`ra = pc `

#### jalr

> The indirect jump instruction JALR (jump and link register) uses the I-type encoding. The target address is obtained by adding the sign-extended 12-bit I-immediate to the register rs1, then setting the least-significant bit of the result to zero. The address of the instruction following the jump(pc+4) is written to register rd. Register x0 can be used as the destination if the result is not required.

指令的格式：

![image-20231208150907280](https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150150234.png)

jalr就是一个跳转指令，它还设置rs1和rd的值：

```cpp
// rs1存放跳转目标地址
rs1 += imm; 			  
rs1 = rs1 & (0xFFFFFFFE); // 最低位置为0
// rd存放jalr后面那条指令的地址
rd = pc;
```

因此，在执行完`jalr 1562(ra)`后（这里隐含了rs1，rs1和rd都是ra），ra就被设置为jalr下面那条指令的地址。

其实不分析上面的东西也可以知道ra的值，因为它表示`return-address`，存放的是函数的返回地址，显然就是jalr后面的第一条指令的地址。

### 0x04

> ```cpp
> unsigned int i = 0x00646c72;
> printf("H%x Wo%s", 57616, &i);
> ```
>

> What is the output?

57616的16进制表示为`0xe110`，因此前半部分为`He110`。

RISC-V是小端序的，从上面的汇编代码可以看出按字节编址，根据ascii表对i的各个字节进行一一对应：

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150152095.gif" style="zoom:50%;" />

0x72对应`r`，0x6c对应`l`，0x64对应`d`，因此后半部分为`World`。

## Backtrace

任务：打印kernel stack中每个栈帧的`return address`，栈结构：

```txt
Stack
                   .
                   .
      +-> |       ...       |   |
      |   +-----------------+   |
      |   | return address  |   |
      |   |   previous fp ------+
      |   | saved registers |
      |   | local variables |
      |   |       ...       | <-+
      |   +-----------------+   |
      |   | return address  |   |
      +------ previous fp   |   |
          | saved registers |   |
          | local variables |   |
  $fp --> |       ...       |   |
          +-----------------+   |
          | return address  |   |
          |   previous fp ------+
          | saved registers |
  $sp --> | local variables |
          +-----------------+
```

注意：

1. `return address`指的是函数的return语句应该返回的地址，也就是说，这是一个指令的地址，是这个任务要打印的东西。
2. `previous fp`指的是上一个栈帧的地址，这是一个数据的地址，不是我们要打印的东西，我们用这个地址来进行栈的遍历，如果把它打印出来，值应该是递增的。
3. `fp`指向的是栈帧+8处，在使用时需要-8或-16。

实现：

```cpp
void backtrace(void) {
  printf("backtrace:\n");
  uint64 fp = r_fp();
  // page address range: [lo, hi)
  uint64 hi = PGROUNDUP(fp);
  while (fp < hi) {
    printf("%p\n", *((uint64 *)(fp - 8)));
    fp = *(uint64 *)(fp - 16);
  }
}
```

栈底的第一个栈帧的`return address`不用打印，那是一个garbage value。

![image-20231206135025154](https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150155558.png)

## Alarm

> 在本练习中，您将向 xv6 添加一项功能，该功能会在进程使用 CPU 时间时定期发出警报。 这对于想要限制其消耗的 CPU 时间的计算密集型进程，或者想要计算但也想要执行一些定期操作的进程可能很有用。 更一般地说，您将实现用户级中断/故障处理程序的原始形式； 例如，您可以使用类似的方法来处理应用程序中的页面错误。

这个任务让我们实现定期执行指定函数的功能，`sigalarm(0, 0)`特指取消定时执行。

整个流程：

1. 使用`sigalarm`告知进程要执行一个定期任务`handler()`
2. 在用户空间中执行代码...
3. 发生时钟中断
4. 在`usertrap()`中发现是时钟中断，且计时器到期，那么要执行`handler()`
5. 执行`handler()`
6. `handler`结尾执行`sigreturn()`
7. `sigreturn()`恢复第2步中的现场，返回用户空间
8. 重复2-7



修改`proc.h`如下：

```cpp
struct proc {
  // ...
  int n; // per n ticks
  int next; // ticks to next call 倒计时器
  uint64 handler; // handler address

  struct trapframe *alarmframe;
  int alarmflag;
};
```

在执行handler前，需要保存trapframe页中的信息，如果不保存，在handler的最后，通过sigreturn进入内核时会覆盖这部分信息，因此使用一个新的alarmframe页复制trapframe，就定义在trapframe下面：

```cpp
// memlayout.h
...
#define ALARMFRAME (TRAPFRAME - PGSIZE)
```

这个页面的管理方式有2种：

1. 第一种是和trapframe页一样，在进程创建时就分配，然后一直存在，直到进程销毁才释放。
2. 第二种是在sigalarm时创建，在`sigalarm(0, 0)`或进程销毁时释放。

我选择了第一种，因为代码和trapframe的管理类似，比较方便，代码的修改方式和lab page tables中的Speed up system calls任务类似。



`sys_sigalarm`中分别保存n和handler即可，要注意二者都为0时表示取消定时任务：

```cpp
uint64 sys_sigalarm(void) {
    struct proc *p = myproc();

    int n; argint(0, &n);
    uint64 handler; argaddr(1, &handler);

    if (n == 0 && handler == 0) {
        p->n = 0;
        p->next = 0;
        p->handler = 0;
        p->alarmflag = 0;
        return 0;
    }

    p->n = n;
    p->next = n;
    p->handler = handler;

    return 0;
}
```



`usertrap`中进行倒计时，时间到了就保存现场，然后设置pc到handler，这样中断结束会返回到handler执行：

```cpp
void usertrap(void) {
  // ...
  if(which_dev == 2) {
    yield();

    if (p->n > 0 && p->alarmflag == 0) {
      p->next--;
      if (p->next == 0) {
        p->next = p->n;
        //保护现场
        memmove(p->alarmframe, p->trapframe, 512); 
        // pc改为handler
        p->trapframe->epc = p->handler;
        p->alarmflag = 1;
      }
    }
  }
  usertrapret();
}
```



`sigreturn`中将alarmframe的内容复制回trapframe以恢复现场，这样pc的值又会改回原来的用户代码处，而不是再返回到handler中，要注意不要让syscall的返回值覆盖a0，因此这里的返回值直接设为a0：

```cpp
uint64 sys_sigreturn(void) {
    struct proc* p = myproc();
    memmove(p->trapframe, p->alarmframe, 512);
    p->alarmflag = 0;
    return p->trapframe->a0;
}
```

