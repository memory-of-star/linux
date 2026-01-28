#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/configfs.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AI Assistant");
MODULE_DESCRIPTION("Dynamic NUMA Node Creator via Configfs");

/* 
 * Data structure representing a dynamic NUMA node configuration 
 */
struct dyn_numa_node {
    struct config_item item;
    u64 phys_addr;      /* Start physical address */
    u64 size;           /* Size in bytes */
    int nid;            /* Target NUMA Node ID */
    bool active;        /* State: true=online, false=offline */
};

static inline struct dyn_numa_node *to_dyn_node(struct config_item *item)
{
    return item ? container_of(item, struct dyn_numa_node, item) : NULL;
}

/* --- Sysfs/Configfs Attribute Show/Store Functions --- */

static ssize_t dyn_node_phys_addr_show(struct config_item *item, char *page)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    return sprintf(page, "0x%llx\n", node->phys_addr);
}

static ssize_t dyn_node_phys_addr_store(struct config_item *item,
                                      const char *page, size_t count)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    u64 val;
    int ret;

    if (node->active) {
        pr_err("dyn_numa: Cannot change address while node is active\n");
        return -EBUSY;
    }

    ret = kstrtoull(page, 0, &val);
    if (ret)
        return ret;

    node->phys_addr = val;
    return count;
}

static ssize_t dyn_node_size_show(struct config_item *item, char *page)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    return sprintf(page, "0x%llx\n", node->size);
}

static ssize_t dyn_node_size_store(struct config_item *item,
                                 const char *page, size_t count)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    u64 val;
    int ret;

    if (node->active) {
        pr_err("dyn_numa: Cannot change size while node is active\n");
        return -EBUSY;
    }

    ret = kstrtoull(page, 0, &val);
    if (ret)
        return ret;

    node->size = val;
    return count;
}

static ssize_t dyn_node_nid_show(struct config_item *item, char *page)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    return sprintf(page, "%d\n", node->nid);
}

static ssize_t dyn_node_nid_store(struct config_item *item,
                                const char *page, size_t count)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    int val;
    int ret;

    if (node->active) {
        pr_err("dyn_numa: Cannot change node ID while node is active\n");
        return -EBUSY;
    }

    ret = kstrtoint(page, 0, &val);
    if (ret)
        return ret;

    if (val < 0 || val >= MAX_NUMNODES) {
        pr_err("dyn_numa: Invalid Node ID %d (MAX=%d)\n", val, MAX_NUMNODES);
        return -EINVAL;
    }

    node->nid = val;
    return count;
}

static ssize_t dyn_node_state_show(struct config_item *item, char *page)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    return sprintf(page, "%s\n", node->active ? "online" : "offline");
}

