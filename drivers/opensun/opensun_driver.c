#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/configfs.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/node.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yiqi Chen");
MODULE_DESCRIPTION("OpenSun Manager");

/* =========================================================================
 *  Part 1: Dynamic NUMA Node Logic (Memory Hotplug) - Hierarchical
 * ========================================================================= */

struct opensun_numa_node {
    struct config_group group;
    int nid;
};

struct opensun_mem_range {
    struct config_item item;
    u64 phys_addr;
    u64 size;
    bool active;
};

static inline struct opensun_numa_node *to_os_numa_node(struct config_item *item)
{
    return item ? container_of(to_config_group(item), struct opensun_numa_node, group) : NULL;
}

static inline struct opensun_mem_range *to_os_mem_range(struct config_item *item)
{
    return item ? container_of(item, struct opensun_mem_range, item) : NULL;
}

/* --- Range Attributes --- */

static ssize_t os_range_addr_show(struct config_item *item, char *page)
{
    return sprintf(page, "0x%llx\n", to_os_mem_range(item)->phys_addr);
}

static ssize_t os_range_addr_store(struct config_item *item, const char *page, size_t count)
{
    struct opensun_mem_range *range = to_os_mem_range(item);
    u64 val;
    if (range->active) return -EBUSY;
    if (kstrtoull(page, 0, &val)) return -EINVAL;
    range->phys_addr = val;
    return count;
}

static ssize_t os_range_size_show(struct config_item *item, char *page)
{
    return sprintf(page, "0x%llx\n", to_os_mem_range(item)->size);
}

static ssize_t os_range_size_store(struct config_item *item, const char *page, size_t count)
{
    struct opensun_mem_range *range = to_os_mem_range(item);
    u64 val;
    if (range->active) return -EBUSY;
    if (kstrtoull(page, 0, &val)) return -EINVAL;
    range->size = val;
    return count;
}

static ssize_t os_range_state_show(struct config_item *item, char *page)
{
    return sprintf(page, "%s\n", to_os_mem_range(item)->active ? "online" : "offline");
}

static ssize_t os_range_state_store(struct config_item *item, const char *page, size_t count)
{
    struct opensun_mem_range *range = to_os_mem_range(item);
    struct opensun_numa_node *node = to_os_numa_node(item->ci_parent); /* Parent is the node group */
    bool enable;
    int ret;

    if (sysfs_streq(page, "online") || sysfs_streq(page, "1")) enable = true;
    else if (sysfs_streq(page, "offline") || sysfs_streq(page, "0")) enable = false;
    else return -EINVAL;

    if (enable == range->active) return count;

    if (enable) {
        if (!IS_ALIGNED(range->phys_addr, memory_block_size_bytes()) || 
            !IS_ALIGNED(range->size, memory_block_size_bytes()) || 
            range->size == 0) {
            pr_err("opensun: Invalid alignment or size. Block size: 0x%lx\n", memory_block_size_bytes());
            return -EINVAL;
        }

        /* Use parent node's nid */
        if (!node_possible(node->nid)) {
             pr_err("opensun: Node %d is not possible. Cannot add memory.\n", node->nid);
             return -EINVAL;
        }

        pr_info("opensun: Adding memory [0x%llx - 0x%llx] to Node %d\n", 
                range->phys_addr, range->phys_addr + range->size, node->nid);

        ret = add_memory(node->nid, range->phys_addr, range->size, MHP_MEMMAP_ON_MEMORY);
        if (ret) {
            ret = add_memory(node->nid, range->phys_addr, range->size, MHP_NONE);
        }

        if (ret) {
            pr_err("opensun: add_memory failed: %d. Check dmesg or /proc/iomem.\n", ret);
            return ret;
        }

        pr_info("opensun: Memory added. Please online blocks in /sys/devices/system/memory/ manually.\n");
        range->active = true;
    } else {
        pr_info("opensun: Removing memory from Node %d\n", node->nid);
        ret = offline_and_remove_memory(range->phys_addr, range->size);
        if (ret) {
            pr_err("opensun: Remove failed: %d\n", ret);
            return ret;
        }
        range->active = false;
    }
    return count;
}

CONFIGFS_ATTR(os_range_, addr);
CONFIGFS_ATTR(os_range_, size);
CONFIGFS_ATTR(os_range_, state);

