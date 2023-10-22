#include <linux/list.h>
#include <linux/types.h>
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
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/vmalloc.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <asm/msr.h>
#include <asm/msr-index.h>

#define PAGE_NUMBER (0x247fffffff >> 12)
#define HOT_PAGE_MAX_NUM 1000

#define CPU_NUMBER 256
#define MAX_EVENT_NUM 10

#define DEBUG_COUNTER(name, times) \
	name += 1; \
	if (name % times == 0){ \
		printk(#name ": %lld\n", name); \
	}


// static struct mutex kneomemd_lock;
static struct task_struct * kneopebsd;

// an array store hotness of all pages
static u8 *page_access_times;

// an array store events for each cpu
struct perf_event *event_array[MAX_EVENT_NUM][CPU_NUMBER];
static int current_event_num = 0;

static unsigned long migrated_pages = 0;
static int iter = 0;
static DECLARE_WAIT_QUEUE_HEAD(kneopebsd_wait);

static unsigned long wait_time = 100000; // 100ms

static unsigned long long total_sample_cnt = 0;
static unsigned long long total_sample_failed_cnt = 0;

// for neoprof-base promotion
static bool neopebs_promotion_enabled = false;

//pebs_sampling_period: how many instructions it takes to overflow
static u32 pebs_sampling_period = 500;
static u32 pebs_config = 0x08d3;

static unsigned long long pebs_local_access_cnt = 0;
static unsigned long long pebs_remote_access_cnt = 0;

static unsigned long long pebs_pfn_not_valid_cnt = 0;
static unsigned long long pebs_page_not_valid_cnt = 0;
static unsigned long long pebs_folio_not_valid_cnt = 0;
static unsigned long long pebs_folio_isolated_failed_cnt = 0;
static unsigned long long pebs_folio_unevictable_cnt = 0;

// static struct hotpage {
//     u64 huge_pfn;
//     struct list_head list;
// }; 

static u64 hp_entry[HOT_PAGE_MAX_NUM];
static int hp_entry_num = 0; 



/*
 * Check whether current monitoring should be stopped
 *
 * Returns true if need to stop current monitoring.
 */
static bool kneopebsd_need_stop(void)
{
    if (kthread_should_stop())
        return true;
    return false;
}

static void kneopebs_usleep(unsigned long usecs)
{
    /* See Documentation/timers/timers-howto.rst for the thresholds */
    if (usecs > 20 * USEC_PER_MSEC)
        schedule_timeout_idle(usecs_to_jiffies(usecs));
    else
        usleep_idle_range(usecs, usecs + 1);
}


void neopebs_overflow_callback(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs) {
    u8 *page_access = NULL;
    u64 pfn, phys_addr, addr;
    u64 huge_pfn;
    // struct page *page;
    // struct folio *_folio;
    // int nr_remaining;
    // int nr_succeeded;
    // static unsigned long migrated_pages = 0;
    static unsigned long debug1 = 0;
    static unsigned long debug2 = 0;
    // folio_list is the list of pages to be migrated
    // LIST_HEAD(folio_list);
    

    phys_addr = data->phys_addr;
    addr = data->addr;
        
    pfn = (phys_addr >> 12);
    
    if ((pfn < NODE_DATA(1)->node_start_pfn) || (pfn > (NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages))){
        DEBUG_COUNTER(pebs_local_access_cnt, 1000)
        if (debug1 % 1000 == 0){
            printk("pfn: %lld, node1 start: %lld, node1 end: %lld, addr: %llx\n", pfn, NODE_DATA(1)->node_start_pfn, NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages, addr);
        }
        debug1 += 1;
        return;
    }
    else{
        DEBUG_COUNTER(pebs_remote_access_cnt, 1000)
        if (debug2 % 1000 == 0){
            printk("pfn: %lld, node0 start: %lld, node0 end: %lld, addr: %llx\n", pfn, NODE_DATA(0)->node_start_pfn, NODE_DATA(0)->node_start_pfn + NODE_DATA(0)->node_spanned_pages, addr);
        }
        debug2 += 1;
    }

    huge_pfn = (pfn >> 9);

    if (hp_entry_num < HOT_PAGE_MAX_NUM){
        hp_entry[hp_entry_num] = huge_pfn;
        hp_entry_num++;
    }

    pfn = (huge_pfn << 9);
    if ((pfn < NODE_DATA(1)->node_start_pfn) || (pfn > (NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages))){
        printk("pfn: %lld, node1 start: %lld, node1 end: %lld, huge_pfn << 9 n overflow fail, huge_pfn: %lld\n", pfn, NODE_DATA(1)->node_start_pfn, NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages, huge_pfn);
        return;
    }

    
}

int create_perf_events(int event_num) {
    int cpu;
    struct perf_event *event;
    struct perf_event_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_RAW; 
    // attr.config = 0x82d0;  // MEM_INST_RETIRED.ALL_STORES
    attr.config = pebs_config;
    attr.sample_period = pebs_sampling_period;

    // attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_WEIGHT | PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR;
    attr.sample_type = PERF_SAMPLE_PHYS_ADDR;
    attr.disabled = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_callchain_kernel = 1;
    attr.exclude_idle = 1;
    attr.precise_ip = 1;
    // attr.pinned = 1;
    attr.size = sizeof(attr);

    for_each_online_cpu(cpu) {
        event = perf_event_create_kernel_counter(&attr, cpu, NULL, 
                                                 neopebs_overflow_callback, NULL);
        if (IS_ERR(event)) {
            pr_info("Failed to create perf event for CPU %d\n", cpu);
            return PTR_ERR(event);
        }

        event_array[event_num][cpu] = event;

        perf_event_enable(event);
        printk("perf_event_enable, cpu: %d\n", cpu);
    }

    return 0;
}

void cleanup_perf_events(int event_num) {
    int cpu;
    for_each_online_cpu(cpu) {
        if (event_array[event_num][cpu]) {
            perf_event_disable(event_array[event_num][cpu]);
            perf_event_release_kernel(event_array[event_num][cpu]);
            event_array[event_num][cpu] = NULL;
        }
    }
}

void enable_perf_events(int event_num) {
    int cpu;
    for_each_online_cpu(cpu) {
        if (event_array[event_num][cpu]) {
            perf_event_enable(event_array[event_num][cpu]);
            printk("perf_event_enable, cpu: %d\n", cpu);
        }
    }
}

void disable_perf_events(int event_num) {
    int cpu;
    for_each_online_cpu(cpu) {
        if (event_array[event_num][cpu]) {
            perf_event_disable(event_array[event_num][cpu]);
            printk("perf_event_disable, cpu: %d\n", cpu);
        }
    }
}

// static int hotpage_list_clear(struct list_head _list_entry)
// {
//     struct hotpage *hp, *tmp;
//     list_for_each_entry_safe(hp, tmp, &_list_entry, list) {
//         list_del(&hp->list);
//         kfree(hp);
//     }
//     return 0;
// }

static void neopebsd_do_promotion(void){
    struct hotpage *hp, *tmp;
    u64 huge_pfn, pfn;
    struct page *page;
    struct folio *_folio;
    int nr_remaining;
    int nr_succeeded;
    static int debug1 = 0;
    u64 _hp_entry[HOT_PAGE_MAX_NUM];
    LIST_HEAD(folio_list);
    int _hp_entry_num = hp_entry_num;

    memcpy(_hp_entry, hp_entry, hp_entry_num * sizeof(u64));
    hp_entry_num = 0;

    for (int i = 0; i < _hp_entry_num; i++){
        huge_pfn = _hp_entry[i];

        pfn = (huge_pfn << 9);
        if ((pfn < NODE_DATA(1)->node_start_pfn) || (pfn > (NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages))){
            printk("pfn: %lld, node1 start: %lld, node1 end: %lld, huge_pfn << 9 fail, huge_pfn: %lld\n", pfn, NODE_DATA(1)->node_start_pfn, NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages, huge_pfn);
            return;
        }

        for (pfn = (huge_pfn << 9); pfn < ((huge_pfn + 1) << 9); pfn++){
            u64 _pfn;
            if (!pfn_valid(pfn))
            {
                DEBUG_COUNTER(pebs_pfn_not_valid_cnt, 1000)
                return;
            }
            page = pfn_to_page(pfn);
            if (!page){
                DEBUG_COUNTER(pebs_page_not_valid_cnt, 1000)
                return;
            }
            page = compound_head(page);
            _pfn = page_to_pfn(page);
            _pfn = pfn;

            if ((_pfn < NODE_DATA(1)->node_start_pfn) || (_pfn > (NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages))){
                if (debug1 % 1000 == 0){
                    printk("pfn: %lld, node1 start: %lld, node1 end: %lld, page_to_pfn fail\n", pfn, NODE_DATA(1)->node_start_pfn, NODE_DATA(1)->node_start_pfn + NODE_DATA(1)->node_spanned_pages);
                }
                debug1 += 1;
                continue;
            }

            _folio = damon_get_folio(_pfn);
            if (!_folio)
            {
                DEBUG_COUNTER(pebs_folio_not_valid_cnt, 1000)
                return;
            }
            if (!folio_isolate_lru(_folio)) {
                folio_put(_folio);
                DEBUG_COUNTER(pebs_folio_isolated_failed_cnt, 1000)
                return;
            }
            if (folio_test_unevictable(_folio)){
                DEBUG_COUNTER(pebs_folio_unevictable_cnt, 1000)
                folio_putback_lru(_folio);
                return;
            }
            else{
                list_add(&_folio->lru, &folio_list);
            }
            folio_put(_folio);
        }
    }

    nr_remaining = _neomem_migrate_pages(&folio_list, alloc_misplaced_dst_page,
                     NULL, 0, MIGRATE_ASYNC,
                     MR_NUMA_MISPLACED, &nr_succeeded);

    if (nr_remaining) {
        while (!list_empty(&folio_list)){
            _folio = list_entry(folio_list.next, struct folio, lru);
            list_del(&_folio->lru);
            folio_putback_lru(_folio);
        }
    }

    if (nr_succeeded) {
        migrated_pages += nr_succeeded;
    }
    BUG_ON(!list_empty(&folio_list));	
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kneopebsd_fn(void * data)
{
	long deadline = jiffies;
	long delta;
    pr_info("kneopebsd starts\n");
    iter = 0;

    while (!kneopebsd_need_stop()) {

        deadline += usecs_to_jiffies(wait_time);

        // perform promotion
        if(!neopebs_promotion_enabled){
            for (int i = 0; i < current_event_num; i++)
                disable_perf_events(i);
            wait_event_interruptible(kneopebsd_wait, neopebs_promotion_enabled);
            for (int i = 0; i < current_event_num; i++)
                enable_perf_events(i);
            deadline = jiffies + usecs_to_jiffies(wait_time);
        }

        neopebsd_do_promotion();

        delta = jiffies - deadline;
		if (delta < 0)
			schedule_timeout_interruptible(-delta);
		else if (delta >= HZ)
			pr_warn("profd running %ld.%02d seconds late",
					delta / HZ, (int)(delta % HZ) * 100 / HZ);

        iter += 1;
        if (iter % 10 == 0){
            printk("%ld pages were migrated in the last 10 iterations\n", migrated_pages);
            migrated_pages = 0;
        }
    }

    for (int i = 0; i < current_event_num; i++)
        cleanup_perf_events(i);
    current_event_num = 0;

    pr_info("kneomem finishes\n");
    kneopebsd = NULL;
    return 0;
}


int neo_pebs_start(void)
{
    int err;
    err = -EBUSY;
    void * ctx = NULL; 

    page_access_times = vmalloc(PAGE_NUMBER * sizeof(u8));

    if (!page_access_times){
        printk("Error: failed to allocate page_access_times\n");
        return -ENOMEM;
    }

    memset(page_access_times, 0 , PAGE_NUMBER * sizeof(u8));

    // INIT_LIST_HEAD(&hp_entry);

    kneopebsd = kthread_run(kneopebsd_fn, ctx, "kneopebsd.%d", 1);
    if (IS_ERR(kneopebsd)) {
        err = PTR_ERR(kneopebsd);
        kneopebsd = NULL;
    }
    return err;
}

static int __neo_pebs_stop(void)
{
    if(kneopebsd) {
        get_task_struct(kneopebsd);
        kthread_stop(kneopebsd);
        put_task_struct(kneopebsd);
        return 0;
    }
    return -EPERM;
}

int neo_pebs_stop(void)
{
    int err = 0;
    err = __neo_pebs_stop();
    return err;
}


// sysfs interface

static ssize_t neopebs_promotion_enabled_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  neopebs_promotion_enabled ? "true" : "false");
}

