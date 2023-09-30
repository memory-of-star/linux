#include <linux/list.h>
#include <linux/types.h>
#include "../drivers/neoprof/neoprof.h"
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

// #define DEBUG 1

static struct list_head hp_entry;
// static struct mutex kneomemd_lock;
static struct task_struct * kneomemd;

static unsigned long migrated_pages = 0;
static int iter = 0;
static DECLARE_WAIT_QUEUE_HEAD(kneomemd_wait);

static unsigned long wait_time = 100000; // 100ms
static int FAST_NODE_ID = 0;
// static u64 migration_cnt = 0;
static u64 scan_cnt = 0;
static u32 clear_interval = 10;
static u32 hotness_threshold = 2;

// for state monitor
static u32 state_sample_interval = 100;
static u32 total_state_sample_cnt = 0;
static u32 wr_state_sample_cnt = 0;
static u32 rd_state_sample_cnt = 0;

static unsigned long long pfn_invalid_cnt = 0;
static unsigned long long folio_invalid_cnt = 0;
static unsigned long long folio_not_lru_cnt_after_get = 0;
static unsigned long long folio_unevictable_cnt = 0;
static unsigned long long folio_remained_cnt = 0;
static unsigned long long page_not_existed_cnt = 0;

#define DEBUG_COUNTER(name, times) \
	name += 1; \
	if (name % times == 0){ \
		printk(#name ": %lld\n", name); \
	}

static void get_states(void){
    total_state_sample_cnt = get_total_state_sample_cnt();
    wr_state_sample_cnt = get_wr_state_sample_cnt();
    rd_state_sample_cnt = get_rd_state_sample_cnt();
}

// for neoprof-base promotion
static bool neomem_promotion_enabled = false;

bool neomem_debug_enabled = false;

static struct hotpage {
    u64 paddr;
    struct list_head list;
}; 

static u64 nr_hotpages;

static void hotpage_add(u64 paddr)
{
    struct hotpage *hp = kmalloc(sizeof(*hp), GFP_KERNEL);
    hp->paddr = paddr;
    list_add(&hp->list, &hp_entry);
}

static int hotpage_clear(void)
{
    struct hotpage *hp, *tmp;
    list_for_each_entry_safe(hp, tmp, &hp_entry, list) {
        list_del(&hp->list);
        kfree(hp);
    }
    return 0;
}

static void hotpage_dump(void)
{
    struct hotpage *hp;
    list_for_each_entry(hp, &hp_entry, list) {
        pr_info("hotpage: %llx\n", hp->paddr);
    }
}

static void hotpage_init(void)
{
    INIT_LIST_HEAD(&hp_entry);
    nr_hotpages = 0;

    // init the hotness threshold
    set_hotness_threshold(hotness_threshold);
}

/*
 * Read hotpages from neoprof device
 */
static int get_hotpages_from_neoprof(void)
{
    nr_hotpages = get_nr_hotpages();
#ifdef DEBUG
    if (nr_hotpages > 0)
        printk("N Hotpages: %lu", nr_hotpages);
#endif 
    for(int i = 0; i < nr_hotpages; i++) {
        // the get_hotpage operation is distructive?
        u64 paddr = get_hotpage();
        hotpage_add(paddr);
    }
// #ifdef DEBUG
//     hotpage_dump();
// #endif
    return 0;   
}


static int neomem_migrate_pages(void)
{
    unsigned long pfn;
    struct hotpage *hp, *tmp;
    struct page *page;
    int nr_remaining;
    int nr_succeeded;
    struct folio *_folio;

    unsigned hp_count = 0;

    // folio_list is the list of pages to be migrated
    LIST_HEAD(folio_list);
    list_for_each_entry_safe(hp, tmp, &hp_entry, list) {
        pfn = PHYS_PFN(hp->paddr);
        hp_count++;
        if (!pfn_valid(pfn))
        {
            DEBUG_COUNTER(pfn_invalid_cnt, 100000)
            continue;
        }
        page = pfn_to_page(pfn);
        if (!page){
            DEBUG_COUNTER(page_not_existed_cnt, 10000)
            continue;
        }
        page = compound_head(page);
        pfn = page_to_pfn(page);

        struct folio *folio = damon_get_folio(pfn);
        if (!folio)
        {
            DEBUG_COUNTER(folio_invalid_cnt, 100000)
            continue;
        }
        if (!folio_isolate_lru(folio)) {
            folio_put(folio);
            DEBUG_COUNTER(folio_not_lru_cnt_after_get, 100000)
            continue;
        }
        if (folio_test_unevictable(folio)){

            DEBUG_COUNTER(folio_unevictable_cnt, 100000)
            folio_putback_lru(folio);
        }
        else{
            list_add(&folio->lru, &folio_list);
        }
        folio_put(folio);
    }

    // printk("hp_count: %ld\n", hp_count);

    nr_remaining = _neomem_migrate_pages(&folio_list, alloc_misplaced_dst_page,
                     NULL, 0, MIGRATE_ASYNC,
                     MR_NUMA_MISPLACED, &nr_succeeded);

    if (nr_remaining) {
        while (!list_empty(&folio_list)){
            _folio = list_entry(folio_list.next, struct folio, lru);
            list_del(&_folio->lru);
            folio_putback_lru(_folio);
            DEBUG_COUNTER(folio_remained_cnt, 10000)
        }
    }

    if (nr_succeeded) {
        iter += 1;
        migrated_pages += nr_succeeded;
        if (iter % 10 == 0){
            pr_err("%ld pages were migrated in the last 10 iterations\n", migrated_pages);
            migrated_pages = 0;
        }

    }
    BUG_ON(!list_empty(&folio_list));	

    return 0;
}

static void do_promotion(void)
{
    int err = 0;
    /*
     *  Step 1: get the hotpages from neoprof
     *  hotpages are appended to hp_entry
     */ 
    err = get_hotpages_from_neoprof();
    if(err)
    {
        pr_err("get_hotpages_from_neoprof failed\n");
        return;
    }
    /*
     * Step 2: migrate pages
     */
    err = neomem_migrate_pages();
    if(err)
    {
        pr_err("migrate_pages failed\n");
        return;
    }
    /*
     * Step 3: Clear pages
     */
    err = hotpage_clear();
    if(err)
    {
        pr_err("hotpage_clear failed\n");
        return;
    }
}

/*
 * Check whether current monitoring should be stopped
 *
 * Returns true if need to stop current monitoring.
 */
static bool kneomemd_need_stop(void)
{
    if (kthread_should_stop())
        return true;
    return false;
}

static void kneomem_usleep(unsigned long usecs)
{
    /* See Documentation/timers/timers-howto.rst for the thresholds */
    if (usecs > 20 * USEC_PER_MSEC)
        schedule_timeout_idle(usecs_to_jiffies(usecs));
    else
        usleep_idle_range(usecs, usecs + 1);
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kneomemd_fn(void * data)
{
    pr_info("kneomemd starts\n");
    while (!kneomemd_need_stop()) {        
        // control the scan frequency
        kneomem_usleep(wait_time);
        // get the states
        get_states();

        // perform promotion
        if(!neomem_promotion_enabled){
            wait_event_interruptible(kneomemd_wait, neomem_promotion_enabled);
        }

        do_promotion();
        scan_cnt++;
        if(scan_cnt % clear_interval == 0){
            // clear the states in neoprof
            reset_neoprof();
        }
    }
    pr_info("kneomem finishes\n");
    kneomemd = NULL;
    return 0;
}

/*
 * __damon_start() - Starts monitoring.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int neomem_start(void)
{
    hotpage_init();
    
    int err;
    err = -EBUSY;
    void * ctx; // no use
    kneomemd = kthread_run(kneomemd_fn, ctx, "kdamond.%d", 1);
    if (IS_ERR(kneomemd)) {
        err = PTR_ERR(kneomemd);
        kneomemd = NULL;
    }
    return err;
}

static int __neomem_stop(void)
{
    if(kneomemd) {
        get_task_struct(kneomemd);
        kthread_stop(kneomemd);
        put_task_struct(kneomemd);
        return 0;
    }
    return -EPERM;
}

int neomem_stop(void)
{
    int err = 0;
    err = __neomem_stop();
    return err;
}

/*
 * sysfs interface
 */

static ssize_t neomem_promotion_enabled_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  neomem_promotion_enabled ? "true" : "false");
}