static struct configfs_attribute *os_range_attrs[] = {
    &os_range_attr_addr,
    &os_range_attr_size,
    &os_range_attr_state,
    NULL,
};

static void os_range_release(struct config_item *item)
{
    struct opensun_mem_range *range = to_os_mem_range(item);
    if (range->active)
        offline_and_remove_memory(range->phys_addr, range->size);
    kfree(range);
}

static struct configfs_item_operations os_range_item_ops = {
    .release = os_range_release,
};

static const struct config_item_type os_range_type = {
    .ct_item_ops = &os_range_item_ops,
    .ct_attrs = os_range_attrs,
    .ct_owner = THIS_MODULE,
};

/* --- Node Attributes (The Group) --- */

static ssize_t os_node_nid_show(struct config_item *item, char *page)
{
    return sprintf(page, "%d\n", to_os_numa_node(item)->nid);
}

static ssize_t os_node_nid_store(struct config_item *item, const char *page, size_t count)
{
    struct opensun_numa_node *node = to_os_numa_node(item);
    int val;
    if (kstrtoint(page, 0, &val)) return -EINVAL;
    
    if (!node_possible(val)) {
        pr_err("opensun: Node %d is not possible. Check 'numa=' boot parameter or node_possible_map.\n", val);
        return -EINVAL;
    }
    
    node->nid = val;
    return count;
}

CONFIGFS_ATTR(os_node_, nid);

static struct configfs_attribute *os_node_attrs[] = {
    &os_node_attr_nid,
    NULL,
};

/* Node is a group, so it can create items (ranges) */
static struct config_item *os_node_make_item(struct config_group *group, const char *name)
{
    struct opensun_mem_range *range;
    range = kzalloc(sizeof(*range), GFP_KERNEL);
    if (!range) return ERR_PTR(-ENOMEM);
    config_item_init_type_name(&range->item, name, &os_range_type);
    range->phys_addr = 0;
    range->size = 0;
    range->active = false;
    return &range->item;
}

static void os_node_release(struct config_item *item)
{
    struct opensun_numa_node *node = to_os_numa_node(item);
    kfree(node);
}

static struct configfs_item_operations os_node_item_ops = {
    .release = os_node_release,
};

static struct configfs_group_operations os_node_group_ops = {
    .make_item = os_node_make_item,
};

static const struct config_item_type os_node_type = {
    .ct_item_ops = &os_node_item_ops,
    .ct_group_ops = &os_node_group_ops,
    .ct_attrs = os_node_attrs,
    .ct_owner = THIS_MODULE,
};

/* --- Root Group Operations (Create Nodes) --- */

static struct config_group *os_mem_make_group(struct config_group *group, const char *name)
{
    struct opensun_numa_node *node;
    node = kzalloc(sizeof(*node), GFP_KERNEL);
    if (!node) return ERR_PTR(-ENOMEM);
    config_group_init_type_name(&node->group, name, &os_node_type);
    node->nid = 0; /* Default nid */
    return &node->group;
}

static struct configfs_group_operations os_mem_group_ops = {
    .make_group = os_mem_make_group,
};

static const struct config_item_type os_mem_group_type = {
    .ct_group_ops = &os_mem_group_ops,
    .ct_owner = THIS_MODULE,
};

/* =========================================================================
 *  Part 2: PCI Device Logic (Hardware Control)
 * ========================================================================= */

struct opensun_pci_dev {
    struct pci_dev *pdev;
    void __iomem *mmio_base; /* Points to BAR2 */
    struct config_group group;
};

static inline struct opensun_pci_dev *to_os_dev(struct config_item *item)
{
    return item ? container_of(to_config_group(item), struct opensun_pci_dev, group) : NULL;
}

/* 
 * Macro to define a register access file in configfs.
 * _name: Filename
 * _offset: Offset from BAR2 base
 */
#define DEFINE_OPENSUN_REG(_name, _offset)                                  \
static ssize_t os_dev_##_name##_show(struct config_item *item, char *page)  \
{                                                                           \
    struct opensun_pci_dev *dev = to_os_dev(item);                          \
    u32 val = 0;                                                            \
    if (dev->mmio_base)                                                     \
        val = readl(dev->mmio_base + (_offset));                            \
    return sprintf(page, "0x%x\n", val);                                    \
}                                                                           \
static ssize_t os_dev_##_name##_store(struct config_item *item,             \
                                      const char *page, size_t count)       \
{                                                                           \
    struct opensun_pci_dev *dev = to_os_dev(item);                          \
    u32 val;                                                                \
    if (kstrtou32(page, 0, &val)) return -EINVAL;                           \
    if (dev->mmio_base) {                                                   \
        writel(val, dev->mmio_base + (_offset));                            \
        /* pr_info("opensun: Wrote 0x%x to " #_name "\n", val); */          \
    }                                                                       \
    return count;                                                           \
}                                                                           \
CONFIGFS_ATTR(os_dev_, _name)

