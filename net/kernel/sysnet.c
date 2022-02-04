//
// network system calls.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

struct sock {
  struct sock *next; // the next socket in the list
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};

static struct spinlock lock;
static struct sock *sockets;

void sock_dump(){
  printf("-------------------------\n");
  struct sock* sock;
  // if(!holding(&lock)){
  //   acquire(&lock);
  // }
  sock = sockets;
  while(sock != 0){
    printf("raddr: %d, lport: %d, rport: %d\n", sock->raddr, sock->lport, sock->rport);
    sock = sock->next;
  }
  // if(holding(&lock)){
  //   release(&lock);
  // }
  printf("-------------------------\n");
}

void
sockinit(void)
{
  initlock(&lock, "socktbl");
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
	pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

//
// Your code here.
//
// Add and wire in methods to handle closing, reading,
// and writing for network sockets.
//

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  //
  // Your code here.
  //
  // Find the socket that handles this mbuf and deliver it, waking
  // any sleeping reader. Free the mbuf if there are no sockets
  // registered to handle it.
  //
  acquire(&lock);
  struct sock* sock = sockets;
  // 首先找到对应的 socket
  while(sock != 0){
    if(sock->lport == lport && sock->raddr == raddr && sock->rport == rport){
      break;
    }
    sock = sock->next;
    if(sock == 0){
      printf("[Kernel] sockrecvudp: can't find socket.\n");
      return;
    }
  }
  release(&lock);
  acquire(&sock->lock);
  // 将 mbuf 分发到 socket 中
  mbufq_pushtail(&sock->rxq, m);
  // 唤醒可能休眠的 socket
  release(&sock->lock);
  wakeup((void*)sock);
}

int sock_read(struct sock* sock, uint64 addr, int n){
  acquire(&sock->lock);
  while(mbufq_empty(&sock->rxq)) {
    // 当队列为空的时候，进入 sleep, 将 CPU
    // 交给调度器
    if(myproc()->killed) {
      release(&sock->lock);
      return -1;
    }
    sleep((void*)sock, &sock->lock);
  }
  int size = 0;
  if(!mbufq_empty(&sock->rxq)){
    struct mbuf* recv_buf = mbufq_pophead(&sock->rxq);
    if(recv_buf->len < n){
      size = recv_buf->len;
    }else{
      size = n;
    }
    if(copyout(myproc()->pagetable, addr, recv_buf->head, size) != 0){
      release(&sock->lock);
      return -1;
    }
    // 或许要考虑一下读取的大小再考虑是否释放，因为有可能
    // 读取的字节数要比 buf 中的字节数少
    mbuffree(recv_buf);
  }
  release(&sock->lock);
  return size;
}

int sock_write(struct sock* sock, uint64 addr, int n){
  acquire(&sock->lock);
  struct mbuf* send_buf = mbufalloc(sizeof(struct udp) + sizeof(struct ip) + sizeof(struct eth));
  if (copyin(myproc()->pagetable, (char*)send_buf->head, addr, n) != 0){
    release(&sock->lock);
    return -1;
  }
  mbufput(send_buf, n);
  net_tx_udp(send_buf, sock->raddr, sock->lport, sock->rport);
  release(&sock->lock);
  return n;
}

void sock_close(struct sock* sock){
  struct sock* prev = 0;
  struct sock* cur = 0;
  acquire(&lock);
  // 遍历 sockets 链表找到对应的 socket 并将其
  // 从链表中移除
  cur = sockets;
  while(cur != 0){
    if(cur->lport == sock->lport && cur->raddr == sock->raddr && cur->rport == sock->rport){
      if(cur == sockets){
        sockets = sockets->next;
        break;
      }else{
        sock = cur;
        prev->next = cur->next;
        break;
      }
    }
    prev = cur;
    cur = cur->next;
  }
  // 释放 sock 所有的 mbuf
  acquire(&sock->lock);
  while(!mbufq_empty(&sock->rxq)){
    struct mbuf* free_mbuf = mbufq_pophead(&sock->rxq);
    mbuffree(free_mbuf);
  }
  // 释放 socket
  release(&sock->lock);
  release(&lock);
  kfree((void*)sock);
}