static ssize_t neomem_promotion_enabled_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;

	ret = kstrtobool(buf, &neomem_promotion_enabled);
    if (neomem_promotion_enabled)
        wake_up_interruptible(&kneomemd_wait);
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute neomem_promotion_enabled_attr =
	__ATTR(neomem_enabled, 0644, neomem_promotion_enabled_show,
	       neomem_promotion_enabled_store);


/* 
 * hotness threshold
 */

static ssize_t neomem_threshold_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u\n", hotness_threshold);
}

static ssize_t neomem_threshold_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 threshold;

    ret = kstrtou32(buf, 0, &threshold);
    if (ret)
        return ret;

    hotness_threshold = threshold;
    set_hotness_threshold(hotness_threshold);

    return count;
}

static struct kobj_attribute neomem_threshold_attr =
    __ATTR(hot_threshold, 0644, neomem_threshold_show,
           neomem_threshold_store);


/* 
 *  Migration interval (wait_time)
 */

static ssize_t neomem_migration_interval_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u\n", wait_time);
}

static ssize_t neomem_migration_interval_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 new_wait_time;

    ret = kstrtou32(buf, 0, &new_wait_time);
    if (ret)
        return ret;

    wait_time = new_wait_time;
    return count;
}

static struct kobj_attribute neomem_migration_interval_attr =
	__ATTR(migration_interval, 0644, neomem_migration_interval_show,
	       neomem_migration_interval_store);

