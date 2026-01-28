insmod /root/linux_cache/dyn_numa/dyn_numa.ko
mkdir /sys/kernel/config/dyn_numa/node1
echo 0x2080000000 > /sys/kernel/config/dyn_numa/node1/phys_addr
echo 0x100000000 > /sys/kernel/config/dyn_numa/node1/size
echo 1 > /sys/kernel/config/dyn_numa/node1/nid
echo online > /sys/kernel/config/dyn_numa/node1/state
