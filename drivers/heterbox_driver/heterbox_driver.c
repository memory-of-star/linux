#include "heterbox_driver.h"

#define HETERBOX_MMIO_BASE 0x20beffa00000 // TODO: Avoid hardcoding
#define HETERBOX_REGION_SIZE 0x10000
#define DDR_OFFSET 0x80000 // page addr offset defined by CXL IP
#define DEVICE_MEM_SIZE 0x400000
#define CXL_MEM_BASE 0x2080000000 // CXL memory base addr

#define TEST_REG 0x100
#define FAST_MEMORY_BOUNDARY_REG 0x200
#define SLOW_MEMORY_ADD_CYCLE_REG 0x300
#define INIT_REMAP_TABLE_REG 0x400
#define RESET_DEBUG_INFO_REG 0x500
#define CACHE_HIT_NUM_LOW_CHANNEL_0_REG 0x600
#define CACHE_HIT_NUM_HIGH_CHANNEL_0_REG 0x700
#define CACHE_MISS_NUM_LOW_CHANNEL_0_REG 0x800
#define CACHE_MISS_NUM_HIGH_CHANNEL_0_REG 0x900
#define CACHE_HIT_NUM_LOW_CHANNEL_1_REG 0xa00
#define CACHE_HIT_NUM_HIGH_CHANNEL_1_REG 0xb00
#define CACHE_MISS_NUM_LOW_CHANNEL_1_REG 0xc00
#define CACHE_MISS_NUM_HIGH_CHANNEL_1_REG 0xd00

#define MIGRATE_ADDRESS_FAST_REG 0xe00
#define MIGRATE_ADDRESS_SLOW_REG 0xf00
#define MIGRATE_REQ_PUSH 0x1000

#define SKETCH_THRESHOLD_REG 0x1100
#define SKETCH_RESET_PERIOD_REG 0x1200

#define MIGRATE_PAGE_CNT_REG 0x1300
#define MIGRATE_PAGE_LIMIT_REG 0x1400
#define MIGRATE_PAGE_LIMIT_PERIOD_REG 0x1500
#define MIGRATE_PAGE_CNT_EXECUTED_REG 0x1600

#define COLD_SCAN_PERIOD_REG 0x1d00

#define READ_FASTMEM_CNT_LOW_REG 0x2200
#define READ_FASTMEM_CNT_HIGH_REG 0x2300
#define READ_SLOWMEM_CNT_LOW_REG 0x2400
#define READ_SLOWMEM_CNT_HIGH_REG 0x2500


static void __iomem *mmio_base;
static unsigned long long num_error_cxl_pages = 0;

extern bool neomem_debug_enabled;

// Reg read
static inline u32 heterbox_read(u32 reg_offset)
{
    return readl(mmio_base + reg_offset);
}

// Reg write
static inline void heterbox_write(u32 reg_offset, u32 value)
{
    writel(value, mmio_base + reg_offset);
}

// Test NeoProf RW by writing 0xabcd to WR_TEST_REG and reading it back
static void test_rw(void){
    heterbox_write(TEST_REG, 0x246);
    u32 data;
    data = heterbox_read(TEST_REG);
    if (data == 0x369){
        pr_info("Test HeterBox AVMM Interface: success\n");
    } else {
        pr_info("Test HeterBox AVMM Interface: failed\n");
    }
}

static int __init heterbox_init(void)
{
    // Mapping MMIO regs
    mmio_base = ioremap(HETERBOX_MMIO_BASE, HETERBOX_REGION_SIZE);
    if (!mmio_base) {
        printk("Failed to map HeterBox MMIO registers\n");
        return -ENOMEM;
    }
    test_rw();
    return 0;
}

static void __exit heterbox_exit(void)
{
    if (mmio_base)
        iounmap(mmio_base);
}

// slow memory add cycle
u32 get_fast_memory_bound(void)
{
    return heterbox_read(FAST_MEMORY_BOUNDARY_REG);
}

void set_fast_memory_bound(u32 fast_memory_bound)
{
    heterbox_write(FAST_MEMORY_BOUNDARY_REG, fast_memory_bound);
}

// slow memory add cycle
u32 get_slow_memory_add_cycle(void)
{
    return heterbox_read(SLOW_MEMORY_ADD_CYCLE_REG);
}

void set_slow_memory_add_cycle(u32 slow_memory_add_cycle)
{
    heterbox_write(SLOW_MEMORY_ADD_CYCLE_REG, slow_memory_add_cycle);
}


// remap mode
u32 get_remap_mode(void)
{
    return heterbox_read(INIT_REMAP_TABLE_REG);
}

void enable_remap_mode(void)
{
    heterbox_write(INIT_REMAP_TABLE_REG, 0x1);
}

void disable_remap_mode(void)
{
    heterbox_write(INIT_REMAP_TABLE_REG, 0x0);
}


// cache debug info
u64 get_cache_hit_num(int channel){
    u64 ret = -1;
    u32 low;
    u32 high;

    if (channel == 0){
        low = heterbox_read(CACHE_HIT_NUM_LOW_CHANNEL_0_REG);
        high = heterbox_read(CACHE_HIT_NUM_HIGH_CHANNEL_0_REG);
        ret = (((u64)high) << 32) | ((u64)low);
    }
    else if (channel == 1){
        low = heterbox_read(CACHE_HIT_NUM_LOW_CHANNEL_1_REG);
        high = heterbox_read(CACHE_HIT_NUM_HIGH_CHANNEL_1_REG);
        ret = (((u64)high) << 32) | ((u64)low);
    }
    return ret;
}

