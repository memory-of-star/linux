#include "neoprof.h"

#define NEOPROFILER_MMIO_BASE 0x20beffa00000 // TODO: Avoid hardcoding
#define NEOPROFILER_REGION_SIZE 0x10000
#define DDR_OFFSET 0x80000 // page addr offset defined by CXL IP
#define CXL_MEM_BASE 0x2080000000 // CXL memory base addr

#define HOTPAGE_NUM_REG 0x200
#define HOTPAGE_REG 0x300
#define THRESHOLD_SET_REG 0x300
#define NR_HOTPAGE_REG 0x200
#define RD_TEST_REG 0x100
#define WR_TEST_REG 0x100
#define RESET_REG 0x200
#define NR_PAGE_IN 0x400

static void __iomem *mmio_base;

// Reg read
static inline u32 neoprof_read(u32 reg_offset)
{
    return readl(mmio_base + reg_offset);
}

// Reg write
static inline void neoprof_write(u32 reg_offset, u32 value)
{
    writel(value, mmio_base + reg_offset);
}

// Test NeoProf RW by writing 0xabcd to WR_TEST_REG and reading it back
static void test_rw(void){
    neoprof_write(WR_TEST_REG, 0xabcd);
    u32 data;
    data = neoprof_read(RD_TEST_REG);
    if (data == 0xabcd){
        pr_info("Test NeoProf RW: success\n");
    } else {
        pr_info("Test NeoProf RW: failed\n");
    }
}

static int __init neoprof_init(void)
{
    // Mapping MMIO regs
    mmio_base = ioremap(NEOPROFILER_MMIO_BASE, NEOPROFILER_REGION_SIZE);
    if (!mmio_base) {
        printk("Failed to map NeoProf MMIO registers\n");
        return -ENOMEM;
    }
    test_rw();
    return 0;
}

static void __exit neoprof_exit(void)
{
    if (mmio_base)
        iounmap(mmio_base);
}
/*
 * Read out the number of total hot pages
 */
u32 get_nr_hotpages(void)
{
    return neoprof_read(HOTPAGE_NUM_REG);
}
/*
 * Read out the addresses of hot pages in CXL memory TODO: how to read out the addresses of hot pages in DRAM
 */
u64 get_hotpage(void)
{
    volatile u64 hp_addr = neoprof_read(HOTPAGE_REG); // [63:12] is the address of hot page
    if (hp_addr < DDR_OFFSET){
        printk("Error: hot page address is not in DDR\n");
        return 0;
    }
    hp_addr = ((hp_addr - DDR_OFFSET) << 12) + CXL_MEM_BASE; 
    return hp_addr;
}
/*
 * Set the hotness threshold
 */
void set_hotness_threshold(u32 threshold)
{
    neoprof_write(THRESHOLD_SET_REG, threshold);
}

void reset_neoprof(void)
{
    neoprof_write(RESET_REG, 0x1);
}


module_init(neoprof_init);
module_exit(neoprof_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("PKUZHOU");
MODULE_DESCRIPTION("Neoprofiler Linux driver");