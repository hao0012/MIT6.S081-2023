# xv6 trap流程

## 中断时的硬件操作

CPU在发生中断时需要保护现场：

1. 通过清除SIE来关中断
2. 将pc内容保存到sepc
3. 将当前模式保存在sstatus寄存器的SPP bit中
4. 将中断类型号保存在scause
5. 修改模式
6. 将stvec（uservec或kernelvec）保存到pc以开始执行内核代码

## 系统调用流程

### 函数调用流程

1. usys.pl: 将系统调用对应的编号填入到a7寄存器，然后调用ecall
2. ecall执行硬件操作，包括保存pc等
3. uservec(kernel/trampoline.S)
4. usertrap(kernel/trap.c)：发现中断类型号scause == 8，选择syscall函数执行
5. syscall(kernel/syscall.c)：通过a7寄存器得知系统调用号，调用对应的函数
6. usertrapret 
7. userret

### 难点

 从用户空间陷入需要考虑以下因素：

1. 在陷入时，RISC-V并不会切换页表，也就是说刚陷入完时使用的仍然是用户页表，此时用户页表必须包含`uservec`的映射，否则无法正确执行代码，`uservec`中进行页表的切换。（`stvec`存放着`uservec`的虚拟地址）
2. 由于`uservec`中会进行页表的切换，`uservec`在用户、内核页表中映射的位置必须一致（在`trampoline`页中）。

从用户空间陷入、执行系统调用的步骤：

1. 在用户空间执行`ecall`指令
2. 执行`uservec`：
   1. 交换`a0`和`sscratch`（存放`TRAPFRAME`页地址）的内容
   2. 将32个寄存器的内容保存在`TRAPFRAME`页面中
   3. 取出`TRAPFRAME`中保存的kernel stack地址、当前CPU的id、`usertrap`的地址等，切换为内核栈、内核页表等。
3. 执行`usertrap`，根据不同的中断类型进行不同的处理
4. 执行`usertrapret`和`userret`，在后者中切换回用户页表