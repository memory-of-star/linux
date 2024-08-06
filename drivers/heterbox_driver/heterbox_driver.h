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

u64 get_cache_hit_num(int);
u64 get_cache_miss_num(int);

u32 get_migrate_address_fast(void);
void set_migrate_address_fast(u32);

u32 get_migrate_address_slow(void);
void set_migrate_address_slow(u32);

void start_migrate(void);

u32 get_sketch_threshold(void);
void set_sketch_threshold(u32);
u32 get_sketch_reset_period(void);
void set_sketch_reset_period(u32);
u32 get_migrate_page_cnt(void);

u32 get_migrate_page_limit(void);
void set_migrate_page_limit(u32);
u32 get_migrate_page_limit_period(void);
void set_migrate_page_limit_period(u32);
u32 get_migrate_page_cnt_executed(void);
void set_migrate_page_cnt_executed(u32);

u32 get_cold_scan_period(void);
void set_cold_scan_period(u32);

u32 pfn_to_device_addr(u32);

u64 get_fastmem_access_cnt(void);
u64 get_slowmem_access_cnt(void);

#endif 