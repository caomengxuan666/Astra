#!/bin/bash

# Astra Cluster测试脚本

echo "启动Astra集群测试..."

# 启动第一个节点
echo "启动节点1: 端口6380，集群端口16380"
start "Astra Node 1" Astra-CacheServer.exe --cluster 1 --cluster-port 16380 -p 6380 -l info &

# 等待节点启动
sleep 2

# 启动第二个节点
echo "启动节点2: 端口6381，集群端口16381"
start "Astra Node 2" Astra-CacheServer.exe --cluster 1 --cluster-port 16381 -p 6381 -l info &

# 等待节点启动
sleep 2

# 启动第三个节点
echo "启动节点3: 端口6382，集群端口16382"
start "Astra Node 3" Astra-CacheServer.exe --cluster 1 --cluster-port 16382 -p 6382 -l info &

# 等待节点启动
sleep 2

echo "所有节点已启动，开始配置集群..."

# 在节点6380上执行集群配置
echo "在节点6380上执行集群配置..."
redis-cli -p 6380 CLUSTER MEET 127.0.0.1 6381
redis-cli -p 6380 CLUSTER MEET 127.0.0.1 6382

# 等待节点相互发现
sleep 1

# 为每个节点分配槽位
echo "为节点分配槽位..."
redis-cli -p 6380 CLUSTER ADDSLOTS 0-5460
redis-cli -p 6381 CLUSTER ADDSLOTS 5461-10922
redis-cli -p 6382 CLUSTER ADDSLOTS 10923-16383

# 检查集群状态
echo "检查集群状态..."
echo "节点信息:"
redis-cli -p 6380 CLUSTER NODES

echo "槽位分配信息:"
redis-cli -p 6380 CLUSTER SLOTS

echo "开始测试键值存储和重定向..."

# 测试键值存储
echo "存储测试键值..."
redis-cli -c -p 6380 SET key1 "value1"
redis-cli -c -p 6380 SET key2 "value2"
redis-cli -c -p 6380 SET key3 "value3"
redis-cli -c -p 6380 SET key4 "value4"

# 测试键值获取
echo "获取测试键值..."
redis-cli -c -p 6380 GET key1
redis-cli -c -p 6380 GET key2
redis-cli -c -p 6380 GET key3
redis-cli -c -p 6380 GET key4

# 检查键分布
echo "检查各节点键分布..."
echo "节点6380上的键:"
redis-cli -c -p 6380 KEYS *

echo "节点6381上的键:"
redis-cli -c -p 6381 KEYS *

echo "节点6382上的键:"
redis-cli -c -p 6382 KEYS *

echo "集群测试完成！"