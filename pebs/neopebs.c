#include "neopebs.h"

static int neo_pebs_turn(void)
{
	int err;
	err = neo_pebs_start();
	return 0;
}


static int __init neo_pebs_init(void)
{
    int err;
    pr_debug("neo_pebs_init\n");

    err = neo_pebs_turn();
    return err;
}
late_initcall(neo_pebs_init);

