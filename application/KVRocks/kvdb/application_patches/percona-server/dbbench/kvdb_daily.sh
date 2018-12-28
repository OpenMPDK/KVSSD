#!/bin/bash -xe
if [ "$#" -ne 2 ]
then
    echo "invalid # of argument"
    echo "use $0 <root of src> <log_dir>"
    exit 1
fi

export PYTHONPATH=~/python:$PYTHONPATH
CLIENT=`hostname`

SRCTOP=`realpath $1`
LOGBASE=`realpath $2`
LOGDIR="$LOGBASE/kvdb_${CLIENT}.fast"

ulimit -c 100000000
set +e
sudo service mysql stop
sudo mkdir /nvmessd
sudo umount /nvmessd

cd /tmp/
# kill any collection
~/bin/sar_smart_stop.sh

#SRCTOP=~/src.stable4
SRCBINARY=${SRCTOP}/kvdb/build/db_bench
# temporary
#SRCBINARY=~/conf/bin/db_bench
BINDIR=${SRCTOP}/kvdb/build.perffast

ts=`date +"%Y%m%d_%H_%M_%S"`

mkdir -p ${BINDIR}
mkdir -p ${LOGDIR}
mkdir -p ${LOGDIR}/${ts}

# get kernel version
kernel=`uname -a |awk '{print $3}' | awk -F "." '{printf "%s.%s", $1,$2}'`
if [[ "${kernel}" == "4.9" ]]
then
    cd ${SRCTOP}/insdb/kv_kernel_driver/iosched_dd_kr_new/nvme_iosched_driver_new
    make clean
    make -j $(nproc)

    set +e
    sudo rmmod nvme
    sudo rmmod nvme_core

    set -e
    sudo insmod nvme-core.ko
    sudo insmod nvme.ko
elif [[ "${kernel}" == "4.4" ]]
then
    cd ${SRCTOP}/insdb/kv_kernel_driver/iosched_dd_kr_new/nvme_iosched_driver_new_v4_4_0

    make clean
    make -j $(nproc)
    set -e
    sudo ./re_insmod.sh
else
    echo "not supported kernel version: $kernel"
    exit 1
fi

sudo cp 99-kv-nvme-dev.rules /etc/udev/rules.d/
sleep 20
sudo chmod 0666 /dev/nvme0n1

sudo /usr/sbin/nvme format /dev/nvme0n1 -n1 -s1
cd ${SRCTOP}/insdb/insdb/insdb-master
out-static/c_test

set -e
sudo nvme format /dev/nvme1n1 -n1 -s1
sudo parted -s -a optimal /dev/nvme1n1 mklabel gpt
sudo parted -s -a optimal /dev/nvme1n1 mkpart test1 0G 100%
sleep 5
sudo mkfs.ext4 -L media /dev/nvme1n1p1
sleep 5
sudo mount -t ext4 /dev/nvme1n1p1 /nvmessd

set -e
sudo rm -rf /nvmessd/kvdbt*
sudo mkdir /nvmessd/kvdbt
sudo chmod 777 /nvmessd/kvdbt

cd ~/bin
sleep 5
cp -f ${SRCBINARY} ${BINDIR}

## for limited memory test 9GB for 1million of 4K
sudo sync
sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"
sudo swapoff -a
#cd ~/Downloads/vdisk/
#./mount_vdisk.sh 111

# start stats collection
cd ${LOGDIR}/${ts}
~/bin/sar_smart_bench.sh kvrocks 36000 db_bench

set +e
python ~/bin/rocksdb_bench_daily_fast.py --dbpath=/nvmessd/kvdbt --dbbench_path=${BINDIR} --dbtype=kvdb --out_dir ${LOGDIR} --kvssd /dev/nvme0n1 --automatic

# stop stats collection
~/bin/sar_smart_stop.sh

#cd ~/Downloads/vdisk/
#./umount_vdisk.sh
sudo swapon
