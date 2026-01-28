# Dynamic NUMA Node Manager

## 简介
这是一个 Linux 内核模块，通过 Configfs 接口允许用户态动态创建 NUMA 节点并添加物理内存。

## 前置条件 (非常重要！)
你不能凭空“创造”物理内存。你必须告诉 Linux 内核在启动时**不要使用**某块物理内存，这样我们才能通过热插拔将其“添加”回来。

### 1. 修改 GRUB 启动参数
编辑 `/etc/default/grub`，在 `GRUB_CMDLINE_LINUX` 中添加 `memmap` 参数来预留内存。

格式: `memmap=<size>$<start_address>` (单位可以是 K, M, G)

**示例**: 预留 1GB 内存，起始地址为 4GB (0x100000000)
```bash
memmap=1G$4G
```
或者更具体的十六进制：
```bash
memmap=0x40000000$0x100000000
```

更新 Grub 并重启:
```bash
update-grub
reboot
```

### 2. 验证预留
重启后，检查 `/proc/iomem`，你应该能看到标记为 `Reserved` 的区域。
```bash
grep Reserved /proc/iomem
```

## 编译与安装
```bash
make
insmod dyn_numa.ko
```

## 使用方法

该模块挂载在 `/sys/kernel/config/dyn_numa/`。

### 创建节点
```bash
# 1. 创建配置目录
mkdir /sys/kernel/config/dyn_numa/node1

# 2. 设置参数 (对应你在 memmap 中预留的地址和大小)
# 注意：地址和大小必须与 memory block size 对齐 (通常是 128MB)
echo 0x2080000000 > /sys/kernel/config/dyn_numa/node1/phys_addr
echo 0x100000000 > /sys/kernel/config/dyn_numa/node1/size
echo 1 > /sys/kernel/config/dyn_numa/node1/nid

# 3. 上线内存 (触发 add_memory)
echo online > /sys/kernel/config/dyn_numa/node1/state
```

### 验证
```bash
numactl -H
cat /sys/devices/system/node/node1/meminfo
```

### 用户态 Online (如果是旧内核或模块未自动 Online)
如果内存添加成功但不可用，可能需要手动 online 内存块：
```bash
for mem in /sys/devices/system/memory/memory*; do
  state=$(cat $mem/state)
  if [ "$state" == "offline" ]; then
      echo online > $mem/state
  fi
done
```

### 销毁节点
```bash
echo offline > /sys/kernel/config/dyn_numa/node1/state
rmdir /sys/kernel/config/dyn_numa/node1
```
