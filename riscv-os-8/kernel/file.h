
// 文件结构体
struct file{
    enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
    int ref; // 引用计数
    char readable; // 可读标志
    char writable; // 可写标志
    // struct pipe *pipe; // 如果是管道，则指向管道结构体
    struct inode *ip; // 如果是文件，则指向 i节点结构体
    uint off; // 文件偏移量
    short major; // 设备号（仅用于设备文件）
};

#define major(dev) ((dev) >> 16 && 0xFFFF)// 获取主设备号
#define minor(dev) ((dev) & 0xFFFF)// 获取次设备号
#define makedev(mn, mx) (((mn) << 16) | (mx))// 创建设备号

struct inode{
    uint dev;           // i节点所在设备号
    uint inum;         // i节点号
    int ref;           // 引用计数
    struct sleeplock lock; // i节点锁
    int valid;         // i节点是否有效

    short type;        // 文件类型
    short major;       // 主设备号（仅用于设备文件）
    short minor;       // 次设备号（仅用于设备文件）
    short nlink;       // 硬链接数量
    uint size;         // 文件大小（以字节为单位）
    uint addrs[NDIRECT+1]; // 数据块地址
};

struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1