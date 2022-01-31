# User Level Threads

## Warmup: RISC-V Assembly
1. Which registers contain arguments to functions? For example, which register holds 13 in main's call to printf?  
  
在 `main` 函数中 a0, a1, a2 寄存器保存着调用参数， 13 放在 a2 中。  
  
2. Where is the function call to f from main? Where is the call to g? (Hint: the compiler may inline functions.)  
  
在 `main` 函数中直接将调用 `f` 的结果计算了出来: 
```assembly
  24:	4635                	li	a2,13
  26:	45b1                	li	a1,12
```  
  
3. At what address is the function printf located?  
  
```assembly
  30:	00000097          	auipc	ra,0x0
  34:	620080e7          	jalr	1568(ra) # 650 <printf>
```
`printf` 函数的地址为: 48 + 1568 = 1616 = 650(16)  
  
4. What value is in the register ra just after the jalr to printf in main?  
  
返回地址是 `0x38` 

## 实现
用户态线程，又叫做协程，是相对内核态线程更加轻量化的线程。而用户态线程也分为有栈协程和无栈协程，在本实验中我们将去实现有栈协程。大部分代码已经给好了，我们需要做的就是去实现线程调度和上下文切换的过程。在实现之前，我们需要为 `thread` 结构体添加上下文切换所需的寄存器，在上下文切换中我们需要保存 `callee saved registers`，这是由于 `caller saved registers` 由调用者来保存，一般被保存在栈中，而 `callee saved registers` 由被调用者进行保存，因此在上下文切换我们需要去保存这些寄存器，除了 `callee saved registers`，我们还需要去保存 `sp` 和 `ra` 寄存器，这是由于在上下文切换之后需要根据 `ra` 寄存器的值进行保存，如果不进行切换，则无法进行函数的切换，而 `sp` 表示栈指针，也是在函数调用时必要的地址。  
  
我们线程上下文的结构如下所示:
```c
// 线程上下文，用于保存寄存器
struct thread_context{
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```  
  
线程的结构如下所示:
```c
struct thread {
  struct     thread_context context; // 线程上下文
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  char       name[10]; // For Debug
};
```  
  
而上下文切换的过程则只需要将当前的寄存器堆存入旧的上下文中，并将新的上下文中的寄存器读入寄存器堆即可:
```assembly
/* Switch from current_thread to next thread_thread, and make
 * next_thread the current_thread.  Use t0 as a temporary register,
 * which should be caller saved. */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
	// a0, a1分别为老进程与新进程
	sd ra, 0(a0)
	sd sp, 8(a0)
	sd s0, 16(a0)
	sd s1, 24(a0)
	sd s2, 32(a0)
	sd s3, 40(a0)
	sd s4, 48(a0)
	sd s5, 56(a0)
	sd s6, 64(a0)
	sd s7, 72(a0)
	sd s8, 80(a0)
	sd s9, 88(a0)
	sd s10, 96(a0)
	sd s11, 104(a0)

	ld ra, 0(a1)
	ld sp, 8(a1)
	ld s0, 16(a1)
	ld s1, 24(a1)
	ld s2, 32(a1)
	ld s3, 40(a1)
	ld s4, 48(a1)
	ld s5, 56(a1)
	ld s6, 64(a1)
	ld s7, 72(a1)
	ld s8, 80(a1)
	ld s9, 88(a1)
	ld s10, 96(a1)
	ld s11, 104(a1)

	ret    /* return to ra */

```  
  
在用户态线程初始化的过程中，我们只需要初始化线程的状态以及 `ra`、`sp` 寄存器的值即可:
```c
// 创建线程, 参数为函数指针
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->name[0] = name;
  name++;
  // 将线程状态设置为可运行
  t->state = RUNNABLE;
  // 将上下文的函数返回值设置为函数指针的地址
  t->context.ra = (uint64)func;
  // 设置上下文的栈地址
  t->context.sp = (uint64)&t->stack[STACK_SIZE];
}
```
这里要注意, 一定要将 `sp` 设置为栈的最后一个字节的地址，因为在编译调用函数的时候会将栈寄存器减去一部分，如果设置为第一个字节的地址则会发生栈溢出，这样就会破坏其他内存地址的值。  
  
最后进行线程调度时我们只需要顺序找到一个 `RUNNABLE` 的线程并进行上下文切换即可:
```c
// 线程调度
void 
thread_schedule(void)
{
  struct thread *t, *next_thread;

  // thread_dump();
  /* Find another runnable thread. */
  next_thread = 0;
  t = current_thread + 1;
  for(int i = 0; i < MAX_THREAD; i++){
    if(t >= all_thread + MAX_THREAD)
      t = all_thread;
    if(t->state == RUNNABLE) {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    // 找到下一个调度的线程并执行上下文切换
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    // 进行上下文切换
    thread_switch((uint64)&t->context, (uint64)&current_thread->context);
  } else{
    next_thread = 0;
  }
}
```