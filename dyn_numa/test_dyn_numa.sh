#!/bin/bash

# Check if module is loaded
if ! lsmod | grep -q "dyn_numa"; then
    echo "Loading module..."
    insmod dyn_numa.ko || exit 1
fi

# Mount configfs if not mounted
if ! mount | grep -q "configfs"; then
    echo "Mounting configfs..."
    mount -t configfs none /sys/kernel/config
fi

# Create a new dynamic node
echo "Creating dyn_node_1..."
mkdir -p /sys/kernel/config/dyn_numa/dyn_node_1

# Configure the node
# NOTE: You must ensure this physical address range is reserved (e.g., via memmap=2G$6G boot param)
# and is not being used by the kernel.
# 0x200000000 = 8GB (Example address)
# WARNING: Check /proc/iomem to ensure this address is not 'System RAM' or 'Soft Reserved'
PHYS_ADDR=0x200000000
SIZE=0x40000000 # 1GB
# NOTE: Must use a valid node ID (check /sys/devices/system/node/possible)
NODE_ID=1

echo "Setting physical address to $PHYS_ADDR..."
echo $PHYS_ADDR > /sys/kernel/config/dyn_numa/dyn_node_1/phys_addr

echo "Setting size to $SIZE..."
echo $SIZE > /sys/kernel/config/dyn_numa/dyn_node_1/size

echo "Setting NUMA node ID to $NODE_ID..."
echo $NODE_ID > /sys/kernel/config/dyn_numa/dyn_node_1/nid

# Bring it online (This calls add_memory)
echo "Adding memory to kernel (add_memory)..."
if echo online > /sys/kernel/config/dyn_numa/dyn_node_1/state; then
    echo "Success! Memory added."
else
    echo "Failed to add memory. Check dmesg for details."
    exit 1
fi

# Now we need to ensure the memory is in ZONE_MOVABLE.
# 1. Find the memory blocks corresponding to our physical address range.
# Block size is usually 128MB or 2GB.
BLOCK_SIZE_HEX=$(cat /sys/devices/system/memory/block_size_bytes | xargs printf "0x%x")
BLOCK_SIZE=$((BLOCK_SIZE_HEX))

START_ADDR=$((PHYS_ADDR))
END_ADDR=$((PHYS_ADDR + SIZE))

echo "Scanning for new memory blocks from $PHYS_ADDR to $END_ADDR..."

for ((addr=$START_ADDR; addr<$END_ADDR; addr+=$BLOCK_SIZE)); do
    # Calculate block index
    BLOCK_INDEX=$((addr / BLOCK_SIZE))
    MEM_PATH="/sys/devices/system/memory/memory$BLOCK_INDEX"
    
    if [ -d "$MEM_PATH" ]; then
        STATE=$(cat $MEM_PATH/state)
        echo "Block $BLOCK_INDEX ($MEM_PATH) is currently: $STATE"
        
        # If it's offline, online it as movable
        if [ "$STATE" == "offline" ]; then
            echo "  -> Onlining as movable..."
            echo online_movable > $MEM_PATH/state
        elif [ "$STATE" == "online" ]; then
            # If it's already online (ZONE_NORMAL), we try to offline and re-online as movable.
            # This might fail if kernel already put unmovable data there, but since we just added it, chances are good.
            echo "  -> Warning: Block was auto-onlined to ZONE_NORMAL. Attempting to fix..."
            echo offline > $MEM_PATH/state
            if [ $? -eq 0 ]; then
                echo online_movable > $MEM_PATH/state
                echo "  -> Fixed: Now online_movable."
            else
                echo "  -> FAILED to offline. This block is stuck in ZONE_NORMAL and cannot be reliably removed later."
            fi
        fi
    fi
done

# Show status
cat /sys/kernel/config/dyn_numa/dyn_node_1/state

# Check dmesg
dmesg | tail -n 5
