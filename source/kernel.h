#pragma once

#include <cstdint>

#define ADDRESS_main_entry_hook 0x0101C56C

#define KERN_SYSCALL_TBL_1      0xFFE84C70 // unknown
#define KERN_SYSCALL_TBL_2      0xFFE85070 // works with games
#define KERN_SYSCALL_TBL_3      0xFFE85470 // works with loader
#define KERN_SYSCALL_TBL_4      0xFFEAAA60 // works with home menu
#define KERN_SYSCALL_TBL_5      0xFFEAAE60 // works with browser (previously KERN_SYSCALL_TBL)


void RevertMainHook();

void KernelWriteU32(uint32_t addr, uint32_t value);

void __attribute__((noinline)) kern_write(void *addr, uint32_t value);
