#include "type.h"
#include "riscv.h"
#include "def.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"


struct logheader{
    int n;
    int block[LOGBLOCKS];
};

struct log{
    struct spinlock lock;
    int start;
    int outstanding; // how many FS syscalls are executing.
    int committing;  // in commit(), please wait.
    int dev;
    struct logheader lh;
} log;

static void recover_from_log(void);
static void commit();

// 初始化日志系统
void initlog(int dev, struct superblock *sb){
    printf("initlog: dev=%d sb=%x\n", dev, sb);
    if(sizeof(struct logheader) >= BSIZE)
        panic("initlog: too big logheader");
    initlock(&log.lock, "log");
    log.start = sb->logstart;
    log.dev = dev;
    recover_from_log();
}

// 从日志中恢复数据
static void install_trans(int recovering){
    int tail;
    for (tail = 0; tail < log.lh.n; tail++) {
    if(recovering) {
      printf("recovering tail %d dst %d\n", tail, log.lh.block[tail]);
    }
    struct buf *lbuf = bread(log.dev, log.start+tail+1); 
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); 
    memmove(dbuf->data, lbuf->data, BSIZE); 
    bwrite(dbuf);  
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void read_head(){
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *lh = (struct logheader *)buf->data;
    int i;
    log.lh.n = lh->n;
    for(i = 0; i < log.lh.n; i++){
        log.lh.block[i] = lh->block[i];
    }
    brelse(buf);
}

// Write in-memory log header to disk
static void write_head(){
    struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

// 从日志中恢复数据
static void recover_from_log(void){
    read_head();
    install_trans(1); // recovering = 1
    log.lh.n = 0;
    write_head();
}

// 开始一个文件系统操作
void begin_op(void){
    acquire(&log.lock);
    while(1){
        if(log.committing){
            sleep(&log, &log.lock);
        } else if(log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGBLOCKS){
            sleep(&log, &log.lock);
        } else {
            log.outstanding++;
            release(&log.lock);
            break;
        }
    }
}

// 结束一个文件系统操作
void end_op(void){
    int do_commit = 0;

    acquire(&log.lock);
    log.outstanding--;
    if(log.committing)
        panic("end_op: log.committing");
    if(log.outstanding == 0){
        do_commit = 1;
        log.committing = 1;
    } else {
        wakeup(&log);
    }
    release(&log.lock);

    if(do_commit){
        // call commit w/o holding locks
        commit();
        acquire(&log.lock);
        log.committing = 0;
        wakeup(&log);
        release(&log.lock);
    }
}

// Copy modified blocks from cache to log.
static void write_log(){
    int tail;
    for(tail = 0; tail < log.lh.n; tail++){
        struct buf *to = bread(log.dev, log.start + tail + 1);
        struct buf *from = bread(log.dev, log.lh.block[tail]);
        memmove(to->data, from->data, BSIZE);
        bwrite(to);
        brelse(from);
        brelse(to);
    }
}

// Commit a log transaction
static void commit(){
    if(log.lh.n > 0){
        write_log();     
        write_head();    
        install_trans(0);// recovering = 0
        log.lh.n = 0;
        write_head();// clear the log
    }
}

// Add the block to the log.  Copy to log if necessary.
void log_write(struct buf *b){
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGBLOCKS)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorption
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}