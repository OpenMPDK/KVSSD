#!/bin/bash -xe
set +e
sudo service mysql stop
sleep 5
sudo umount /nvmessd
sudo umount /nvmessd_sys
sudo rm -rf /nvmessd_sys/mysql

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
sudo cp ~/conf/mysqld.cnf.ALI /etc/mysql/percona-server.conf.d/mysqld.cnf

## setup one block for data dir
sudo nvme format /dev/nvme0n1 -n1 -s1
sudo parted -s -a optimal /dev/nvme0n1 mklabel gpt
sudo parted -s -a optimal /dev/nvme0n1 mkpart test 0G 100%
sleep 5s
sudo mkfs.ext4 -L media /dev/nvme0n1p1

## set up one block for DB creation
sudo nvme format /dev/nvme1n1 -n1 -s1
sudo parted -s -a optimal /dev/nvme1n1 mklabel gpt
sudo parted -s -a optimal /dev/nvme1n1 mkpart test1 0G 100%
sleep 5s
sudo mkfs.ext4 -L media /dev/nvme1n1p1

set +e
sudo mkdir /nvmessd
sudo mkdir /nvmessd_sys
sudo mkdir -p /var/run/mysqld

set -e
sudo chmod 0777 /nvmessd_sys
sudo chmod 0777 /nvmessd

sudo mount -t ext4 /dev/nvme1n1p1 /nvmessd
sudo mount -t ext4 /dev/nvme0n1p1 /nvmessd_sys

sudo mkdir /nvmessd_sys/mysql
sudo chown mysql:mysql /nvmessd_sys/mysql

sudo chown mysql:mysql /var/run/mysqld


sudo rm -rf /nvmessd/mysql
sudo mysqld --initialize --init-file ~/conf/mysql_init.sql

## utilize two ssd for fair comparison with kvssd
sudo mv /nvmessd/mysql/.rocksdb /nvmessd_sys/mysql/
sudo ln -s /nvmessd_sys/mysql/.rocksdb /nvmessd/mysql/

## move system data to another block, and leave db data in /nvmessd/mysql

# sudo service mysql start
sudo /usr/sbin/mysqld --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf --basedir=/usr --datadir=/nvmessd/mysql --plugin-dir=/usr/lib/mysql/plugin --user=mysql --log-error=/var/log/mysql/error.log --pid-file=/var/run/mysqld/mysqld.pid --socket=/var/run/mysqld/mysqld.sock --daemonize=ON --port=3306

##    sudo mysqld --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf --initialize --init-file ~/conf/mysql_init.sql
## or sudo mysqld_safe --defaults-file=/etc/mysql/percona-server.conf.d/mysqld.cnf
##    sudo mysqladmin -uroot -proot -P3306 shutdown
##    sudo mysqladmin -uroot -proot -P3307 shutdown

# sudo mysql -uroot -proot  -e "GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY 'root' WITH GRANT OPTION;"

# for perf29 / perf28
# sudo parted -s -a optimal /dev/nvme2n1 mklabel gpt
# sudo parted -s -a optimal /dev/nvme2n1 mkpart testssd 0G 100%
# sudo mkfs.ext4 -L media /dev/nvme2n1p1
# sudo mount -t ext4 /dev/nvme2n1p1 /nvmessd
# sudo chmod 777 /nvmessd
