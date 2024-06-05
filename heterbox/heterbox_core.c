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
 *  cache_debug_info
 */
static ssize_t cache_debug_info_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u64 cache_hit_num_channel_0 = get_cache_hit_num(0);
    u64 cache_hit_num_channel_1 = get_cache_hit_num(1);
    u64 cache_miss_num_channel_0 = get_cache_miss_num(0);
    u64 cache_miss_num_channel_1 = get_cache_miss_num(1);

    return sysfs_emit(buf, "cache_hit_num_channel_0: %lld\n" \
                           "cache_hit_num_channel_1: %lld\n" \
                           "cache_miss_num_channel_0: %lld\n" \
                           "cache_miss_num_channel_1: %lld\n", \
                           cache_hit_num_channel_0, cache_hit_num_channel_1, cache_miss_num_channel_0, cache_miss_num_channel_1);
}

static ssize_t cache_debug_info_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    return count;
}

static struct kobj_attribute cache_debug_info_attr =
    __ATTR(cache_debug_info, 0644, cache_debug_info_show,
           cache_debug_info_store);


/* 
 *  migrate_address_fast
 */

static ssize_t migrate_address_fast_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 migrate_address_fast = get_migrate_address_fast();
    return sysfs_emit(buf, "%u\n", migrate_address_fast);
}

static ssize_t migrate_address_fast_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 migrate_address_fast;
    u32 migrate_address_fast_device;

    ret = kstrtou32(buf, 0, &migrate_address_fast);
    if (ret)
        return ret;

    migrate_address_fast_device = pfn_to_device_addr(migrate_address_fast);

    set_migrate_address_fast(migrate_address_fast_device);
    return count;
}

static struct kobj_attribute migrate_address_fast_attr =
    __ATTR(migrate_address_fast, 0644, migrate_address_fast_show,
           migrate_address_fast_store);


/* 
 *  migrate_address_slow
 */

static ssize_t migrate_address_slow_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 migrate_address_slow = get_migrate_address_slow();
    return sysfs_emit(buf, "%u\n", migrate_address_slow);
}

static ssize_t migrate_address_slow_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 migrate_address_slow;
    u32 migrate_address_slow_device;

    ret = kstrtou32(buf, 0, &migrate_address_slow);
    if (ret)
        return ret;

    migrate_address_slow_device = pfn_to_device_addr(migrate_address_slow);

    set_migrate_address_slow(migrate_address_slow_device);
    return count;
}

static struct kobj_attribute migrate_address_slow_attr =
    __ATTR(migrate_address_slow, 0644, migrate_address_slow_show,
           migrate_address_slow_store);


/* 
 *  start_migrate
 */

static ssize_t start_migrate_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 start_migrate = 0;
    return sysfs_emit(buf, "%u\n", start_migrate);
}

static ssize_t start_migrate_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 num;

    ret = kstrtou32(buf, 0, &num);
    if (ret)
        return ret;

    if (num) {
        start_migrate();
    }
    return count;
}

static struct kobj_attribute start_migrate_attr =
    __ATTR(start_migrate, 0644, start_migrate_show,
           start_migrate_store);


/* 
 *  sketch_threshold
 */

static ssize_t sketch_threshold_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 sketch_threshold = get_sketch_threshold();
    return sysfs_emit(buf, "%u\n", sketch_threshold);
}

static ssize_t sketch_threshold_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 sketch_threshold;

    ret = kstrtou32(buf, 0, &sketch_threshold);
    if (ret)
        return ret;

    set_sketch_threshold(sketch_threshold);
    return count;
}

static struct kobj_attribute sketch_threshold_attr =
    __ATTR(sketch_threshold, 0644, sketch_threshold_show,
           sketch_threshold_store);


/* 
 *  sketch_reset_period
 */

static ssize_t sketch_reset_period_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 sketch_reset_period = get_sketch_reset_period();
    return sysfs_emit(buf, "%u\n", sketch_reset_period);
}

static ssize_t sketch_reset_period_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 sketch_reset_period;

    ret = kstrtou32(buf, 0, &sketch_reset_period);
    if (ret)
        return ret;

    set_sketch_reset_period(sketch_reset_period);
    return count;
}

