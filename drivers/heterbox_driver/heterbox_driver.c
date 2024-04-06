#include "heterbox_driver.h"

#define HETERBOX_MMIO_BASE 0x20beffa00000 // TODO: Avoid hardcoding
#define HETERBOX_REGION_SIZE 0x10000
#define DDR_OFFSET 0x80000 // page addr offset defined by CXL IP
#define CXL_MEM_BASE 0x2080000000 // CXL memory base addr

#define TEST_REG 0x100
#define FAST_MEMORY_BOUNDARY_REG 0x200
#define SLOW_MEMORY_ADD_CYCLE_REG 0x300
#define INIT_REMAP_TABLE_REG 0x400


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


module_init(heterbox_init);
module_exit(heterbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yiqi Chen");
MODULE_DESCRIPTION("HeterBox Linux driver");