# Memory Allocator

Dynamic memory allocator implemented in **C**, reproducing the core functionality of `malloc()`, `calloc()`, `realloc()`, and `free()` using low-level system calls.  
The allocator efficiently manages both heap and mapped memory through block splitting, coalescing, and best-fit reuse.

---

## Overview
This project implements a custom allocator that manages virtual memory manually through `sbrk()` and `mmap()`.  
It handles block metadata, alignment, and fragmentation to achieve efficient allocation and reallocation.

**Language:** C  
**Focus:** Memory management, system calls, low-level programming, performance optimization  

---

## Features

### Memory Allocation
- **`os_malloc(size)`** — allocates a block of memory;  
  uses `sbrk()` for small allocations and `mmap()` for large ones.  
- **`os_calloc(nmemb, size)`** — allocates and zero-initializes a block for arrays.  
- **`os_realloc(ptr, size)`** — resizes an existing block, expanding or moving it as needed.  
- **`os_free(ptr)`** — frees a previously allocated block, reusing or unmapping memory.

### Implementation Details
- **8-byte alignment** for all allocations.  
- **Block metadata structure** (`struct block_meta`) tracks size, status, and adjacency.  
- **Block splitting** to minimize internal fragmentation.  
- **Block coalescing** to reduce external fragmentation.  
- **Heap preallocation** — allocates 128 KB on first use to reduce system calls.  
- **Best-fit search** to find the most suitable free block for reuse.  

---

## Example Usage
```
void *p = os_malloc(100);
p = os_realloc(p, 200);
os_free(p);

void *q = os_calloc(10, sizeof(int));
os_free(q);
```
---
## Learning Outcomes
- Implemented a custom allocator with manual heap and memory mapping.  
- Practiced managing alignment, fragmentation, and block reuse.  
- Deepened understanding of virtual memory and system-level allocation.  
- Applied efficient low-level memory management techniques in C.  
