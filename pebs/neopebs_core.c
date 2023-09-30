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

#define PAGE_NUMBER (0x247fffffff >> 12)

#define CPU_NUMBER 256

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
struct perf_event *event_array[CPU_NUMBER];

static unsigned long migrated_pages = 0;
static int iter = 0;
static DECLARE_WAIT_QUEUE_HEAD(kneopebsd_wait);

static unsigned long wait_time = 100000; // 100ms

static unsigned long long total_sample_cnt = 0;
static unsigned long long total_sample_failed_cnt = 0;

// for neoprof-base promotion
static bool neopebs_promotion_enabled = false;

//pebs_sampling_period: how many instructions it takes to overflow
static unsigned long pebs_sampling_period = 500;


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

unsigned long get_pfn(struct mm_struct *mm, unsigned long vaddr) {
    struct page *pages[1];
    unsigned long pfn = 0;
    int ret;

    static int debug = 0;

    ret = get_user_pages_remote(mm, vaddr, 1, 0, pages, NULL, NULL);
    if (ret != 1) {
        if (!debug){
            printk("get_user_pages_remote failed, ret: %d\n", ret);
            debug = 1;
        }
        return 0; 
    }

    pfn = page_to_pfn(pages[0]);

    put_page(pages[0]);

    return pfn;
}

void neopebs_overflow_callback(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs) {
    u8 *page_access = NULL;

    // struct task_struct *curr_task = current;
    // struct mm_struct *mm = NULL;
    u64 pfn, phys_addr, addr;

    static int debug1 = 0;
    static int debug2 = 0;

    // if (data->addr) {
    //     unsigned long pfn;

    //     if (curr_task) {
    //         mm = curr_task->mm;
    //         if (mm) {
    //             pfn = get_pfn(mm, data->addr);
    //             if (pfn) {

    //                 if (!debug){
    //                     printk("get_pfn success, pfn: %ld\n", pfn);
    //                     debug = 1;
    //                 }

    //                 if (pfn > PAGE_NUMBER){
    //                     printk("pfn > PAGE_NUMBER, %ld\n", pfn);
    //                     return ;
    //                 }

    //                 page_access = page_access_times + pfn;
    //                 *page_access = *page_access + 1;

    //                 DEBUG_COUNTER(total_sample_cnt, 100000)
    //             }
    //         }
    //     }
    // }

    phys_addr = data->phys_addr;
    addr = data->addr;
        
    pfn = (phys_addr >> 12);

    printk("phys_addr: %lld, pfn: %lld", phys_addr, pfn);

    if (pfn > PAGE_NUMBER){
        if (debug1 >= 500){
            printk("pfn > PAGE_NUMBER for 500 times, pfn: %lld, data->phys_addr: %p, data->addr: %p\n", pfn, phys_addr, addr);
            debug1 = 0;
        }
        debug1 += 1;
    }
    else{
        if (debug2 >= 500){
            printk("pfn <= PAGE_NUMBER for 500 times, pfn: %lld, data->phys_addr: %p, data->addr: %p\n", pfn, phys_addr, addr);
            debug2 = 0;
        }
        debug2 += 1;
    }
    
}

int create_perf_events(void) {
    int cpu;
    struct perf_event *event;
    struct perf_event_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_RAW; 
    attr.config = 0x82d0;  // MEM_INST_RETIRED.ALL_STORES
    attr.sample_period = pebs_sampling_period;

    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_WEIGHT | PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR;
    attr.disabled = 0;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_callchain_kernel = 1;
    attr.exclude_callchain_user = 1;
    attr.precise_ip = 1;

    for_each_online_cpu(cpu) {
        event = perf_event_create_kernel_counter(&attr, cpu, NULL, 
                                                 neopebs_overflow_callback, NULL);
        if (IS_ERR(event)) {
            pr_info("Failed to create perf event for CPU %d\n", cpu);
            return PTR_ERR(event);
        }

        event_array[cpu] = event;

        perf_event_enable(event);
        printk("perf_event_enable, cpu: %d\n", cpu);
    }

    return 0;
}

void cleanup_perf_events(void) {
    int cpu;
    for_each_online_cpu(cpu) {
        if (event_array[cpu]) {
            perf_event_disable(event_array[cpu]);
            perf_event_release_kernel(event_array[cpu]);
            event_array[cpu] = NULL;
        }
    }
}

void enable_perf_events(void) {
    int cpu;
    for_each_online_cpu(cpu) {
        if (event_array[cpu]) {
            perf_event_enable(event_array[cpu]);
            printk("perf_event_enable, cpu: %d\n", cpu);
        }
    }
}

void disable_perf_events(void) {
    int cpu;
    for_each_online_cpu(cpu) {
        if (event_array[cpu]) {
            perf_event_disable(event_array[cpu]);
            printk("perf_event_disable, cpu: %d\n", cpu);
        }
    }
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kneopebsd_fn(void * data)
{
    pr_info("kneopebsd starts\n");

    create_perf_events();

    while (!kneopebsd_need_stop()) {
        // control the scan frequency
        kneopebs_usleep(wait_time);

        // perform promotion
        if(!neopebs_promotion_enabled){
            disable_perf_events();
            wait_event_interruptible(kneopebsd_wait, neopebs_promotion_enabled);
            enable_perf_events();
        }
    }

    cleanup_perf_events();

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

static struct attribute *neopebs_attrs[] = {
	&neopebs_promotion_enabled_attr.attr,
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