static ssize_t dyn_node_state_store(struct config_item *item,
                                  const char *page, size_t count)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    bool enable;
    int ret;
    unsigned long block_sz = memory_block_size_bytes();

    if (sysfs_streq(page, "online") || sysfs_streq(page, "1"))
        enable = true;
    else if (sysfs_streq(page, "offline") || sysfs_streq(page, "0"))
        enable = false;
    else {
        pr_err("dyn_numa: Invalid state. Use 'online' or 'offline'\n");
        return -EINVAL;
    }

    if (enable == node->active)
        return count;

    if (enable) {
        /* --- ENABLE LOGIC --- */
        
        /* 1. Alignment Check */
        if (!IS_ALIGNED(node->phys_addr, block_sz) ||
            !IS_ALIGNED(node->size, block_sz)) {
            pr_err("dyn_numa: Error: Address (0x%llx) or size (0x%llx) not aligned to memory block size (0x%lx)\n",
                   node->phys_addr, node->size, block_sz);
            return -EINVAL;
        }

        if (node->size == 0) {
            pr_err("dyn_numa: Error: Size cannot be zero\n");
            return -EINVAL;
        }
        
        pr_info("dyn_numa: Attempting to add memory: nid=%d, start=0x%llx, size=0x%llx\n", 
                node->nid, node->phys_addr, node->size);

        /* 
         * 2. Register Memory
         * We use add_memory() directly which is suitable for taking over memory 
         * that might be presented as "Soft Reserved" or raw ranges (like CXL windows 
         * after unbinding kmem).
         */
        
        /* 
         * MHP_MEMMAP_ON_MEMORY is highly recommended for CXL/Device memory to avoid
         * wasting main memory for struct pages.
         */
        ret = add_memory(node->nid, node->phys_addr, node->size, MHP_MEMMAP_ON_MEMORY);
        
        /* Fallback: If MHP_MEMMAP_ON_MEMORY fails (e.g. due to size/alignment), try without it */
        if (ret) {
            pr_warn("dyn_numa: add_memory(MHP_MEMMAP_ON_MEMORY) failed: %d. Retrying with MHP_NONE...\n", ret);
            ret = add_memory(node->nid, node->phys_addr, node->size, MHP_NONE);
        }

        if (ret) {
            pr_err("dyn_numa: add_memory failed: %d. \n"
                   "Troubleshooting:\n"
                   "1. Check /proc/iomem for overlaps.\n"
                   "2. If targeting CXL memory, ensure 'kmem' driver is unloaded/unbound.\n", ret);
            return ret;
        }
        
        /* 
         * 3. Online Memory Blocks
         * add_memory() adds the memory sections but they might default to offline.
         * Since walk_memory_blocks is not exported to modules in all kernels,
         * we rely on userspace (udev or script) to online the memory blocks.
         * e.g., echo online > /sys/devices/system/memory/memoryXXX/state
         */
        pr_info("dyn_numa: Memory added. Please online blocks in /sys/devices/system/memory/ manually if udev didn't.\n");
        
        /* 
         * Old code removed due to missing symbols:
         * ret = walk_memory_blocks(node->phys_addr, node->size, NULL, online_memory_block_cb);
         */
         
         node->active = true;

    } else {
        /* --- DISABLE LOGIC --- */
        
        pr_info("dyn_numa: Removing memory: nid=%d, start=0x%llx, size=0x%llx\n", 
                node->nid, node->phys_addr, node->size);
        
        /* 
         * offline_and_remove_memory() is the modern helper that:
         * 1. Offlines the memory blocks (migrating pages if movable)
         * 2. Removes the memory from the kernel
         * Note: This can fail if memory is pinned or kernel non-movable pages ended up here.
         */
        ret = offline_and_remove_memory(node->phys_addr, node->size);
        if (ret) {
            pr_err("dyn_numa: offline_and_remove_memory failed: %d. Memory might be in use.\n", ret);
            return ret;
        }
        
        node->active = false;
        pr_info("dyn_numa: Node %d memory removed\n", node->nid);
    }

    return count;
}

/* --- Configfs Boilerplate --- */

CONFIGFS_ATTR(dyn_node_, phys_addr);
CONFIGFS_ATTR(dyn_node_, size);
CONFIGFS_ATTR(dyn_node_, nid);
CONFIGFS_ATTR(dyn_node_, state);

static struct configfs_attribute *dyn_node_attrs[] = {
    &dyn_node_attr_phys_addr,
    &dyn_node_attr_size,
    &dyn_node_attr_nid,
    &dyn_node_attr_state,
    NULL,
};

static void dyn_node_release(struct config_item *item)
{
    struct dyn_numa_node *node = to_dyn_node(item);
    if (node->active) {
        pr_info("dyn_numa: Cleaning up active node %s on removal\n", item->ci_name);
        offline_and_remove_memory(node->phys_addr, node->size);
    }
    kfree(node);
}

static struct configfs_item_operations dyn_node_item_ops = {
    .release = dyn_node_release,
};

static const struct config_item_type dyn_node_type = {
    .ct_item_ops = &dyn_node_item_ops,
    .ct_attrs = dyn_node_attrs,
    .ct_owner = THIS_MODULE,
};

static struct config_item *dyn_numa_make_item(struct config_group *group,
                                            const char *name)
{
    struct dyn_numa_node *node;

    node = kzalloc(sizeof(*node), GFP_KERNEL);
    if (!node)
        return ERR_PTR(-ENOMEM);

    config_item_init_type_name(&node->item, name, &dyn_node_type);
    node->nid = 0; 
    node->active = false;
    
    return &node->item;
}

static struct configfs_group_operations dyn_numa_group_ops = {
    .make_item = dyn_numa_make_item,
};

static const struct config_item_type dyn_numa_group_type = {
    .ct_group_ops = &dyn_numa_group_ops,
    .ct_owner = THIS_MODULE,
};

static struct configfs_subsystem dyn_numa_subsys;

static int __init dyn_numa_init(void)
{
    config_group_init(&dyn_numa_subsys.su_group);
    mutex_init(&dyn_numa_subsys.su_mutex);
    config_group_init_type_name(&dyn_numa_subsys.su_group, "dyn_numa", &dyn_numa_group_type);

    pr_info("dyn_numa: Module loaded. Create nodes in /sys/kernel/config/dyn_numa/\n");
    return configfs_register_subsystem(&dyn_numa_subsys);
}

static void __exit dyn_numa_exit(void)
{
    configfs_unregister_subsystem(&dyn_numa_subsys);
    pr_info("dyn_numa: Module unloaded\n");
}

module_init(dyn_numa_init);
module_exit(dyn_numa_exit);
