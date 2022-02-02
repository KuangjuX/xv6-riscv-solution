#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
// 发送队列
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
// 接受队列
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    // 设置 tx_ring 每个 mbuf 的状态
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  // 设置 tx_ring 的基地址
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  // 设置 tx_ring 的长度
  regs[E1000_TDLEN] = sizeof(tx_ring);
  // 将头部和尾部的 offset 设置为 0，表示 tx_ring 为空
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    // 为 rx_ring 分配内存，用来接收 packets
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    // 用来设置描述符的地址
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  // 设置 rx_ring 的基地址
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  // 通知以太网控制器 rx_ring 的头部和尾部
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  // 设置网卡接收的 MAC 地址
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  // return -1;
  
  // 获取 ring position
  uint64 tdt = regs[E1000_TDT];
  uint64 index = tdt % TX_RING_SIZE;
  struct tx_desc send_desc = tx_ring[index];
  if(!(send_desc.status & E1000_TXD_STAT_DD)) {
    return -1;
  }
  if(tx_mbufs[index]->head != 0){
    // 如果该位置的缓冲区不为空则释放
    mbuffree(tx_mbufs[index]);
  }
  tx_mbufs[index] = m;
  tx_ring[index].addr = (uint64)tx_mbufs[index]->head;
  tx_ring[index].length = (uint16)tx_mbufs[index]->len;
  tx_ring[index].cmd = (1 << 3) | (1 << 1) | (1 << 0);
  tx_ring[index].status = 0;

  tdt = (tdt + 1) % TX_RING_SIZE;
  regs[E1000_TDT] = tdt;
  __sync_synchronize();
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  // 扫描 RX queue 处理收到每个收到的 pakcet
  // 对于每个 pakcet 都创建和分发 mbuf，调用 net_rx()
  // 将其传输到协议层
  
  // 获取接收 packet 的位置
  uint64 rdt = regs[E1000_RDT];
  uint64 index = (rdt + 1) % RX_RING_SIZE; 
  struct rx_desc recv_desc = rx_ring[index];
  if(!(recv_desc.status & E1000_RXD_STAT_DD)){
    // 查看新的 packet 是否有 E1000_RXD_STAT_DD 标志，如果
    // 没有，则直接返回
    return;
  }
  // 使用 mbufput 更新长度并将其交给 net_rx() 处理
  mbufput(rx_mbufs[index], recv_desc.length);
  net_rx(rx_mbufs[index]);
  // 分配新的 mbuf 并将其写入到描述符中并将状态吗设置成 0
  rx_mbufs[index] = mbufalloc(0);
  rx_ring[index].addr = (uint64)rx_mbufs[index]->head;
  rx_ring[index].status = 0;

  rdt = index;
  regs[E1000_RDT] = rdt;
}

// 当 e1000 网卡收到数据时会发生中断
void
e1000_intr(void)
{
  e1000_recv();
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR];
}
