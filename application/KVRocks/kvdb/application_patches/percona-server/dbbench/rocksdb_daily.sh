#!/bin/bash -xe
ulimit -c 100000000
set +e
sudo service mysql stop
sudo umount /nvmessd

## assumes that the rocksdb is build at ~/src/rocksdb
mkdir ~/src/rocksdb/build.perf29

set -e
cd /lib/modules/4.9.5-040905-generic/kernel/drivers/nvme/host

sudo rmmod nvme
sudo rmmod nvme_core

sudo insmod nvme-core.ko
sudo insmod nvme.ko

#set +e
#sudo nvme format /dev/nvme1n1 -n1 -s0
#sudo parted -s -a optimal /dev/nvme1n1 mklabel gpt
#sudo parted -s -a optimal /dev/nvme1n1 mkpart test1 0G 100%
#sleep 5
#sudo mkfs.ext4 -L media /dev/nvme1n1p1
sleep 5
sudo mount -t ext4 /dev/nvme1n1p1 /nvmessd

set -e
sudo rm -rf /nvmessd/rocksdb*
sudo mkdir /nvmessd/rocksdb
sudo chmod 777 /nvmessd/rocksdb

cd ~/bin
sleep 5
cp -f ~/src/rocksdb/db_bench ~/src/rocksdb/build.perf29/
python ./rocksdb_bench_daily.py --dbpath=/nvmessd/rocksdb --dbbench_path=~/src/rocksdb/build.perf29 --dbtype=rocksdb --out_dir rocksdb4k_perf29 --automatic #--runall