static ssize_t neopebs_promotion_enabled_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;

	ret = kstrtobool(buf, &neopebs_promotion_enabled);
    if (neopebs_promotion_enabled)
        wake_up_interruptible(&kneopebsd_wait);
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute neopebs_promotion_enabled_attr =
	__ATTR(neopebs_enabled, 0644, neopebs_promotion_enabled_show,
	       neopebs_promotion_enabled_store);


static ssize_t neopebs_sampling_interval_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", pebs_sampling_period);
}

static ssize_t neopebs_sampling_interval_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 new_sample_interval;

    ret = kstrtou32(buf, 0, &new_sample_interval);
    if (ret){
        printk("set neopebs_sampling_interval failed, ret: %d\n", ret);
        return ret;
    }

    pebs_sampling_period = new_sample_interval;
    return count;
}


static struct kobj_attribute neopebs_sampling_interval_attr =
	__ATTR(neopebs_sampling_interval, 0644, neopebs_sampling_interval_show,
	       neopebs_sampling_interval_store);


static ssize_t neopebs_config_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%x\n", pebs_config);
}

static ssize_t neopebs_config_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 new_config;

    ret = kstrtou32(buf, 0, &new_config);
    if (ret){
        printk("set neopebs_config failed, ret: %d\n", ret);
        return ret;
    }

    pebs_config = new_config;
    return count;
}


