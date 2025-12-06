#include "type.h"

#define ROOTINO 1  // root i-number
#define BSIZE 1024  // block size

// on-disk superblock
struct superblock {
    uint magic;        // 文件系统魔数
    uint size;         // 文件系统大小（以块为单位）
    uint nblocks;      // 数据块数量
    uint ninodes;      // i节点数量
    uint nlog;         // 日志块数量
    uint logstart;     // 日志起始块号
    uint inodestart;   // i节点起始块号
    uint bmapstart;    // 位图起始块号
};

#define FSMAGIC 0x10203040  // 文件系统魔数
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// on-disk inode structure
struct dinode {
    short type;           // 文件类型
    short major;          // 主设备号（仅用于设备文件）
    short minor;          // 次设备号（仅用于设备文件）
    short nlink;          // 硬链接数量
    uint size;            // 文件大小（以字节为单位）
    uint addrs[NDIRECT+1]; // 数据块地址
};

#define IPB (BSIZE / sizeof(struct dinode)) // 每块包含的 i节点数量
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart) // 计算 i节点 i 所在的块号
#define BPB (BSIZE*8) // 每块包含的位图位数
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart) // 计算数据块 b 所在的位图块号

#define DIRSIZ 14

// 目录项
struct dirent {
    ushort inum;             // i节点号
    char name[DIRSIZ];       // 文件名
};