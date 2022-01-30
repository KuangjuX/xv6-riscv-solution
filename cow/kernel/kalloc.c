// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// 用来记录页引用

uint page_refs[PHYSTOP >> 12];
struct spinlock refs_lock;

void pin_page(uint32 index){
  acquire(&refs_lock);
  page_refs[index]++;
  release(&refs_lock);
}

void unpin_page(uint32 index){
  acquire(&refs_lock);
  page_refs[index]--;
  release(&refs_lock);
}

int get_page_ref(uint index) {
  return page_refs[index];
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // 初始化引用计数
    uint index = ((uint64)p - (uint64)pa_start) / PGSIZE;
    acquire(&refs_lock);
    page_refs[index] = 1;
    release(&refs_lock);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  uint32 index = ((uint64)pa - PGROUNDUP((uint64)end)) / PGSIZE;
  if(get_page_ref(index) < 1){
    printf("[Kernel] kfree: refs: %d\n", get_page_ref(index));
    panic("[Kernel] kfree: refs < 1.\n");
  }
  unpin_page(index);
  int refs = get_page_ref(index);
  if(refs > 0)return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    // 将引用计数加一
    uint32 index = ((uint64)r - PGROUNDUP((uint64)end)) / PGSIZE;
    // pin_page(index);
    acquire(&refs_lock);
    page_refs[index] = 1;
    release(&refs_lock);
  }
  return (void*)r;
}
