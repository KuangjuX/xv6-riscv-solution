# Copy on Write Fork

## 思路
思路相对来说比较简单，`copy on write` 主要实现是效果是在 `fork` 的时候并不要为子进程额外分配物理地址，而是与父进程共享物理地址，并且将物理地址的页表项(即最后一级页表项)设为不可写，这样，如果父进程与子进程如果不被写的话就可以完全正常执行，当设计到写操作时，硬件的 MMU 将会检测到页表项不可写，此时会产生 `Page Fault` 异常，而操作系统在异常处理中需要重新分配私有的物理页，并将原有的物理页拷贝到私有物理页并进行重新映射，并将页表项重新设置为可写即可。而原有的物理页则可能被多个进程所引用，所以由引用计数来决定是否被释放，具体过程见实现部分。

## 实现
首先用户进程在执行 `fork` 的时候子进程不需要对父进程物理页面进行实际拷贝，实际修改是在 `uvmcopy` 这个函数里面:
```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  // 将父进程页表内容拷贝到子进程中
  // 将所有页表项设置为不可写,这里要模拟一遍页表翻译过程
  for(uint64 vaddr = 0; vaddr < sz; vaddr += PGSIZE) {
    // 我们只需要将最低级的页表加上 COW 标识符
    // 并擦去写标志位即可
    extern char end[];
    pte_t* pte = walk(old, vaddr, 0);
    uint64 pa = (uint64)PTE2PA((uint64)(*pte));
    uint32 index = (pa - PGROUNDUP((uint64)end)) / PGSIZE;
    pin_page(index);
    uint64 flags = (PTE_FLAGS((uint64)(*pte)) | PTE_COW) & ~PTE_W;
    if(mappages(new, vaddr, PGSIZE, pa, flags) != 0){
      panic("[Kernel] uvmcopy: fail to copy parent physical address.\n");
    }
    if(pte == 0){
      panic("[Kernel] uvmcopy: pte should exist.\n");
    }
    if(!(*pte & PTE_COW)) {
        // 将页表项加上 COW 标识符
        *pte |= PTE_COW;
        // 清除写标志位
        *pte &= ~(PTE_W);
      }
  }
  
  return 0;
}
```