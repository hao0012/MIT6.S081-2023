# IO

## 外部中断的产生

### 主板

设备连接到主板上：

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150221310.png" alt="image-20250204194550534" style="zoom:33%;" />

### 内存映射

设备被映射到内存中的指定地址处，对该地址执行load/store指令即可读写设备。

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150222504.png" alt="image-20250204194758281" style="zoom:33%;" />

### PLIC

CPU上通过Platform Level Interrupt Control处理设备中断。如图所示，外部中断全部发给PLIC，PLIC通知所有CPU，但只有一个没有在处理中断的CPU会告诉PLIC由它来处理中断，然后PLIC将请求发给该CPU处理，如果所有CPU都在处理中断，则等待。

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150226913.png" alt="image-20250204195022660" style="zoom:33%;" />

### UART

QEMU通过UART这个设备与键盘、console等外部设备进行通信。UART中包含若干个寄存器。

<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150223729.png" alt="image-20250204201120074" style="zoom:33%;" />

## 设备IO

设备驱动通常分为bottom/top两部分:

1. top: 从计算机内部向外部发送数据（输出）
2. bottom: 从外部向计算机内部发送数据（输入）

设备驱动通常有一个数据缓冲区，可以将设备和CPU解耦，设备和CPU分别向缓冲区中读写数据。

### top部分——以printf为例

在user/printf.c中的printf函数中，实际上调用了putc这个函数，而putc是write系统调用的封装，将字符输出到指定的fd中。

#### write

write中调用filewrite，而后者根据文件类型调用不同的函数处理：

1. pipe: 调用pipewrite
2. device: 调用对应设备的write函数（）
3. inode: 进行磁盘写

```c++
int filewrite(struct file *f, uint64 addr, int n) {
  // ...
  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // ... 
  }
  // ...
}
```

这里console的write函数为consolewrite。

#### consolewrite

```c++
// user write()s to the console go here.
int consolewrite(int user_src, uint64 src, int n) {
  int i;
  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c);
  }
  return i;
}
```

1. either_copyin：将字符从用户态拷贝到内核态
2. uartputc: 将字符传递给UART

#### uartputc

将一个字符添加到UART的输出缓冲区中，并告诉UART开始输出。如果缓冲区满了，则会阻塞。该缓冲区可以看作单生产者-单消费者模型，分别有一个读/写指针。

步骤：

1. sleep等待缓冲区不满
2. 将字符写入缓冲区
3. uartstart: 通知UART开始传输

```c++
void uartputc(int c) {
  acquire(&uart_tx_lock);
  
  if(panicked){
    for(;;);
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  release(&uart_tx_lock);
}
```

#### uartstart

如果UART为空并且有字符等待传输，则开始传输，该函数可以被top和bottom两方调用，如果被top调用，则是从计算机输出数据到设备；如果被bottom调用，则是从外部设备读取数据。

1. 如果缓冲区为空，或UART中所有的寄存器都满了，则直接返回，UART在合适时间会通过中断通知CPU可以发出数据了
2. 将字符写入到UART的THR(Transmitter Holding Register)寄存器，这相当于告诉UART要发送该字符。

```c++
void uartstart() {
  while(1){
    if(uart_tx_w == uart_tx_r){
      return;
    }
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    WriteReg(THR, c);
  }
}
```

### bottom部分

bottom部分即外部中断处理程序，当设备的中断发来了，CPU需要调用对应设备的中断处理程序进行处理，该程序的运行不依赖于进程的上下文。

当用键盘输入字符时，键盘向PLIC发送一个中断请求，PLIC将该请求交给一个CPU，该CPU执行以下硬件操作:

1. 关中断，清空SIE bit，直到完成这次外部中断处理
2. 保存pc到sepc
3. 保存当前模式（用户态或内核态），然后设置为内核态
4. 设置pc为stvec，执行usertrap
5. usertrap中调用devintr

#### devintr

检查是什么类型的设备中断：

2: 时钟中断

1: 除时钟外其他设备（UART/磁盘）的中断

0: 未识别的类型

如果是1，则通过plic_claim询问PLIC外部设备中断的类型。

```c++
int devintr() {
  uint64 scause = r_scause();
  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.
    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // ... 时钟中断
    return 2;
  } else {
    return 0;
  }
}
```

#### uartintr

UART的中断处理程序：

1. 循环调用uartgetc获取UART中缓存的输入字符，将字符通过consoleintr再次将字符输出到UART的缓冲区
2. uartstart让UART开始将字符输出到控制台，这一步在前面的printf中已经讲过了

```c++
void uartintr(void) {
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }
  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}

int uartgetc(void) {
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  }
  return -1;
}
```

#### consoleintr

consolewrite是将用户态的数据先拷贝到内核态然后再输出到UART，而consoleintr的数据已经在内核态了，只需要将数据输出到UART即可，同样调用的是uartputc。

