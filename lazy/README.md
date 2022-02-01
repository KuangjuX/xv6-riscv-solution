# Lazy Page Allocation

在本次实验中我们需要实现内存页懒加载机制，也就是当用户向内核申请内存的时候，内核不需要实际分配内存，而只是增长当前进程的 `size`，也就是说我们需要修改 `sys_sbrk()` 系统调用：
```c
// 懒加载，在这里仅仅改变进程记录的内存大小的值
// 而并不实际分配
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (n < 0) {
    if(growproc(n) < 0)
      return -1;
  }else{
    if(addr + n >= MAXVA){
      return -1;
    }
    myproc()->sz = addr + n;
  }
  return addr;
}
```  
  
由于我们并没有实际向进程分配内存，因此当进程访问未分配的内存时会发生也错误，我们需要在 `usertrap` 中拿到页错误的地址并分配内存地址并进行映射:
```c
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->tf->epc = r_sepc();
  
  uint64 scause = r_scause();
  if(scause == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->tf->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  }else if((which_dev = devintr()) != 0){
    // ok
  } else {
    if(scause == 0xf || scause == 0xd) {
      // 如果读取的是用户栈下的保护页则不需要进行分配
      uint64 err_addr = PGROUNDDOWN(r_stval());
      uint64 new_addr = err_addr + PGSIZE;
      
      if(err_addr > p->sz || new_addr >= MAXVA || err_addr <= p->tf->sp) {
        p->killed = 1;
      }else{
        pagetable_t pgt = p->pagetable;
        // 页错误，懒加载时当需要执行代码的时候需要触发此中断
        // 获取到发生地址错误的虚拟地址
        // 每次只分配一页
        char* page = kalloc();
        if(page == 0){
          uvmdealloc(pgt, err_addr, new_addr);
          p->killed = 1;
        }else {
          if(mappages(
            pgt,
            err_addr,
            PGSIZE,
            (uint64)page,
            PTE_W|PTE_X|PTE_R|PTE_U
          ) != 0){
            kfree(page);
            uvmdealloc(pgt, err_addr, new_addr);
          }
        }
      }
    }else {
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      p->killed = 1;
    }
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
```  
  
这里注意，我们对于发生错误的地址有一定的限制要求，首先发生错误的地址不能超过使用 `proc->size`，否则就是访问了不允许的地址，其次错误地址不可以超过系统最大虚拟地址 `MAXVA`，最后不允许访问用户栈下面的 `guard page`，即不能为其分配地址，而是要按正常的异常的处理走，否则会触发难以想象的后果。
  
除此之外，当我们调用 `fork` 系统调用的时候，系统会遍历该进程所有的地址并进行分配映射，然后有些地址实际上是并没有被分配的，对于这部分地址我们选择忽略而并非 `panic`，我们修改 `uvmcopy` 如下：
```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0){
      continue;
    }
    if((*pte & PTE_V) == 0){
      continue;
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i, 1);
  return -1;
}
```  
  
当系统调用 `sys_read` 或者 `sys_write` 来进行读写文件时涉及到内存的拷贝，因此当我们处理到文件内存拷贝时遇到为被分配的页时需要重新分配映射后再进行拷贝，例如我们修改 `writei` 如下所示：
```c
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      // brelse(bp);
      // break;
      uint64 addr = PGROUNDDOWN(src);
      void* page = kalloc();
      if(mappages(
        myproc()->pagetable,
        addr,
        PGSIZE,
        (uint64)page,
        PTE_W|PTE_X|PTE_R|PTE_U
      ) != 0){
        kfree(page);
        uvmdealloc(myproc()->pagetable, addr, addr + PGSIZE);
      }
      either_copyin(bp->data + (off % BSIZE), user_src, src, m);
    }
    log_write(bp);
    brelse(bp);
  }

  if(n > 0){
    if(off > ip->size)
      ip->size = off;
    // write the i-node back to disk even if the size didn't change
    // because the loop above might have called bmap() and added a new
    // block to ip->addrs[].
    iupdate(ip);
  }

  return n;
}
```