static struct kobj_attribute neopebs_config_attr =
	__ATTR(neopebs_config, 0644, neopebs_config_show,
	       neopebs_config_store);



static ssize_t neopebs_promotion_reenabled_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  neopebs_promotion_enabled ? "true" : "false");
}

static ssize_t neopebs_promotion_reenabled_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;

	ret = kstrtobool(buf, &neopebs_promotion_enabled);
    if (neopebs_promotion_enabled){
        wake_up_interruptible(&kneopebsd_wait);
        for (int i = 0; i < current_event_num; i++)
            enable_perf_events(i);
    }
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute neopebs_promotion_reenabled_attr =
	__ATTR(neopebs_reenabled, 0644, neopebs_promotion_reenabled_show,
	       neopebs_promotion_reenabled_store);



static ssize_t neopebs_create_event_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  neopebs_promotion_enabled ? "true" : "false");
}

static ssize_t neopebs_create_event_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;
    bool temp = false;

	ret = kstrtobool(buf, &temp);
    if (temp){
        if (current_event_num >= MAX_EVENT_NUM){
            printk("Error: current_event_num >= MAX_EVENT_NUM\n");
            return -ENOMEM;
        }
        create_perf_events(current_event_num);
        printk("create event num: %d, sample interval: %d, config: %x\n", current_event_num, pebs_sampling_period, pebs_config);
        current_event_num++;
    }
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute neopebs_create_event_attr =
	__ATTR(neopebs_create_event, 0644, neopebs_create_event_show,
	       neopebs_create_event_store);



