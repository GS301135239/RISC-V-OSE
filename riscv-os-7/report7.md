# 实验7：文件系统

## 系统设计部分

### 系统架构部分

其中核心文件的作用如下：

- virtio.h：虚拟磁盘头文件
- virtio_disk.c：磁盘读写操作与I/O中断的处理程序代码
- bio.c：缓冲区读写I/O操作代码
- buf.h：缓冲区结构体定义
- file.h/.c：文件、inode结构体的定义和新增/减少文件计数、文件读写操作
- fs.h/.c：设备文件系统的结构体定义和设备文件系统操作
- log.c：日志系统代码，负责保证缓存和磁盘文件系统数据的一致性和事务管理操作
- sleeplock.h/.c：睡眠锁的实现，基于spinlock的改进，减少因为使用在长阻塞场景下（如磁盘I/O）使用自旋锁而导致的内核性能浪费
- plic.c：xv6系统下初始化和操作中断控制器，使操作系统能够正确处理外部设备（如 UART、VIRTIO）的中断请求
- mkfs.c：创建文件系统的代码，本实验一切实现的基础


### 与 xv6 对比分析

- 我设计的文件系统主要是根据xv6源代码中文件系统的设计思路实现的，由于我设计的操作系统还未实现ELF的导入和完整的用户态文件系统操作，因此在设计过程中优化了sleeplock、virtio_disk中断管理等多个模块的设计和判断，提高了系统的健壮性，使得文件系统能够在启动阶段就能完成对于其设计功能的测试。

## 实验过程部分

### 实验步骤

#### 1）实现sleeplock睡眠锁

- 睡眠锁是基于自旋锁spinlock的改进，其主要实现原理是利用proc部分中的睡眠机制，对于未获得锁的进程，使其在对应的channel上睡眠，放弃轮询的坚持等待，这种实现对于文件系统的IO长时间操作，可以避免使用spinlock导致的反复轮询对CPU性能的浪费

```c

// 获取睡眠锁
void acquiresleep(struct sleeplock *lk)
{
    acquire(&lk->lk);// 先获取保护睡眠锁的自旋锁

    while(lk->locked) {
        if(myproc() == 0)
            panic("acquiresleep");
        sleep(lk, &lk->lk);// 睡眠等待锁释放
    }

    lk->locked = 1;// 获取锁成功
    
    if(myproc())
        lk->pid = myproc()->pid;// 记录持有锁的进程ID
    else
        lk->pid = 0;

    release(&lk->lk);// 释放保护睡眠锁的自旋锁
}

// 释放睡眠锁
void releasesleep(struct sleeplock *lk)
{
    acquire(&lk->lk);// 先获取保护睡眠锁的自旋锁

    if(lk->locked == 0) {
        panic("releasesleep");
    }// 锁未被持有或不是当前进程持有，报错

    if(myproc() != 0 && lk->pid != myproc()->pid) {
        panic("releasesleep");
    }// 不是当前进程持有锁，报错

    lk->locked = 0;// 释放锁
    lk->pid = 0;// 清除持有锁的进程ID

    if(myproc() != 0)
        wakeup(lk);// 唤醒等待该锁的进程

    release(&lk->lk);// 释放保护睡眠锁的自旋锁
}

```

- 这一部分与xv6对比也作出了一定的改变，对于myproc == 0的情况不再作出判断，这样的改变保证了在测试文件系统的过程中不会因为没有initproc的存在而返回0，导致在后续步骤中对空指针进行解引用而出现错误（因为这个错误是不报PANIC，而是报kernel_trap里scause为D的缺页错误！在这里被卡了好久好久……）

#### 2）实现virtio.h和virtio_disk.c虚拟磁盘部分

- 这部分关于磁盘初始化的代码主要操作与现实分盘操作完全一致，主要步骤为先加载一个虚拟磁盘，再在内存中加载一部分空间分配给每一个子盘，然后为每一个子盘分配一个描述符（与现实中C/D……盘思路完全相同）

```c

  disk.desc = alloc();
  disk.avail = alloc();
  disk.used = alloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

```

- 磁盘读写部分，这一部分主要依赖中断来进行的，当需要读写磁盘时，先使用中断，申请睡眠锁，再由读写磁盘函数将需要读写磁盘的线程提供的数据写回磁盘中

