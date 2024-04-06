#ifndef __NEOPROF_H__
#define __NEOPROF_H__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/*
    * The following functions are used to access the neoprof device
*/
u32 get_fast_memory_bound(void);
void set_fast_memory_bound(u32 fast_memory_bound);

u32 get_slow_memory_add_cycle(void);
void set_slow_memory_add_cycle(u32 slow_memory_add_cycle);

u32 get_remap_mode(void);
void enable_remap_mode(void);
void disable_remap_mode(void);

#endif 