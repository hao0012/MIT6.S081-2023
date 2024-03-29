// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock bulock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

int hashcode(struct buf* b) {
  return (b->blockno % NBUCKET);
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

void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bulock[i], "bcache");
  }

  // Create linked list of buffers
  for (int i = 0; i < NBUCKET; i++) {
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }
  
  int c = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    insert(b);
    if (++c == NBUCKET) c = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int i = blockno % NBUCKET;
  acquire(&bcache.bulock[i]);
  // Is the block already cached?
  for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bulock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bulock[i]);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (int j = 0; j < NBUCKET; j++) {
    if (j == i) continue;
    acquire(&bcache.bulock[j]);
    for(b = bcache.head[j].prev; b != &bcache.head[j]; b = b->prev){
      if(b->refcnt == 0) {
        remove(b);
        release(&bcache.bulock[j]);
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
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

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int i = hashcode(b);
  acquire(&bcache.bulock[i]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[i].next;
    b->prev = &bcache.head[i];
    bcache.head[i].next->prev = b;
    bcache.head[i].next = b;
  }
  
  release(&bcache.bulock[i]);
}

void
bpin(struct buf *b) {
  int i = hashcode(b);
  acquire(&bcache.bulock[i]);
  b->refcnt++;
  release(&bcache.bulock[i]);
}

void
bunpin(struct buf *b) {
  int i = hashcode(b);
  acquire(&bcache.bulock[i]);
  b->refcnt--;
  release(&bcache.bulock[i]);
}


