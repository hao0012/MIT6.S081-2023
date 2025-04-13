# Lab Lock

## Memory allocator

将内存管理的空闲链表从总共只有一个改为每个CPU一个，以减少并发的冲突。

设置`NCPU`个空闲链表和锁，每个CPU一个：

```c
struct {
    struct spinlock lock[NCPU];
    struct run *freelist[NCPU];
} kmem;
```

修改锁的初始化代码，初始化所有的锁：

```c
void kinit() {
    for (int i = 0; i < NCPU; i++) {
        initlock(&kmem.lock[i], "kmem");
    }
    freerange(end, (void*)PHYSTOP);
}
```

对于整个内存空间的划分，我采用了均分的方式，将从`end`到`PHYSTOP`范围内的空间均分为`NCPU`份，每个CPU使用对应空间中的内存。

判断物理地址`pa`属于哪个CPU：

```c
int blocknum(uint64 pa) {
    uint64 blocksize = ((uint64)PHYSTOP - (uint64)end) / NCPU;
    return (pa - (uint64)end) / blocksize;
}
```

修改`kalloc`，分配空间时，先在当前CPU拥有的空间中分配，如果没有空闲空间，那么去其他CPU拥有的空间中查找：

```c
void * kalloc(void) {
    struct run *r;
    push_off();
    int cpu = cpuid();
    pop_off();

    // 先从当前CPU拥有的空间中查找
    acquire(&kmem.lock[cpu]);
    r = kmem.freelist[cpu];
    if(r)
        kmem.freelist[cpu] = r->next;
    release(&kmem.lock[cpu]);
    if(r) { // 如果找到了，那么直接返回即可
        memset((char*)r, 5, PGSIZE); // fill with junk
        return (void*)r;
    }

    // 去其他CPU拥有的空间中查找
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
```



修改`kfree`，释放空间时，应该将空间还给管理这个空间的CPU，可能不是当前CPU，用上面所说的`blocknum`方法即可得到`pa`属于哪个CPU：

```c
void kfree(void *pa) {
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
```



<img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150058973.png" alt="image-20240102174426340" style="zoom:50%;" /><img src="https://raw.githubusercontent.com/hao0012/ImageStore/main/20250413150102675.png" alt="image-20240102174735241" style="zoom: 67%;" />

## Buffer cache

多个线程读取文件时会发生互斥，在磁盘缓冲区上发生很多阻塞。

> 修改块缓存，以便运行 bcachetest 时 bcache 中所有锁的循环次数接近于零。 理想情况下，块缓存中涉及的所有锁的计数总和应为零，但总和小于 500 也可以。修改 bget 和 brelse，以便对 bcache 中的不同块进行并发查找和释放不会造成锁冲突（例如，不必全部等待 bcache.lock）。 您必须保持每个块最多缓存一个副本的不变性。

使用Hash Table代替原来的单链表，给每个桶一个锁，降低锁的颗粒度。

### 磁盘缓冲区的原始结构

由一个单链表组成，空闲块和非空闲块都放在一起。查找某个磁盘块时整个链表都要上锁。

当找到磁盘块后，后续可能进行修改操作，给单个磁盘块上睡眠锁。

### 数据结构修改

哈希表的每个桶都是一个双向带头和尾的链表：

```c++
struct {
  struct spinlock bulock[NBUCKET];
  // 所有的缓存块
  struct buf buf[NBUF];
  // 所有头和尾
  struct buf head[NBUCKET];
  struct buf tail[NBUCKET];
} bcache;

int hashcode(struct buf* b) {
  return (b->blockno % NBUCKET);
}

void init_buf(struct buf* b) {
  b->valid = 0;
  b->disk = 0;
  b->refcnt = 0;
  b->prev = 0;
  b->next = 0;
}

void insert(struct buf* b) { // insert in head
  int i = hashcode(b);
  b->next = bcache.head[i].next;
  b->prev = &bcache.head[i];
  bcache.head[i].next->prev = b;
  bcache.head[i].next = b;
}

void remove(struct buf* b) {
  b->prev->next = b->next;
  b->next->prev = b->prev;
}
```

### binit

1. 初始化所有的锁
2. 创建链表块间的连接
3. 将所有的块放入对应的桶中

```c++
void
binit(void)
{
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bulock[i], "bcache");
  }

  // Create linked list of buffers
  for (int i = 0; i < NBUCKET; i++) {
    bcache.head[i].prev = 0;
    bcache.head[i].next = &bcache.tail[i];
    bcache.tail[i].prev = &bcache.head[i];
    bcache.tail[i].next = 0;
  }

  // 把所有块都初始化并插入到Hash Table中
  for(struct buf *b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    init_buf(b);
    insert(b);
  }
}
```

### bget

1. 检查请求的块是否已经载入了缓冲区中，如果有直接返回该块。
2. 检查当前桶中是否还有空闲块，有则直接返回该块。
3. 检查其余桶中是否还有空闲块，有则插入当前桶中。

```c++
static struct buf* bget(uint dev, uint blockno) { // blockno是磁盘块号，不是缓冲块号
  int i = blockno % NBUCKET;
  acquire(&bcache.bulock[i]);
  // 是否已经存在？
  struct buf *b;
  for(b = bcache.head[i].next; b != &bcache.tail[i]; b = b->next) {
    if(b->refcnt > 0 && b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.bulock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 当前桶中是否有空闲块？
  for(b = bcache.head[i].next; b != &bcache.tail[i]; b = b->next) {
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0; // 此时是空的，需要后续填充数据
      b->refcnt = 1;
      release(&bcache.bulock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bulock[i]);
  // 检查其他的桶中是否有空闲块
  for (int j = 0; j < NBUCKET; j++) {
    if (j == i) continue;
    acquire(&bcache.bulock[j]);
    for(b = bcache.head[j].next; b != &bcache.tail[j]; b = b->next) {
      // 找到一个空闲块
      if(b->refcnt == 0) {
        remove(b);
        release(&bcache.bulock[j]);
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0; // 此时是空的，需要后续填充数据
        b->refcnt = 1;
        acquire(&bcache.bulock[i]);
        insert(b);
        release(&bcache.bulock[i]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bulock[j]);
  }
  panic("bget: no buffers");
}
```

### brelse

减引用，如果减到0，那么释放该块：

```c++
void brelse(struct buf *b) {
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int i = hashcode(b);
  acquire(&bcache.bulock[i]);
  b->refcnt--;
  if (b->refcnt == 0) {
    remove(b);
    init_buf(b);
    insert(b);
  }
  
  release(&bcache.bulock[i]);
}
```

