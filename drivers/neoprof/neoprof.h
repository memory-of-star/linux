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

/* 
 * State Monitor
 */

void set_state_sample_interval(u32 interval);

// Read out the number of total sampled CXL read or write
u32 get_total_state_sample_cnt(void);

// Read out the number of sampled CXL write
u32 get_wr_state_sample_cnt(void);

// Read out the number of sampled CXL read
u32 get_rd_state_sample_cnt(void);

#endif 