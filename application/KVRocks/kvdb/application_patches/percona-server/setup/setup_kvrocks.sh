#!/bin/bash -xe
ulimit -n 65536
#ulimit -c unlimited
set +e
#stopmysql.sh
sudo service mysql stop
sleep 5
sudo umount /nvmessd

sudo service irqbalance stop

#set -e
cd ~/src.master/insdb/kv_kernel_driver/iosched_dd_kr_new/nvme_iosched_driver_new
sudo rmmod nvme
sudo rmmod nvme_core
sudo insmod nvme-core.ko
sudo insmod nvme.ko
sudo cp 99-kv-nvme-dev.rules /etc/udev/rules.d/

sleep 20 
sudo chmod 0666 /dev/nvme0n1

#sudo rm -rf /var/lib/mysql
sudo rm -f /var/log/mysql/error.log
sudo /usr/sbin/nvme format /dev/nvme0n1 -n1 -s1
sleep 5

#sudo nvme format /dev/nvme1n1 -n1 -s1
#sudo parted -s -a optimal /dev/nvme1n1 mklabel gpt
#sudo parted -s -a optimal /dev/nvme1n1 mkpart test 0G 100%
#sleep 5s
#sudo mkfs.ext4 -L media /dev/nvme1n1p1

set -e
sudo mount -t ext4 /dev/nvme1n1p1 /nvmessd
sudo rm -rf /nvmessd/mysql

sudo mkdir -p /var/run/mysqld
sudo chown mysql:mysql /var/run/mysqld

# for debug
#sudo cp ~/src.master/kv-percona-server.dbg/build/storage/rocksdb/ha_rocksdb.so /usr/lib/mysql/plugin/ha_rocksdb.so
sudo cp ~/src.master/kv-percona-server/build/storage/rocksdb/ha_rocksdb.so /usr/lib/mysql/plugin/ha_rocksdb.so
sudo cp ~/conf/kvmysqld.cnf /etc/mysql/percona-server.conf.d/mysqld.cnf

sudo mysqld --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf --initialize --init-file ~/conf/mysql_init.sql

## reset kv_proc
sudo sh -c "echo 0 > /proc/kv_proc"

#numactl --membind=0 --cpunodebind=0 sudo mysqld_safe --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf
sudo /usr/sbin/mysqld --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf --basedir=/usr --datadir=/nvmessd/mysql --plugin-dir=/usr/lib/mysql/plugin --user=mysql --log-error=/var/log/mysql/error.log --pid-file=/var/run/mysqld/mysqld.pid --socket=/var/run/mysqld/mysqld.sock --daemonize=ON --port=3306

# shutdown
# sudo mysqladmin -uroot -proot -S /var/run/mysqld/mysqld.sock shutdown
