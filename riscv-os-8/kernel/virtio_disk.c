#include "type.h"
#include "riscv.h"
#include "def.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

#define R(r) ((volatile uint32*)(VIRTIO0 + r))

static struct disk{
    // virtio 描述符、可用环和已用环
    struct virtq_desc* desc;
    // 可用环和已用环指针
    struct virtq_avail* avail;
    // 已用环指针
    struct virtq_used* used;
    char free[NUM];// 描述符是否空闲的标志数组
    uint16 used_idx;// 已用环的索引
    struct {
        struct buf *b;
        char status;
    } info[NUM];// 每个描述符对应的缓冲区和状态

    struct virtio_blk_req ops[NUM];// 当前请求结构体
    struct spinlock vdisk_lock;// 保护磁盘结构体的自旋锁
} disk;

// 初始化 virtio 磁盘设备
void virtio_disk_init(void)
{
    uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  printf("Magic value read: 0x%x\n", *R(VIRTIO_MMIO_MAGIC_VALUE));
  printf("Version read: 0x%x\n", *R(VIRTIO_MMIO_VERSION));
  printf("Device ID read: 0x%x\n", *R(VIRTIO_MMIO_DEVICE_ID));
  printf("Vendor ID read: 0x%x\n", *R(VIRTIO_MMIO_VENDOR_ID));

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
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
}

// 分配一个空闲的描述符，返回其索引，失败返回 -1
static int alloc_desc(void)
{
    for(int i = 0; i < NUM; i++){
        if(disk.free[i]){
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

// 释放描述符
static void free_desc(int i)
{
    if(i >= NUM) panic("free_desc index out of range");
    if(disk.free[i]) panic("free_desc freeing free descriptor");
    disk.desc[i].addr = 0;
    disk.desc[i].len = 0;
    disk.desc[i].flags = 0;
    disk.desc[i].next = 0;
    disk.free[i] = 1;
    wakeup(&disk.free[0]);
}

// 释放描述符链
static void free_chain(int i)
{
    while(1){
        if(disk.desc[i].flags & VRING_DESC_F_NEXT){
            int next = disk.desc[i].next;
            free_desc(i);
            i = next;
        } else {
            free_desc(i);
            break;
        }
    }
}

// 分配三个描述符，返回0表示成功，-1表示失败
static int alloc3_desc(int *idx)
{
    // printf("alloc3_desc: trying to allocate 3 descriptors\n");
    for(int i = 0; i < 3; i++){
        int d = alloc_desc();
        if(d < 0){
            for(int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
        idx[i] = d;
    }
    return 0;
}

// 读写磁盘块
void virtio_disk_rw(struct buf *b, int write)
{ 
  // printf("virtio_disk_rw: b=%x write=%d\n", b, write);
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      // printf("Allocated descriptors %d, %d, %d\n", idx[0], idx[1], idx[2]);
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  // printf("virtio_disk_rw: sector=%d write=%d\n", sector, write);

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0; // device reads b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; // device writes 0 on success
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  // printf("virtio_disk_rw: avail->idx=%d\n", disk.avail->idx);

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  disk.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
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

  // printf("virtio_disk_rw: woke up b=%x\n", b);
  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  // printf("virtio_disk_rw: completed\n");

  release(&disk.vdisk_lock);
}

// virtio 磁盘中断处理程序
void virtio_disk_intr(void)
{
  acquire(&disk.vdisk_lock);

  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;
    if(disk.info[id].status != 0){
      panic("virtio disk intr status");
    }
    struct buf *b = disk.info[id].b;
    b->disk = 0;
    wakeup(b);
    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}
