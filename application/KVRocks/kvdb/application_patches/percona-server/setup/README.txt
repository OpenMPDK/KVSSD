Generation instructions for setup:

1). install percona mysql 

    a). sudo apt install mysql-common libjemalloc1 libaio1 libmecab2 libgoogle-perftools4 libgflags-dev libgoogle-perftools-dev libgoogle-glog0v5

    b). wget https://www.percona.com/downloads/Percona-Server-LATEST/Percona-Server-5.7.20-19/binary/debian/xenial/x86_64/Percona-Server-5.7.20-19-r119-xenial-x86_64-bundle.tar

    c). tar xf Percona-Server-5.7.20-19-r119-xenial-x86_64-bundle.tar

    d). Install MyRocks, set up admin user: root with password: root
        sudo apt-get install ./*.deb
    e). After installation, save /usr/lib/mysql/plugin/ha_rocksdb.so
        cp /usr/lib/mysql/plugin/ha_rocksdb.so ~/bin/ha_rocksdb.so.ORIG

2). Intial build setup
    Please reference README.txt in the parent directory

3. KvRocks/Myrocks related files
    a). Intial setup file
        mysql_init.sql

    b). start KvRocks
        setup_kvrocks.sh
    c). start MyRocks
        setup_myrocks.sh

4. Reference configuration files:
    a). kvmysqld.cnf for KvRocks
    b). mysqld.cnf for MyRocks