```c

while(b->disk == 1) {
    if(myproc() != 0) {
        // 正常模式：有进程上下文，可以睡眠等待中断唤醒
        sleep(b, &disk.vdisk_lock);
    } else {
        // 启动模式：没有进程上下文，必须使用忙等待（轮询）
        // 必须释放锁，以便中断处理程序(virtio_disk_intr)能获取锁并更新 b->disk
        release(&disk.vdisk_lock);
        
        // 开启中断，让 PLIC 能接收磁盘中断
        // 注意：在 S 模式下，sstatus.SIE 位控制中断
        intr_on(); 
        
        // 等待一小会儿（或者什么都不做，纯轮询）
        // 这里其实是在等待 virtio_disk_intr 被触发
        // 当中断发生时，CPU 跳转到 trap -> kernelvec -> devintr -> virtio_disk_intr
        // virtio_disk_intr 会修改 b->disk = 0
        
        // 重新获取锁以检查 b->disk
        intr_off(); // 关中断以保证原子性（视你的自旋锁实现而定，通常 acquire 会关中断）
        acquire(&disk.vdisk_lock);
    }
  }

```
- 这段代码主要对原xv6作出了一定的改变，因为为了简单实现，我的文件系统测试在启动过程中就进行，但是此时并没有进程上下文，因此并没有办法获取睡眠锁，因此这里将中断打开一段时间，使得磁盘读写中断能够被触发，然后测试函数才能正常读写磁盘

#### 3）实现buf.h和bio.c缓冲区代码

- buf.h的实现

```c

struct buf {
  int valid;   
  int disk;    
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; 
  struct buf *next;
  uchar data[BSIZE]; __attribute__((aligned(8)));
};

```

- 这里主要实现的是缓冲区的结构体，prev、next指针主要维护一个LRU Cache，而对于数据需要8字节强制对齐是为了防止在缓冲区与磁盘IO交互时产生数据越界等错误。

- bio.c

- 这个文件主要实现的是缓冲区的管理方法，包括初始化、获得Cache块等操作，通过LRU策略来实现Cache块的管理

```c

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

```

#### 4）实现fs.h和fs.c

- fs.h部分的代码主要是实现系统设备层级的文件系统，通过dinode来实现文件节点的管理，同时引入位图bitmap提升访问空闲块的效率
- fs.c部分的代码主要是系统设备层级文件系统的具体实现，同时实现bitmap位图来进行高效的内存管理，最后实现的文件系统读写的主要步骤为：先访问位图所在的inode块，遍历位图，找到空闲的块并插入。
- fs.c的主要遍历方式为二级间接遍历，因此在删除文件内容或文件缩短到0字节回收时需要对直接块和间接块进行释放。
- fs.c层的主要作用是提供用户级别文件系统应用和磁盘读写之间的中间层
- 这个文件主要关注的是文件系统内部结构和磁盘操作，负责磁盘块分配/释放（如 balloc、bfree）。
- 负责inode（索引节点）管理，如分配、锁定、释放、同步到磁盘（ialloc、ilock、iput、iupdate 等）。
- 实现文件内容的实际读写（readi、writei），以及目录项操作（dirlookup、dirlink）。
- 处理文件截断（itrunc），以及路径解析（namei、nameiparent）。
- 直接操作磁盘和缓存（buffer），是文件系统的底层实现。

```c

void itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

static uint bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

static struct inode* namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}


```

- 文件名解析的主要实现是基于递归实现的，通过对“/”符号的解析和递归，处理文件路径的自上而下解析以及各种文件路径的判断处理

#### 5）实现log.c

- 这个文件主要是实现日志系统，实现的思路也比较简单，就是通过日志系统的commit操作保证读写一致性，通过begin_op和end_op来实现文件系统操作的原子性，避免由于并发和竞争导致的文件系统操作不一致性，同时提供了崩溃后从日志中恢复、重做操作的文件系统功能。

```c

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



```

#### 6）实现file.h和file.c

- 这部分的代码主要实现用户级别的应用文件系统管理，这个文件管理代码主要是实现管理进程打开的文件，为进程的文件管理提供进程可用的底层操作系统包装的接口

```c

// 分配文件结构体
struct file* filealloc(void){
    struct file *f;

    acquire(&file_table.lock);
    for(f = file_table.file; f < &file_table.file[NFILE]; f++){
        if(f->ref == 0){
            f->ref = 1;// 分配成功
            release(&file_table.lock);
            return f;
        }
    }
    release(&file_table.lock);
    return 0;
}

// 增加文件引用计数
struct file* filedup(struct file *f){
    acquire(&file_table.lock);
    if(f->ref < 1)
        panic("filedup");
    f->ref++;
    release(&file_table.lock);
    return f;
}

// 关闭文件，减少引用计数，必要时释放文件结构体
void fileclose(struct file *f){
    struct file ff;
    acquire(&file_table.lock);
    if(f->ref < 1)
        panic("fileclose");
    if(--f->ref > 0){
        release(&file_table.lock);
        return;
    }

    ff = *f;
    f->ref = 0;// 标记文件结构体为空闲
    f->type = FD_NONE;
    release(&file_table.lock);

    // 释放文件资源
    if(ff.type == FD_DEVICE || ff.type == FD_INODE){
        // 设备文件或普通文件，释放 i节点
        begin_op();
        iput(ff.ip);
        end_op();
    }

}

```

#### 7）实现plic.c

