#include "type.h"
#include "riscv.h"
#include "def.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "fcntl.h" // 需要定义 O_CREATE, O_RDWR 等

// 简单的辅助函数：创建文件
static struct inode* create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

void fs_test() {
    printf("\n=== Starting File System Test ===\n");

    char *filename = "/test_file";
    char *data = "Hello, RISC-V OS File System!";
    int len = strlen(data);
    char buf[100];
    struct inode *ip;

    // 1. 创建文件
    printf("[TEST] Creating file: %s\n", filename);
    begin_op();
    ip = create(filename, T_FILE, 0, 0);
    if(ip == 0){
        printf("[FAIL] Create failed\n");
        end_op();
        return;
    }
    iunlock(ip); // create 返回锁定的 inode，先解锁以便后续操作
    end_op();
    printf("[PASS] File created. Inode number: %d\n", ip->inum);

    // 2. 写入数据
    printf("[TEST] Writing data: \"%s\"\n", data);
    begin_op();
    ilock(ip);
    // 注意：writei 的第二个参数是 user_src (0 表示内核地址)
    int n = writei(ip, 0, (uint64)data, 0, len);
    iupdate(ip);
    iunlock(ip);
    end_op();

    if(n != len) {
        printf("[FAIL] Write failed. Wrote %d bytes, expected %d\n", n, len);
        return;
    }
    printf("[PASS] Data written successfully.\n");

    // 3. 读取数据
    printf("[TEST] Reading data back...\n");
    memset(buf, 0, sizeof(buf));
    
    ilock(ip);
    // 注意：readi 的第二个参数是 user_dst (0 表示内核地址)
    n = readi(ip, 0, (uint64)buf, 0, sizeof(buf));
    iunlock(ip);

    if(n < 0) {
        printf("[FAIL] Read failed.\n");
        return;
    }
    printf("[INFO] Read %d bytes: \"%s\"\n", n, buf);

    // 4. 校验内容
    if(strncmp(data, buf, len) == 0) {
        printf("[PASS] Data verification successful!\n");
    } else {
        printf("[FAIL] Data verification failed!\n");
        printf("       Expected: %s\n", data);
        printf("       Got:      %s\n", buf);
    }

    // 5. 清理 (减少引用计数)
    // 在真实场景中，unlink 会移除目录项，这里我们只是释放内存中的 inode 引用
    iput(ip); 

    printf("=== File System Test Completed ===\n\n");
}