#include "neoprof.h"

// #define NEOPROFILER_MMIO_BASE 0x20feffa00000L
#define NEOPROFILER_MMIO_BASE 0x2000L
#define NEOPROFILER_REGION_SIZE 0x1000

#define REG_STATUS          0x00
#define REG_NR_HOTPAGES     0x01
#define REG_THRESHOLD       0x02
#define REG_DATA            0x03

static void __iomem *mmio_base;

// Reg read
static inline unsigned int neoprof_read(unsigned long long reg_offset)
{
    return readl(mmio_base + reg_offset);
}

// Reg write
static inline void neoprof_write(unsigned int reg_offset, unsigned int value)
{
    writel(value, mmio_base + reg_offset);
}

static int __init neoprof_init(void)
{
    // Mapping MMIO regs
    mmio_base = ioremap(NEOPROFILER_MMIO_BASE, NEOPROFILER_REGION_SIZE);
    if (!mmio_base) {
        printk("Failed to map NeoProf MMIO registers\n");
        return -ENOMEM;
    }
    // Read example: read coprocessor status register
    unsigned int status = neoprof_read(REG_STATUS);
    pr_info("Neoprof counter: 0x%X\n", status);
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
u64 get_nr_hotpages(void)
{
    return neoprof_read(REG_NR_HOTPAGES);
}

/*
 * Read out the addresses of hot pages in CXL memory TODO: how to read out the addresses of hot pages in DRAM
 */
u64 get_hotpage(void)
{
    volatile u64 hp_addr = *(u64 *)(mmio_base + REG_DATA);
    return hp_addr;
}

/*
 * Set the hotness threshold
 */
void set_hotness_threshold(u64 threshold)
{
    neoprof_write(REG_THRESHOLD, threshold);
}

u64 get_hotness_threshold(void)
{
    return neoprof_read(REG_THRESHOLD);
}


module_init(neoprof_init);
module_exit(neoprof_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("PKUZHOU");
MODULE_DESCRIPTION("Neoprofiler Linux driver");