- 这个文件主要作用是初始化和操作中断控制器，使操作系统能够正确处理外部设备（如 UART、VIRTIO）的中断请求，从而实现基于中断的磁盘IO操作

```c
void plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

void plicinithart(void)
{
  int hart = cpuid();
  
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

int plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

void plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
```

- plicinit()：初始化 PLIC，设置外部设备（如 UART0 和 VIRTIO0）的中断优先级为非零，使其能够被响应。
- plicinithart()：为当前 hart（硬件线程/CPU核）配置可响应的中断源，并设置中断优先级阈值。
- plic_claim()：用于当前 hart 获取（claim）一个待处理的中断号。
- plic_complete(int irq)：通知 PLIC 当前 hart 已经处理完指定的中断号，允许后续中断继续触发。

#### 8）改进trap.c实现外部中断的分发

```c

case 0x8000000000000009L:
            int irq = plic_claim();
            if ( irq == VIRTIO0_IRQ ) {
                virtio_disk_intr();
            } else if ( irq ) {
                printf( "handle_device_intr: unexpected interrupt irq = %d\n", irq );
            }
            if ( irq ) {
                plic_complete( irq );
            }
            return 1;


```

- 这段代码主要作用是对于外部中断（中断号为9）来进行分发，当触发磁盘IO中断时触发plic.c中claim函数触发中断处理分发，再调用磁盘中断处理函数，这个中断分发的逻辑是用户态下实现磁盘IO读写内核态转换的基础。

#### 9）实现mkfs/mkfs.c

- 上面实现的文件系统要想真正运行起来，则需要编译期的时候在用户层级为操作系统生成一个“真正的”文件系统，这样fs.c和磁盘的初始化才能够实现MAGIC、version等的匹配，实现真正的文件系统构建。
- 而这个文件的主要作用就是按照xv6文件系统的布局创建并初始化一个新的文件系统镜像（通常叫做“格式化”磁盘）。它会在指定的磁盘镜像文件（如 fs.img）上。

```c

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/BPB + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGBLOCKS+1;   // Header followed by LOGBLOCKS data blocks.
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void die(const char *);

// convert to riscv byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]);

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;

  sb.magic = FSMAGIC;
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u, inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 2; i < argc; i++){
    // get rid of "user/"
    char *shortname;
    if(strncmp(argv[i], "user/", 5) == 0)
      shortname = argv[i] + 5;
    else
      shortname = argv[i];
    
    assert(index(shortname, '/') == 0);

    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(shortname[0] == '_')
      shortname += 1;

    assert(strlen(shortname) <= DIRSIZ);
    
    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(freeblock);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BPB);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

void
die(const char *s)
{
  perror(s);
  exit(1);
}

```

#### 10）修改Makefile文件

```Makefile

mkfs/mkfs: mkfs/mkfs.c kernel/fs.h kernel/param.h
	gcc -Werror -Wall -I. -o mkfs/mkfs mkfs/mkfs.c

fs.img: mkfs/mkfs $(USER_INIT_BIN)
	./mkfs/mkfs fs.img $(USER_INIT_BIN)

```

- 这个修改主要是将fs.img作为新的指定磁盘镜像文件写入在mkfs.c中必要的新的文件系统镜像，提供真正可用的“文件系统”

```Makefile

run: kernel.bin
	qemu-system-riscv64 -machine virt -bios none -kernel kernel.bin -nographic \
	-drive file=fs.img,if=none,format=raw,id=x0 \
	-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
	-global virtio-mmio.force-legacy=false

```

- 这个修改主要是模拟 RISC-V 虚拟机并加载文件系统镜像。
- -drive行指定一个磁盘镜像文件 fs.img，格式为原始（raw），并分配一个 ID 为 x0，不自动挂载到某个设备。
- -device行将上面定义的磁盘镜像 x0 作为一个 virtio 块设备挂载到 RISC-V 虚拟机的 virtio-mmio 总线上。这样内核可以通过 virtio 驱动访问磁盘。
- -global行禁用 virtio 的 legacy（旧版）模式，强制使用现代 virtio 设备规范，提高兼容性和性能，因为在磁盘的初始化代码中有对于版本的检验代码：

```c

if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }

```

- 如果不强行禁用virtio的legacy，就会自动加载1.0版本，导致初始化失败，只有加上了该限制才能强制启用2.0版本的

### 源码理解总结

- 这个实验的代码量又刷新了一个上限……虽然难度跟5和6这两个出错了都没提示就纯崩溃不动的实验相比有所回落，但是做起来仍然极其的花费时间，特别是在中断分发处理和sleeplock的处理中经常会出现因为锁未在正确时机打开而导致对返回的proc号为0而产生空指针解引用缺页错误
- 通过这个实验我对文件系统有了更深刻的认识，也看明白了一些上个学期操作系统上课中没有弄懂的部分


## 测试验证部分

- 编写fs_test.c进行测试

（运行结果附在实验文件中）