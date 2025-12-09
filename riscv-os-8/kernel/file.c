#include "type.h"
#include "riscv.h"
#include "def.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];

// 全局文件表
struct{  
    struct spinlock lock;
    struct file file[NFILE];
}file_table;

// 初始化文件表
void file_init(void){
    initlock(&file_table.lock, "file_table");
}

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

// 获取文件状态
int filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// 读取文件数据
int fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// 写入文件数据
int filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}
