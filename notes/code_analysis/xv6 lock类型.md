# xv6 lock类型

## spinlock

### 数据结构

```c++
struct spinlock {
  uint locked;
  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};
```

### acquire

通过不断循环的方式尝试加锁直到成功：

```c++
void acquire(struct spinlock *lk) {
  push_off();
  if(holding(lk)) panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
  lk->cpu = mycpu();
}
```

1. 为什么关中断？如果不关中断，在上锁后发生了中断，中断处理程序中可能会再次尝试获得该锁，但是无法获得，并且只有当该中断结束后才能回到原来程序中继续执行解锁，所以这里会死锁；除此之外关中断可以看作是对于过程的保护，其他线程不会中途同时进入临界区，保证了当前线程的独占，加锁等方式是对于资源的保护，保证了资源独占访问。
2. 采用test and set的原子操作进行上锁操作，`__sync_lock_test_and_set`是C中自带的实现，其中使用了RISC-V的指令amoswap
3. __sync_synchronized()用于防止指令重排序，该函数前的`load/store`指令都不能移动到该函数之后，防止锁失效。

### release

```c++
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;
  __sync_synchronize();

  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}
```

采用`__sync_lock_release`解锁而不是赋值，因为C中的赋值可能对应多条指令导致非原子操作。

>例如，对于CPU内的缓存，每一个cache line的大小可能大于一个整数，那么store指令实际的过程将会是：首先会加载cache line，之后再更新cache line。所以对于store指令来说，里面包含了两个微指令。这样的话就有可能得到错误的结果。所以为了避免理解硬件实现的所有细节，例如整数操作不是原子的，或者向一个64bit的内存值写数据是不是原子的，我们直接使用一个RISC-V提供的确保原子性的指令来将locked字段写为0。

## 中断函数

中断是CPU-wise的，每个CPU都有独自的中断。

### 开关中断 intr_on/off

设置sstatus寄存器的SIE bit。

```c++
static inline void intr_on() {
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

static inline void intr_off() {
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}
```

### 实现中断嵌套 push/pop_off

1. noff记录了当前中断的层数。
2. intena记录了旧的中断状态，用于恢复。

```c++
void push_off(void) {
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void pop_off(void) {
  struct cpu *c = mycpu();
  if(intr_get()) panic("pop_off - interruptible");
  if(c->noff < 1) panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
```

## sleeplock

睡眠锁指的是等待锁的进程让出CPU去睡眠，而不是说获得锁的进程去睡眠。

### 使用场景

在进行文件操作时花费的时间较长，如果文件的锁为spinlock，当前进程已经获得锁，其他进程也想要获得，此时其他进程就会进入长时间的自旋，浪费CPU时间。

sleeplock让其他想要获取锁的进程进入sleep，腾出CPU。

### 数据结构

```c++
struct sleeplock {
  uint locked;
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
};
```

其中也包含一个自旋锁，用于保护locked、name和pid这些sleeplock自身的状态。

### acquire

在sleep时会释放lk，wakeup后再次获取锁，因此可以保证到line6的进程只有一个，当获取锁后，其他进程依然在while循环中检查，此时会发现仍然上锁。

```c++
void acquiresleep(struct sleeplock *lk) {
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}
```

### release

```c++
void releasesleep(struct sleeplock *lk) {
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}
```



