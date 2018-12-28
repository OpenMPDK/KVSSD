#!/bin/bash -xe
if [ "$#" -ne 1 ]
then
    echo "invalid # of argument"
    echo "use $0 <root of src>"
    exit 1
fi
SRCTOP=`realpath $1`

ulimit -n 65536
set +e
sudo service mysql stop
sudo umount /nvmessd
set -e

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

cd ${SRCTOP}/insdb/snappy/google-snappy-b02bfa7
set +e
mkdir build
cd build
cmake ..
make -j $(nproc)
set -e

cd ${SRCTOP}/insdb/insdb/insdb-master
make clean
make -j $(nproc)

sudo nvme format /dev/nvme0n1 -n1 -s0
#sudo nvme format /dev/nvme1n1 -n1 -s0
out-static/c_test 
out-static/db_test

## kvdb
cd ${SRCTOP}/kvdb/gflags-v2.2.1
set +e
mkdir build
cd build
cmake ..
make

cd ${SRCTOP}/kvdb
set +e
mkdir build
cd build
make clean
set -e
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo  -DDOWNLOAD_BOOST=1 \
         -DWITH_BOOST=../../..  -DCMAKE_INSTALL_PREFIX=~/install/kvdb
make -j $(nproc)

# for debug
#cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo  -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../..  -DCMAKE_INSTALL_PREFIX=~/install/kvdb -DKVDB_ENABLE_IOTRACE=1
#cmake ..  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DINSDB_ROOT_DIR=../../src/insdb -DKVDB_ROOT_DIR=../../src/kvdb.dbg  -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../..  -DENABLE_DOWNLOADS=1 -DCMAKE_INSTALL_PREFIX=~/install/myrocks.kv

# git clone git@msl-dc-gitlab.ssi.samsung.com:kvssd/kv-percona-server.git
# git checkout Percona-Server-5.7.20-19-branch
# only need to do it once
###########################
# cd ${SRCTOP}/kv-percona-server
# set +e
# mkdir build
# cd build
# make clean
# cmake ..  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DINSDB_ROOT_DIR=../../${SRCTOP}/insdb -DKVDB_ROOT_DIR=../../${SRCTOP}/kvdb  -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../..  -DENABLE_DOWNLOADS=1 -DCMAKE_INSTALL_PREFIX=~/install/myrocks.kv
# 
# make -j $(nproc)

set -e
cd ${SRCTOP}/kv-percona-server/build/storage/rocksdb
make clean
make -j $(nproc)
sudo cp ~/conf/kvmysqld.cnf.ALI /etc/mysql/percona-server.conf.d/mysqld.cnf
sudo cp ${SRCTOP}/kv-percona-server/build/storage/rocksdb/ha_rocksdb.so /usr/lib/mysql/plugin/ha_rocksdb.so

#sudo nvme format /dev/nvme1n1 -n1 -s0
#sudo parted -s -a optimal /dev/nvme1n1 mklabel gpt
#sudo parted -s -a optimal /dev/nvme1n1 mkpart test 0G 100%
##sudo mkfs.ext4 -L media /dev/nvme1n1p1
#sudo mkfs.ext4 -f -L media /dev/nvme1n1p1
#sudo mount -t ext4 /dev/nvme1n1p1 /nvmessd
#
##sudo rm -rf /var/lib/mysql
#sudo rm -f /var/log/mysql/error.log
#sudo mysqld --initialize --init-file ~/conf/mysql_init.sql
#sudo service mysql start
