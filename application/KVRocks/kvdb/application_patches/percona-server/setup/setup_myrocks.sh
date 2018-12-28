#!/bin/bash -xe
set +e
sudo service mysql stop
sleep 5
sudo umount /nvmessd

sudo service irqbalance stop

set -e
cd /lib/modules/4.9.5-040905-generic/kernel/drivers/nvme/host

sudo rmmod nvme
sudo rmmod nvme_core

sudo insmod nvme-core.ko
sudo insmod nvme.ko

sleep 5
#sudo cp /usr/lib/mysql/plugin/ha_rocksdb.so.ORIG /usr/lib/mysql/plugin/ha_rocksdb.so
sudo cp ~/bin/ha_rocksdb.so.ORIG /usr/lib/mysql/plugin/ha_rocksdb.so
sudo cp ~/conf/mysqld.cnf /etc/mysql/percona-server.conf.d/mysqld.cnf

## setup one block for data dir
#sudo nvme format /dev/nvme0n1 -n1 -s1
#sudo parted -s -a optimal /dev/nvme0n1 mklabel gpt
#sudo parted -s -a optimal /dev/nvme0n1 mkpart test 0G 100%
#sleep 5s
#sudo mkfs.ext4 -L media /dev/nvme0n1p1
#sudo mkfs.ext4 -f -L media /dev/nvme0n1p1

## set up one block for DB creation
sudo nvme format /dev/nvme1n1 -n1 -s0
sudo parted -s -a optimal /dev/nvme1n1 mklabel gpt
sudo parted -s -a optimal /dev/nvme1n1 mkpart test1 0G 100%
sleep 5s
sudo mkfs.ext4 -L media /dev/nvme1n1p1

set +e
sudo mkdir /nvmessd
sudo mkdir -p /var/run/mysqld
sudo chown mysql:mysql /var/run/mysqld

set -e
sudo chmod 0777 /nvmessd

sudo mount -t ext4 /dev/nvme1n1p1 /nvmessd

sudo rm -rf /nvmessd/mysql
sudo mysqld --initialize --init-file ~/conf/mysql_init.sql

## move system data to another block, and leave db data in /nvmessd/mysql
sudo /usr/sbin/mysqld --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf --basedir=/usr --datadir=/nvmessd/mysql --plugin-dir=/usr/lib/mysql/plugin --user=mysql --log-error=/var/log/mysql/error.log --pid-file=/var/run/mysqld/mysqld.pid --socket=/var/run/mysqld/mysqld.sock --daemonize=ON --port=3306
