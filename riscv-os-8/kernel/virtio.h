

//Virtio device definitions
#define VIRTIO_MMIO_MAGIC_VALUE 0x000 // should be "virt" == 0x74726976
#define VIRTIO_MMIO_VERSION		0x004 // version; should be 2
#define VIRTIO_MMIO_DEVICE_ID		0x008 // device type; 1 is net, 2 is disk
#define VIRTIO_MMIO_VENDOR_ID		0x00c // 0x554d4551
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020
#define VIRTIO_MMIO_QUEUE_SEL		0x030 // select queue, write-only
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034 // max size of current queue, read-only
#define VIRTIO_MMIO_QUEUE_NUM		0x038 // size of current queue, write-only
#define VIRTIO_MMIO_QUEUE_READY		0x044 // ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050 // write-only
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060 // read-only
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064 // write-only
#define VIRTIO_MMIO_STATUS		0x070 // read/write
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080 // physical address for descriptor table, write-only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW	0x090 // physical address for available ring, write-only
#define VIRTIO_MMIO_DRIVER_DESC_HIGH	0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW	0x0a0 // physical address for used ring, write-only
#define VIRTIO_MMIO_DEVICE_DESC_HIGH	0x0a4

// Virtio status flags
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1
#define VIRTIO_CONFIG_S_DRIVER		2
#define VIRTIO_CONFIG_S_DRIVER_OK	4
#define VIRTIO_CONFIG_S_FEATURES_OK	8

// Virtio block device feature flags
#define VIRTIO_BLK_F_RO              5	//Disk is read-only
#define VIRTIO_BLK_F_SCSI            7	//Supports scsi command passthru
#define VIRTIO_BLK_F_CONFIG_WCE     11	//Writeback mode available in config
#define VIRTIO_BLK_F_MQ             12	//support more than one vq
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX     29

#define NUM 8

struct virtq_desc {
    uint64 addr;   // Address (guest-physical)
    uint32 len;    // Length
    uint16 flags;  // The flags as indicated above
    uint16 next;   // We chain unused descriptors via this
};

#define VRING_DESC_F_NEXT  1 // chained with another descriptor
#define VRING_DESC_F_WRITE 2 // device writes (vs read)

// (otherwise read-only).
struct virtq_avail{
    uint16 flags;//标志位，未使用，置0
    uint16 idx;//驱动程序放入可用描述符的索引
    uint16 ring[NUM];//实际大小为队列大小
    uint16 unused;//未使用，置0
};

struct virtq_used_elem {
    uint32 id;   // Index of start of used descriptor chain.
    uint32 len;  // Total length of the descriptor chain which was written to.
};

struct virtq_used {
    uint16 flags;//标志位，未使用，置0
    uint16 idx;//设备放入已用描述符的索引
    struct virtq_used_elem ring[NUM];//实际大小为队列大小
};

#define VIRTIO_BLK_T_IN		0	//读操作
#define VIRTIO_BLK_T_OUT	1	//写操作

struct virtio_blk_req {
    uint32 type;      // VIRTIO_BLK_T_IN or VIRTIO_BLK_T_OUT
    uint32 reserved;  // 保留字段，置0
    uint64 sector;    // 扇区号
};