static struct kobj_attribute sketch_reset_period_attr =
    __ATTR(sketch_reset_period, 0644, sketch_reset_period_show,
           sketch_reset_period_store);


/* 
 *  migrate_page_cnt
 */

static ssize_t migrate_page_cnt_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 migrate_page_cnt = get_migrate_page_cnt();
    return sysfs_emit(buf, "%u\n", migrate_page_cnt);
}

static ssize_t migrate_page_cnt_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    return count;
}

static struct kobj_attribute migrate_page_cnt_attr =
    __ATTR(migrate_page_cnt, 0644, migrate_page_cnt_show,
           migrate_page_cnt_store);


/* 
 *  migrate_page_limit
 */

static ssize_t migrate_page_limit_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 migrate_page_limit = get_migrate_page_limit();
    return sysfs_emit(buf, "%u\n", migrate_page_limit);
}

static ssize_t migrate_page_limit_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 migrate_page_limit;

    ret = kstrtou32(buf, 0, &migrate_page_limit);
    if (ret)
        return ret;

    set_migrate_page_limit(migrate_page_limit);
    return count;
}

static struct kobj_attribute migrate_page_limit_attr =
    __ATTR(migrate_page_limit, 0644, migrate_page_limit_show,
           migrate_page_limit_store);


/* 
 *  migrate_page_limit_period
 */

static ssize_t migrate_page_limit_period_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 migrate_page_limit_period = get_migrate_page_limit_period();
    return sysfs_emit(buf, "%u\n", migrate_page_limit_period);
}

static ssize_t migrate_page_limit_period_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 migrate_page_limit_period;

    ret = kstrtou32(buf, 0, &migrate_page_limit_period);
    if (ret)
        return ret;

    set_migrate_page_limit_period(migrate_page_limit_period);
    return count;
}

static struct kobj_attribute migrate_page_limit_period_attr =
    __ATTR(migrate_page_limit_period, 0644, migrate_page_limit_period_show,
           migrate_page_limit_period_store);


/* 
 *  migrate_page_cnt_executed
 */

static ssize_t migrate_page_cnt_executed_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 migrate_page_cnt_executed = get_migrate_page_cnt_executed();
    return sysfs_emit(buf, "%u\n", migrate_page_cnt_executed);
}

static ssize_t migrate_page_cnt_executed_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 migrate_page_cnt_executed;

    ret = kstrtou32(buf, 0, &migrate_page_cnt_executed);
    if (ret)
        return ret;

    set_migrate_page_cnt_executed(migrate_page_cnt_executed);
    return count;
}

static struct kobj_attribute migrate_page_cnt_executed_attr =
    __ATTR(migrate_page_cnt_executed, 0644, migrate_page_cnt_executed_show,
           migrate_page_cnt_executed_store);


/* 
 *  migrate_page_cnt_executed
 */

static ssize_t cold_scan_period_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    u32 cold_scan_period = get_cold_scan_period();
    return sysfs_emit(buf, "%u\n", cold_scan_period);
}

static ssize_t cold_scan_period_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 cold_scan_period;

    ret = kstrtou32(buf, 0, &cold_scan_period);
    if (ret)
        return ret;

    set_cold_scan_period(cold_scan_period);
    return count;
}

static struct kobj_attribute cold_scan_period_attr =
    __ATTR(cold_scan_period, 0644, cold_scan_period_show,
           cold_scan_period_store);


/*
 * heterbox_attrs
 */

static struct attribute *heterbox_attrs[] = {
    &fast_memory_bound_attr.attr,
    &slow_memory_add_cycle_attr.attr,
    &remap_mode_attr.attr,
    &cache_debug_info_attr.attr,
    &migrate_address_fast_attr.attr,
    &migrate_address_slow_attr.attr,
    &start_migrate_attr.attr,
    &sketch_threshold_attr.attr,
    &sketch_reset_period_attr.attr,
    &migrate_page_cnt_attr.attr,
    &migrate_page_limit_attr.attr,
    &migrate_page_limit_period_attr.attr,
    &migrate_page_cnt_executed_attr.attr,
    &cold_scan_period_attr.attr,
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