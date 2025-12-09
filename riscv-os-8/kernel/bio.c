#include "type.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "def.h"
#include "fs.h"
#include "buf.h"


struct {
    struct spinlock lock;
    struct buf buf[NBUF];
    struct buf head; // LRU cache list
} bcache;

// Initialize the buffer cache.
void binit(void){
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    // create linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// Returns locked buffer.
static struct buf* bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf* bread(uint dev, uint blockno){
    struct buf *b;

    // printf("bread calling bget(dev=%d, blockno=%d)\n", dev, blockno);
    b = bget(dev, blockno);
    if(!b->valid){
        // printf("bread calling virtio_disk_rw(b=%x)\n", b);
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    // printf("bread returning b=%x\n", b);
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void brelse(struct buf *b)
{
  // printf("brelse called b=%x\n", b);
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
  // printf("brelse completed b=%x\n", b);
}

// Pin a buffer in the cache.
void bpin(struct buf *b){
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}

// Unpin a buffer from the cache.
void bunpin(struct buf *b){
    acquire(&bcache.lock);
    if(b->refcnt <= 0)
        panic("bunpin");
    b->refcnt--;
    release(&bcache.lock);
}