/* 
 *  Clear interval
 */

static ssize_t neomem_clear_interval_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u\n", clear_interval);
}

static ssize_t neomem_clear_interval_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 new_clear_interval;

    ret = kstrtou32(buf, 0, &new_clear_interval);
    if (ret)
        return ret;

    clear_interval = new_clear_interval;
    return count;
}

static struct kobj_attribute neomem_clear_interval_attr =
	__ATTR(clear_interval, 0644, neomem_clear_interval_show,
	       neomem_clear_interval_store);


/* 
 *  State sampling interval
 */
static ssize_t neomem_state_sample_interval_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u\n", state_sample_interval);
}

static ssize_t neomem_state_sample_interval_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 new_state_sample_interval;

    ret = kstrtou32(buf, 0, &new_state_sample_interval);
    if (ret)
        return ret;

    state_sample_interval = new_state_sample_interval;
    set_state_sample_interval(state_sample_interval);
    return count;
}

static struct kobj_attribute neomem_state_sample_interval_attr =
	__ATTR(state_sample_interval, 0644, neomem_state_sample_interval_show,
	       neomem_state_sample_interval_store);


/*
 * Show states
 */
static ssize_t neomem_states_show(struct kobject *kobj,
                      struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%u %u %u\n", total_state_sample_cnt, rd_state_sample_cnt, wr_state_sample_cnt);
}

static struct kobj_attribute neomem_states_attr =
	__ATTR(states_show, 0644, neomem_states_show,
	       NULL);


static ssize_t neomem_debug_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  neomem_debug_enabled ? "true" : "false");
}

static ssize_t neomem_debug_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;

	ret = kstrtobool(buf, &neomem_debug_enabled);

	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute neomem_debug_attr =
	__ATTR(debug, 0644, neomem_debug_show,
	       neomem_debug_store);

static struct attribute *neomem_attrs[] = {
	&neomem_promotion_enabled_attr.attr,
    &neomem_threshold_attr.attr,
    &neomem_migration_interval_attr.attr,
    &neomem_clear_interval_attr.attr,
    &neomem_state_sample_interval_attr.attr,
    &neomem_states_attr.attr, 
    &neomem_debug_attr.attr,
	NULL,
};

static const struct attribute_group neomem_attr_group = {
	.attrs = neomem_attrs,
};

static int __init neomem_init_sysfs(void)
{
	int err;
	struct kobject *neomem_kobj;

	neomem_kobj = kobject_create_and_add("neomem", mm_kobj);
	if (!neomem_kobj) {
		pr_err("failed to create neomem kobject\n");
		return -ENOMEM;
	}
	err = sysfs_create_group(neomem_kobj, &neomem_attr_group);
	if (err) {
		pr_err("failed to register neomem group\n");
		goto delete_obj;
	}
	return 0;

delete_obj:
	kobject_put(neomem_kobj);
	return err;
}
subsys_initcall(neomem_init_sysfs);