static ssize_t neopebs_clear_all_events_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  neopebs_promotion_enabled ? "true" : "false");
}

static ssize_t neopebs_clear_all_events_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	ssize_t ret;
    bool temp = false;
    int cpu;

	ret = kstrtobool(buf, &temp);
    if (temp){
        for (int i = 0; i < current_event_num; i++){
            for_each_online_cpu(cpu){
                printk("cleanup event num: %d, cpu: %d, sample interval: %d, config: %x\n", i, cpu, event_array[i][cpu]->attr.sample_period, event_array[i][cpu]->attr.config);
                printk("event %d, cpu: %d, enabled time: %lld, running time: %lld\n", i, cpu, event_array[i][cpu]->total_time_enabled, event_array[i][cpu]->total_time_running);
            }
            cleanup_perf_events(i);
        }
        current_event_num = 0;
    }
	if (ret)
		return ret;

	return count;
}

static struct kobj_attribute neopebs_clear_all_events_attr =
	__ATTR(neopebs_clear_all_events, 0644, neopebs_clear_all_events_show,
	       neopebs_clear_all_events_store);



static ssize_t neopebs_migrate_interval_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", wait_time);
}

static ssize_t neopebs_migrate_interval_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 new_interval;

    ret = kstrtou32(buf, 0, &new_interval);
    if (ret){
        printk("set neopebs_migrate_interval failed, ret: %d\n", ret);
        return ret;
    }

    wait_time = new_interval;
    return count;
}


