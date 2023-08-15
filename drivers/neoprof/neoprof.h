#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/*
    * The following functions are used to access the neoprof device
*/
u64 get_nr_hotpages(void);

u64 * get_hotpages(void);

u64 get_hotness_threshold(void);

void set_hotness_threshold(u64 threshold);
