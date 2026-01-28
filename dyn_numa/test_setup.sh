#!/bin/bash

# 示例脚本：尝试创建一个 NUMA 节点
# 请确保 PHYS_ADDR 和 SIZE 是你在 GRUB 中预留的 (memmap=...)

# 配置部分
NODE_NAME="test_node"
# 下面这些值必须根据你的 dmesg/iomem 实际预留情况修改！
# 假设预留了 1GB at 4GB
PHYS_ADDR="0x2080000000" 
SIZE="0x100000000"       # 1GB
NID=1

CONFIGFS_ROOT="/sys/kernel/config/dyn_numa"

if [ ! -d "$CONFIGFS_ROOT" ]; then
    echo "Error: Module not loaded? $CONFIGFS_ROOT not found."
    exit 1
fi

echo "Creating node config directory..."
mkdir -p "$CONFIGFS_ROOT/$NODE_NAME"

echo "Setting Physical Address to $PHYS_ADDR..."
echo "$PHYS_ADDR" > "$CONFIGFS_ROOT/$NODE_NAME/phys_addr"

echo "Setting Size to $SIZE..."
echo "$SIZE" > "$CONFIGFS_ROOT/$NODE_NAME/size"

echo "Setting NUMA Node ID to $NID..."
echo "$NID" > "$CONFIGFS_ROOT/$NODE_NAME/nid"

echo "Enabling Node (Hotplug Add)..."
if echo "offline" > "$CONFIGFS_ROOT/$NODE_NAME/state"; then
    echo "Success! Memory added."
    echo "Check dmesg for details."
else
    echo "Failed to online node. Check dmesg."
    dmesg | tail -n 10
    exit 1
fi




