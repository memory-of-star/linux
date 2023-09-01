#ifndef __NEOPROF_H__
#define __NEOPROF_H__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/*
    * The following functions are used to access the neoprof device
*/
u32 get_nr_hotpages(void);

u64 get_hotpage(void);

void set_hotness_threshold(unsigned int  threshold);

void reset_neoprof(void);

#endif 