/* --- Register Definitions (YOU CAN ADD MORE HERE) --- */


DEFINE_OPENSUN_REG(test_reg, 0x100);
DEFINE_OPENSUN_REG(axi_size, 0x700);
DEFINE_OPENSUN_REG(device_bias, 0x800);
DEFINE_OPENSUN_REG(axi_user, 0x900);
DEFINE_OPENSUN_REG(byte_enable, 0x1200);
DEFINE_OPENSUN_REG(add_latency, 0x2a00);
DEFINE_OPENSUN_REG(enable, 0x2b00);
DEFINE_OPENSUN_REG(enable_dma_engine, 0x2c00);
DEFINE_OPENSUN_REG(base_addr, 0x3900);
DEFINE_OPENSUN_REG(function_write_base_addr, 0x3a00);
DEFINE_OPENSUN_REG(addr_remap_table_index, 0x4000);
DEFINE_OPENSUN_REG(addr_remap_table_value, 0x4100);



static ssize_t os_dev_info_show(struct config_item *item, char *page)
{
    struct opensun_pci_dev *dev = to_os_dev(item);
    return sprintf(page, "%s (Vendor: %04x, Device: %04x, BAR2: %p)\n", 
                   pci_name(dev->pdev), dev->pdev->vendor, dev->pdev->device, dev->mmio_base);
}
CONFIGFS_ATTR_RO(os_dev_, info);

static struct configfs_attribute *os_dev_attrs[] = {
    &os_dev_attr_info,
    &os_dev_attr_test_reg,
    &os_dev_attr_axi_size,
    &os_dev_attr_device_bias,
    &os_dev_attr_axi_user,
    &os_dev_attr_byte_enable,
    &os_dev_attr_add_latency,
    &os_dev_attr_enable,
    &os_dev_attr_enable_dma_engine,
    &os_dev_attr_base_addr,
    &os_dev_attr_function_write_base_addr,
    &os_dev_attr_addr_remap_table_index,
    &os_dev_attr_addr_remap_table_value,
    NULL,
};

static struct configfs_item_operations os_dev_item_ops = {
    .release = NULL, /* Managed by PCI probe/remove lifecycle */
};

static const struct config_item_type os_dev_type = {
    .ct_item_ops = &os_dev_item_ops,
    .ct_attrs = os_dev_attrs,
    .ct_owner = THIS_MODULE,
};

/* The 'devices' directory group */
static struct config_group os_devices_group;
static const struct config_item_type os_devices_group_type = {
    .ct_owner = THIS_MODULE,
};

/* --- PCI Probe/Remove --- */

static int opensun_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct opensun_pci_dev *dev;
    int ret;

    /* 
     * IMPORTANT: Only bind to Function 1 (.1) which has the BAR2 region.
     */
    if (PCI_FUNC(pdev->devfn) != 1) {
        return 0;
    }

    pr_info("opensun: Probing Function 1 device %s\n", pci_name(pdev));

    ret = pci_enable_device(pdev);
    if (ret) return ret;

    ret = pci_request_regions(pdev, "opensun_driver");
    if (ret) goto err_disable;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        ret = -ENOMEM;
        goto err_regions;
    }
    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    /* Map BAR2 (Region 2) */
    dev->mmio_base = pci_iomap(pdev, 2, 0);
    if (!dev->mmio_base) {
        pr_warn("opensun: Failed to map BAR2 for %s\n", pci_name(pdev));
    } else {
        pr_info("opensun: Mapped BAR2 for %s\n", pci_name(pdev));
        
        /* 
         * --- Initialize Registers ---
         * Write default values to registers upon driver load.
         */
        
        /* Set axi_size to 5 */
        writeq(0x5, dev->mmio_base + 0x700);
        /* Set device_bias to 0 */
        writeq(0x0, dev->mmio_base + 0x800);
        /* Set axi_user to 0 */
        writeq(0x0, dev->mmio_base + 0x900);
        /* Set byte_enable to all 1s */
        writeq(0xffffffffffffffffULL, dev->mmio_base + 0x1200);
        /* Set add_latency to 0 */
        writeq(0x0, dev->mmio_base + 0x2a00);
        /* Set enable to 0 */
        writeq(0x0, dev->mmio_base + 0x2b00);
        /* Set enable_dma_engine to 0 */
        writeq(0x0, dev->mmio_base + 0x2c00);
        /* Set base_addr to 0 */
        writeq(0x0, dev->mmio_base + 0x3900);
        /* Set function_write_base_addr to 0 */
        writeq(0x0, dev->mmio_base + 0x3a00);
    }

    /* Register in Configfs under /sys/kernel/config/opensun/devices/<slot> */
    config_group_init_type_name(&dev->group, pci_name(pdev), &os_dev_type);
    ret = configfs_register_group(&os_devices_group, &dev->group);
    if (ret) {
        pr_err("opensun: Failed to register device in configfs\n");
        goto err_iomap;
    }

    return 0;

