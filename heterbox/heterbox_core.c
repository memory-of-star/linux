#include <linux/list.h>
#include <linux/types.h>
#include "../drivers/heterbox_driver/heterbox_driver.h"
#include "../mm/damon/ops-common.h"
#include <linux/mm.h>
#include "../mm/internal.h"
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/migrate.h>
#include <linux/mm_inline.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/page-flags.h>
#include <linux/swap.h>


int heterbox_start(void){
    return 0;
}

/* 
 *  fast_memory_bound
 */
static ssize_t fast_memory_bound_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 fast_memory_bound = get_fast_memory_bound();
    return sysfs_emit(buf, "%u\n", fast_memory_bound);
}

static ssize_t fast_memory_bound_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 fast_memory_bound;

    ret = kstrtou32(buf, 0, &fast_memory_bound);
    if (ret)
        return ret;

    set_fast_memory_bound(fast_memory_bound);
    return count;
}

static struct kobj_attribute fast_memory_bound_attr =
    __ATTR(fast_memory_bound, 0644, fast_memory_bound_show,
           fast_memory_bound_store);


/* 
 *  slow_memory_add_cycle
 */
static ssize_t slow_memory_add_cycle_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 slow_memory_add_cycle = get_slow_memory_add_cycle();
    return sysfs_emit(buf, "%u\n", slow_memory_add_cycle);
}

static ssize_t slow_memory_add_cycle_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 slow_memory_add_cycle;

    ret = kstrtou32(buf, 0, &slow_memory_add_cycle);
    if (ret)
        return ret;

    set_slow_memory_add_cycle(slow_memory_add_cycle);
    return count;
}

static struct kobj_attribute slow_memory_add_cycle_attr =
    __ATTR(slow_memory_add_cycle, 0644, slow_memory_add_cycle_show,
           slow_memory_add_cycle_store);


/* 
 *  remap_mode
 */
static ssize_t remap_mode_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 remap_mode = get_remap_mode();
    return sysfs_emit(buf, "%u\n", remap_mode);
}

static ssize_t remap_mode_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 remap_mode;

    ret = kstrtou32(buf, 0, &remap_mode);
    if (ret)
        return ret;

    if (remap_mode){
        enable_remap_mode();
    }
    else{
        disable_remap_mode();
    }

    return count;
}

static struct kobj_attribute remap_mode_attr =
    __ATTR(remap_mode, 0644, remap_mode_show,
           remap_mode_store);


/*
 * heterbox_attrs
 */

static struct attribute *heterbox_attrs[] = {
    &fast_memory_bound_attr.attr,
    &slow_memory_add_cycle_attr.attr,
    &remap_mode_attr.attr,
	NULL,
};

static const struct attribute_group heterbox_attr_group = {
	.attrs = heterbox_attrs,
};

static int __init heterbox_init_sysfs(void)
{
	int err;
	struct kobject *heterbox_kobj;

	heterbox_kobj = kobject_create_and_add("heterbox", mm_kobj);
	if (!heterbox_kobj) {
		pr_err("failed to create heterbox kobject\n");
		return -ENOMEM;
	}
	err = sysfs_create_group(heterbox_kobj, &heterbox_attr_group);
	if (err) {
		pr_err("failed to register heterbox group\n");
		goto delete_obj;
	}
	return 0;

delete_obj:
	kobject_put(heterbox_kobj);
	return err;
}
subsys_initcall(heterbox_init_sysfs);