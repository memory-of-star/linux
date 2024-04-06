#include "heterbox.h"

static int heterbox_strategy_start(void)
{
	int err;
	err = heterbox_start();
	return 0;
}


static int __init heterbox_strategy_init(void)
{
    int err;
    pr_debug("kheterboxd heterbox_strategy_init\n");

    err = heterbox_strategy_start();
    return err;
}
late_initcall(heterbox_strategy_init);