static struct kobj_attribute neopebs_migrate_interval_attr =
	__ATTR(neopebs_migrate_interval, 0644, neopebs_migrate_interval_show,
	       neopebs_migrate_interval_store);


static ssize_t neopebs_msr_write_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	u32 low, high;
    rdmsr(MSR_PEBS_LD_LAT_THRESHOLD, low, high);
    return sysfs_emit(buf, "0x%08x%08x\n", high, low);
}

static ssize_t neopebs_msr_write_store(struct kobject *kobj,
                       struct kobj_attribute *attr,
                       const char *buf, size_t count)
{
    ssize_t ret;
    u32 low, high;
    rdmsr(MSR_PEBS_LD_LAT_THRESHOLD, low, high);

    ret = kstrtou32(buf, 0, &low);
    if (ret){
        printk("set neopebs_msr_write_store failed, ret: %d\n", ret);
        return ret;
    }

    wrmsr(MSR_PEBS_LD_LAT_THRESHOLD, low, high);

    // rdmsr(MSR_PEBS_LD_LAT_THRESHOLD, low, high);
    // pr_info("MSR_PEBS_LD_LAT_THRESHOLD now: 0x%08x%08x\n", high, low);
    
    return count;
}

static struct kobj_attribute neopebs_msr_write_attr =
	__ATTR(neopebs_msr_write, 0644, neopebs_msr_write_show,
	       neopebs_msr_write_store);


static struct attribute *neopebs_attrs[] = {
	&neopebs_promotion_enabled_attr.attr,
    &neopebs_sampling_interval_attr.attr,
    &neopebs_config_attr.attr,
    &neopebs_promotion_reenabled_attr.attr,
    &neopebs_create_event_attr.attr,
    &neopebs_clear_all_events_attr.attr,
    &neopebs_migrate_interval_attr.attr,
    &neopebs_msr_write_attr.attr,
	NULL,
};

static const struct attribute_group neopebs_attr_group = {
	.attrs = neopebs_attrs,
};

static int __init neopebs_init_sysfs(void)
{
	int err;
	struct kobject *neopebs_kobj;

	neopebs_kobj = kobject_create_and_add("neopebs", mm_kobj);
	if (!neopebs_kobj) {
		pr_err("failed to create neopebs kobject\n");
		return -ENOMEM;
	}
	err = sysfs_create_group(neopebs_kobj, &neopebs_attr_group);
	if (err) {
		pr_err("failed to register neopebs group\n");
		goto delete_obj;
	}
	return 0;

delete_obj:
	kobject_put(neopebs_kobj);
	return err;
}
subsys_initcall(neopebs_init_sysfs);