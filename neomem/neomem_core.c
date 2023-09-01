#include <linux/list.h>
#include <linux/types.h>
#include "../drivers/neoprof/neoprof.h"
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/migrate.h>
#include <linux/mm_inline.h>
#include <linux/delay.h>

static struct list_head hp_entry;
static struct mutex kneomemd_lock;
static struct task_struct * kneomemd;

static unsigned long wait_time = 500000; // 500ms
static int FAST_NODE_ID = 0;
static u64 migration_cnt = 0;
static u64 scan_cnt = 0;
static u32 clear_interval = 10;
static u32 hotness_threshold = 2;

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
    printk("N Hotpages: %d", nr_hotpages);
	for(int i = 0; i < nr_hotpages; i++) {
		// the get_hotpage operation is distructive?
		u64 paddr = get_hotpage();
		hotpage_add(paddr);
	}
    return 0;   
}


static int neomem_migrate_pages(void)
{

	unsigned long pfn;
    struct hotpage *hp, *tmp;
    struct page *page, *head;

    // source is the list of pages to be migrated
    LIST_HEAD(source);
	list_for_each_entry_safe(hp, tmp, &hp_entry, list) {
        pfn = PHYS_PFN(hp->paddr);

        printk("PFN to migrate: %d",pfn);

        if (!pfn_valid(pfn))
		{
#ifdef DEBUG
			pr_err("pfn %lx is invalid\n", pfn);
#endif
			continue;
		}
        page = pfn_to_page(pfn);
        migrate_misplaced_page_no_vma(page, FAST_NODE_ID);
	}
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
        // perform promotion
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
	
	int err = -EBUSY;
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

int damon_stop(void)
{
	int i, err = 0;
	err = __neomem_stop();
	return err;
}