u64 get_cache_miss_num(int channel){
    u64 ret = -1;
    u32 low;
    u32 high;

    if (channel == 0){
        low = heterbox_read(CACHE_MISS_NUM_LOW_CHANNEL_0_REG);
        high = heterbox_read(CACHE_MISS_NUM_HIGH_CHANNEL_0_REG);
        ret = (((u64)high) << 32) | ((u64)low);
    }
    else if (channel == 1){
        low = heterbox_read(CACHE_MISS_NUM_LOW_CHANNEL_1_REG);
        high = heterbox_read(CACHE_MISS_NUM_HIGH_CHANNEL_1_REG);
        ret = (((u64)high) << 32) | ((u64)low);
    }
    return ret;
}

u32 get_migrate_address_fast(){
    u32 ret;
    ret = heterbox_read(MIGRATE_ADDRESS_FAST_REG);
    return ret;
}

void set_migrate_address_fast(u32 migrate_address_fast)
{
    heterbox_write(MIGRATE_ADDRESS_FAST_REG, migrate_address_fast);
}

u32 get_migrate_address_slow(){
    u32 ret;
    ret = heterbox_read(MIGRATE_ADDRESS_SLOW_REG);
    return ret;
}

void set_migrate_address_slow(u32 migrate_address_slow)
{
    heterbox_write(MIGRATE_ADDRESS_SLOW_REG, migrate_address_slow);
}

void start_migrate(){
    heterbox_write(MIGRATE_REQ_PUSH, 0x1);
}


u32 get_sketch_threshold(){
    u32 ret;
    ret = heterbox_read(SKETCH_THRESHOLD_REG);
    return ret;
}


void set_sketch_threshold(u32 threshold)
{
    heterbox_write(SKETCH_THRESHOLD_REG, threshold);
}


u32 get_sketch_reset_period(){
    u32 ret;
    ret = heterbox_read(SKETCH_RESET_PERIOD_REG);
    return ret;
}


void set_sketch_reset_period(u32 period)
{
    heterbox_write(SKETCH_RESET_PERIOD_REG, period);
}


u32 get_migrate_page_cnt(){
    u32 ret;
    ret = heterbox_read(MIGRATE_PAGE_CNT_REG);
    return ret;
}


u32 get_migrate_page_limit(){
    u32 ret;
    ret = heterbox_read(MIGRATE_PAGE_LIMIT_REG);
    return ret;
}


void set_migrate_page_limit(u32 limit)
{
    heterbox_write(MIGRATE_PAGE_LIMIT_REG, limit);
}


u32 get_migrate_page_limit_period(){
    u32 ret;
    ret = heterbox_read(MIGRATE_PAGE_LIMIT_PERIOD_REG);
    return ret;
}


void set_migrate_page_limit_period(u32 limit_period)
{
    heterbox_write(MIGRATE_PAGE_LIMIT_PERIOD_REG, limit_period);
}


u32 get_migrate_page_cnt_executed(){
    u32 ret;
    ret = heterbox_read(MIGRATE_PAGE_CNT_EXECUTED_REG);
    return ret;
}

u32 get_cold_scan_period(){
    u32 ret;
    ret = heterbox_read(COLD_SCAN_PERIOD_REG);
    return ret;
}

u64 get_fastmem_access_cnt(){
    u64 ret;
    u32 ret_low, ret_high;

    ret_low = heterbox_read(READ_FASTMEM_CNT_LOW_REG);
    ret_high = heterbox_read(READ_FASTMEM_CNT_HIGH_REG);

    ret = (((u64)ret_high) << 32) | ((u64)ret_low);

    return ret;
}

u64 get_slowmem_access_cnt(){
    u64 ret;
    u32 ret_low, ret_high;

    ret_low = heterbox_read(READ_SLOWMEM_CNT_LOW_REG);
    ret_high = heterbox_read(READ_SLOWMEM_CNT_HIGH_REG);

    ret = (((u64)ret_high) << 32) | ((u64)ret_low);

    return ret;
}

void set_cold_scan_period(u32 cold_scan_period){
    heterbox_write(COLD_SCAN_PERIOD_REG, cold_scan_period);
}


void set_migrate_page_cnt_executed(u32 migrate_page_cnt_executed)
{
    heterbox_write(MIGRATE_PAGE_CNT_EXECUTED_REG, migrate_page_cnt_executed);
}


u32 pfn_to_device_addr(u32 pfn){
    u32 device_addr;

    if (pfn < DEVICE_MEM_SIZE - DDR_OFFSET){
        device_addr = ((pfn + DDR_OFFSET) << 5);
    }
    else {
        device_addr = ((pfn + DDR_OFFSET - DEVICE_MEM_SIZE) << 5);
    }

    return device_addr;
}


module_init(heterbox_init);
module_exit(heterbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yiqi Chen");
MODULE_DESCRIPTION("HeterBox Linux driver");