err_iomap:
    if (dev->mmio_base) pci_iounmap(pdev, dev->mmio_base);
    kfree(dev);
err_regions:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

static void opensun_pci_remove(struct pci_dev *pdev)
{
    struct opensun_pci_dev *dev = pci_get_drvdata(pdev);
    
    if (PCI_FUNC(pdev->devfn) != 1 || !dev) {
        return;
    }

    configfs_unregister_group(&dev->group);
    if (dev->mmio_base) pci_iounmap(pdev, dev->mmio_base);
    kfree(dev);
    
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    pr_info("opensun: Removed device %s\n", pci_name(pdev));
}

static const struct pci_device_id opensun_ids[] = {
    { PCI_DEVICE(0x8086, 0x0ddb) }, /* Intel CXL Device */
    { 0, }
};
MODULE_DEVICE_TABLE(pci, opensun_ids);

static struct pci_driver opensun_pci_driver = {
    .name = "opensun_driver",
    .id_table = opensun_ids,
    .probe = opensun_pci_probe,
    .remove = opensun_pci_remove,
};

/* =========================================================================
 *  Part 3: Module Init/Exit
 * ========================================================================= */

static struct configfs_subsystem opensun_subsys;
static struct config_group os_mem_nodes_group;

static const struct config_item_type opensun_root_type = {
    .ct_owner = THIS_MODULE,
};

static int __init opensun_init(void)
{
    int ret;

    /* 1. Init Configfs Root: /sys/kernel/config/opensun/ */
    config_group_init(&opensun_subsys.su_group);
    mutex_init(&opensun_subsys.su_mutex);
    config_group_init_type_name(&opensun_subsys.su_group, "opensun", &opensun_root_type);
    
    ret = configfs_register_subsystem(&opensun_subsys);
    if (ret) return ret;

    /* 2. Create 'devices' group (Auto-managed) */
    config_group_init_type_name(&os_devices_group, "devices", &os_devices_group_type);
    ret = configfs_register_group(&opensun_subsys.su_group, &os_devices_group);
    if (ret) goto err_subsys;

    /* 3. Create 'memory_nodes' group (User-managed) */
    config_group_init_type_name(&os_mem_nodes_group, "memory_nodes", &os_mem_group_type);
    ret = configfs_register_group(&opensun_subsys.su_group, &os_mem_nodes_group);
    if (ret) goto err_devices;

    /* 4. Register PCI Driver */
    ret = pci_register_driver(&opensun_pci_driver);
    if (ret) goto err_mem_nodes;

    pr_info("opensun_driver: Loaded. Configfs at /sys/kernel/config/opensun/\n");
    return 0;

err_mem_nodes:
    configfs_unregister_group(&os_mem_nodes_group);
err_devices:
    configfs_unregister_group(&os_devices_group);
err_subsys:
    configfs_unregister_subsystem(&opensun_subsys);
    return ret;
}

static void __exit opensun_exit(void)
{
    pci_unregister_driver(&opensun_pci_driver);
    configfs_unregister_group(&os_mem_nodes_group);
    configfs_unregister_group(&os_devices_group);
    configfs_unregister_subsystem(&opensun_subsys);
    pr_info("opensun_driver: Unloaded\n");
}

module_init(opensun_init);
module_exit(opensun_exit);
