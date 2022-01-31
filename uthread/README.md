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