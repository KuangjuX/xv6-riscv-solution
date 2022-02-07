# xv6-riscv-solution
这个仓库记录了我的 `MIT 6.S081` 的实现，由于之前使用 `Rust` 语言重新实现了 `xv6-riscv` 操作系统，但是还没有做过 `MIT 6.S081` 的实验，因此在这个仓库了我存储了我感兴趣的实验是实现部分，一些比较简单或者我没有兴趣的则没有做。    
  
另外，`MIT 6.S081` 的挑战题目其实也是很有意思的，涉及到比较现代化的操作系统的知识，例如在 `uthread` 的可选挑战中指出，在目前的用户态线程，一旦当前进程的某个线程阻塞了，则该进程的所有其他用户态线程都会被阻塞。实验要求我们实现可以在多核之间调度的用户态线程，也就是任务窃取策略。实验给了我们一些提示例如 `scheduler activations` 或者一个用户线程占用一个内核线程，其中涉及到 TLB 击落等知识。这些都是很好的探索方向，或许等之后有时间了可以做一下。

## Solutions
- [x] Lazy Page Allocation
- [x] Copy-on-Write Fork 
- [x] User Level Threads
- [x] Networking
- [x] mmap