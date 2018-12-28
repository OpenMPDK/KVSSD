How to run db_bench, TPCC and TPCH

Before you run anything, it's recommended to setup sar for system stats collection.
    sudo apt-get install sysstat

    # set ENABLED="true"
    sudo vi /etc/default/sysstat

1. Build KvRocks
    The following shows general steps taken to create a complete source directory for build,
    installation, and testing. If you believe you have grasped all essentials, you may run
    this scripts to setup and build automatically for you, and resolve any minor errors.

    Usage:
        setup_build.sh <branch name> <the full path for top source directory>

    Example:
        setup_build.sh stable4 ~/src.stable4_XXX

    a). First checkout code, and setup directories
        Please modify $SRCTOP before running this setup, $SRCTOP is the top directory
        that contains all source code. Here is a sample directory layout:
            boost_1_59_0/  insdb/  kvdb/  kv-percona-server/

        .../kvdb/patches/percona-server/setup/setup_build.sh <branch_name> <SRCTOP DIR>

    b). Build various components. Please adjust these items before build inside mk_kvdb.sh
        1). kvssd path
        2). kernel driver folder, please see example below
            for kernel 4.9.5, it is: 
            /lib/modules/4.9.5-040905-generic/kernel/drivers/nvme/host
            
            for kernel 4.4, it is: 
            /lib/modules/4.4.0-72-generic/kernel/drivers/nvme/host

        3). MySQL config file path
        You may also customize or skip any actions inside the scripts.
        
        4). Build folly, your mileage may vary depending on what's currently in your system.
            cd $SRCTOP/insdb/folly

            wget https://github.com/google/googletest/archive/release-1.8.0.tar.gz && \
                tar zxf release-1.8.0.tar.gz && \
                rm -f release-1.8.0.tar.gz && \
                cd googletest-release-1.8.0 && \
                cmake configure . && \
                make && \
                make install
            
            sudo apt-get install \
                nvme-cli \
                vim  \
                gdb git libboost-all-dev \
                libgoogle-glog0v5 libjemalloc1 \
                libsnappy-dev libdouble-conversion1v5 \
                \
                g++ \
                cmake \
                libboost-all-dev \
                libevent-dev \
                libdouble-conversion-dev \
                libgoogle-glog-dev \
                libgflags-dev \
                libiberty-dev \
                liblz4-dev \
                liblzma-dev \
                libsnappy-dev \
                make \
                zlib1g-dev \
                binutils-dev \
                libjemalloc-dev \
                libssl-dev \
                pkg-config

            mkdir _build && cd _build
            cmake configure ..
            make -j $(nproc)
            make install

        5). Build whole KvRocks, including KvRocks RocksDB compaptiable MySQL engine.
            These packages are recommended:

            sudo apt install mysql-common libjemalloc1 libaio1 libmecab2 libgoogle-perftools4 libgflags-dev libgoogle-perftools-dev libgoogle-glog0v5

            Again, please make kernel version or other minor adjustments inside the scripts.

            .../kvdb/patches/percona-server/setup/mk_kvdb.sh <BRANCH> $SRCTOP

            Please note different kernel driver locations below:
            a). for 4.9.5
                ${SRCTOP}/insdb/kv_kernel_driver/iosched_dd_kr_new/nvme_iosched_driver_new

            b). for 4.4
                ${SRCTOP}/insdb/kv_kernel_driver/iosched_dd_kr_new/nvme_iosched_driver_new_v4_4_0
         
    
2. Get db_bench location
    If you followed the instruction above, its location is:
    $SRCTOP/kvdb/build/

3. Get the KvRocks RocksDB compaptiable MySQL engine
    If you followed the instruction above, its location is:
    $SRCTOP/kv-percona-server/build/storage/rocksdb/ha_rocksdb.so

    And you need to replace /usr/lib/mysql/plugin/ha_rocksdb.so with it for KvRocks testing using
    Percona MySQL server. Please save the original for native MySQL testing.

4. Run KvRocks based Percona MySQL server
    This following will copy over mysql configuration file, and KvRocks plugin ha_rocksdb.so
    And clean up current MySQL DB, re-initialize MySQL DB, and set up initial database admin user root
    with password root.
    
    Please ajust and customize content inside based on your source code location and kernel version.
    Please note in order to run any KvRocks program it's important to identify correction version of
    KvRocks kernel driver, load it first and run a basic test.

    cd $SRCTOP/kvdb/patches/percona-server/setup/
    ./setup_kvrocks.sh

5. How to run KvRocks db_bench
    First set up PYTHONPATH for loading Python modules and PATH for locating programs. 
        export PYTHONPATH=${SRCTOP}/kvdb/patches/percona-server/dbbench:$PYTHONPATH
        export PATH=${SRCTOP}/kvdb/patches/percona-server/dbbench:${SRCTOP}/kvdb/patches/percona-server/dbbench/setup:${SRCTOP}/kvdb/patches/percona-server/tpcc:${SRCTOP}/kvdb/patches/percona-server/tpch:${SRCTOP}/kvdb/patches/percona-server/R

    After you have loaded correct kernel driver for KvRocks, run the following
    with ajustment for your environment, please have "--dbpath" setup with proper permission first.
    By using "--dryrun", it will only show you a series of commands it would run to collect db_bench
    results.

    Current the Python scripts assumes these, you may search "valuesize_keys_list" in the beginning
    to adjust. 
    1). run with 4million keys with 100B value
    2). run with 1million keys with 4KB value
    3). It uses 20% keys for table keycount, 
        Please modify "key_tiers_factor" for different key count.

    4). It uses 20% of (keys X value_size) to derive the value cache size
        Please modify "scales" with 1 represent 10% in main() for adjustment, you may have an array
        to multiple tier measurements, but be cautious on time.

        Please especially pay attention to the default setting formula by searching "KVROCKS_OPTIONS"
        for large workload, as the percentage based formula may not meet your requirements any more.

    5). It measures the following essential benchmarks for time savings, you may need to modify 
        "benchmark_options" for more extensive measurements.

        "fillseq",
        "readrandom",
        "readseq",
        "fillrandom",
        "updaterandom",
        "readrandomwriterandom",
        "readwhilewriting",

    There are many other options available for KvRocks, you can also adjust inside the script.

    6). Once you are done with configurations, you can start running db_bench as follows, please
        remove "--dryrun" option once you are happy. You can run multiple times to accumulate
        measurement results.

        cd $SRCTOP/kvdb/patches/percona-server/dbbench
        python ./rocksdb_bench_daily.py --dbpath=/nvmessd/kvdbt --dbbench_path=$SRCTOP/kvdb/build --dbtype=kvdb --out_dir <logdir> --automatic --dryrun

        If you change --dbtype to be rocksdb, then it will use the same configurations for rocksdb for
        performance comparison, except that the KvRocks specific options not applicable to RocksDB will
        be ignored. Original OS kernel driver is recommended.
    
    7). db_bench results in the log directory you set up above such as: 
        ~/log/dbbench/kvdb_msl-ads-dev21.fast/

        You will see many timestamped folders and a log directory.
            20181220_23_53_26/  20181221_00_01_47/  20181222_00_56_44/  log/
            20181220_23_57_40/  20181221_00_07_02/  20181222_01_02_01/

        These timestamped folders contains system stats information for one dbbench run.
            cd 20181222_01_02_01/

            cpu.log   kv_proc.log  mem_cpu_db_bench.log  smart_start1.log  smart_stop1.log
            disk.log  mem.log      smart_start0.log      smart_stop0.log

            cpu.log disk.log mem.log record system wide resource consumption data
            kv_proc.log records kv operation data
            mem_cpu_db_bench.log records each db_bench instance cpu and memory consumption data.

        Inside the log dirctory log/, you will see mutliple files such as:
            20181211-14-28-17.log  dbbench.csv  dbbench_header.csv
        
        The timestamped log file collects all output from a db_bench run, if there are any errors such
        as core dumps or crashes, it will be captured there for investigation and reproduction with
        complete command line. .

        dbbench.csv contains the collected latency and bandwidth data in tabular form. The headers are
        in dbbench_header.csv. Please be aware that if you added or deleted benchmarks for successive
        runs, it will contain different headers, it's not recommended as it can easily introduce errors
        in data interpretation..

        Sample tabular header, if failure column is 1, some crash happened, results should be ignored.

        time, numkeys_per_thread, stats_type, dbtype, value_size, threads, cache_size, runtype, combined, flush, failure, fillseq, readrandom, readseq, fillrandom, updaterandom, readrandomwriterandom, readwhilewriting, sktable_keycount_low, sktable_keycount_high

        Collect final performnce data:

        cd <logdir>/log
        cat dbbench_header.csv | sort -u >> db_bench_final.csv
        cat dbbench.csv >> db_bench_final.csv
          
6. How to setup Percona MySQL server using KvRocks RocksDB compatible storage engine
    1). Download and install Percona Server packages
        wget https://www.percona.com/downloads/Percona-Server-LATEST/Percona-Server-5.7.20-19/binary/debian/xenial/x86_64/Percona-Server-5.7.20-19-r119-xenial-x86_64-bundle.tar
        tar xf Percona-Server-5.7.20-19-r119-xenial-x86_64-bundle.tar
        sudo apt-get install ./*.deb
        
        Please follow screen prompt to set up admin user "root" with password "root" for default MySQL.
    
        Now you may run "sudo service mysql start/stop" to start or stop MySQL server, which listens on
        default port 3306. With this, you may refer to step 4 for KvRocks MySQL server setup.

    2). Run MySQL server in customized way
        Please refer to the sample scripts below:
        .../kvdb/patches/percona-server/setup/

            a). start MySQL using one device
                setup_myrocks_ali_1device.sh

            b). start MySQL using two devices
                setup_myrocks_ali_2device.sh
    
        Since KvRocks always uses 2 devices for running MySQL, in order to have fair comparison 2 devices 
        are recommended.
    

7. How to run TPCC
    You may want to first compile 
        .../kvdb/patches/percona-server/tpcc/nvme_smart_test.c to generate binary
        nvme_smart

    and put it in a local path that can be located directly..
    This binary will be invoked to collect device utilization information for reference. Please note the
    uitlization number is generated using
        $SRCTOP/kvdb/patches/percona-server/tpcc/nvme_smart_test.c

    This is experimental program, the report needs validation and fixes in case of errors.

    1). First start MySQL server after installation, please reference step 4 above.
    Assume you have set up MySQL server with admin user "root", password "root".
        
    2). Run vanilla TPCC without capturing logs
        cd $SRCTOP/kvdb/patches/percona-server/tpcc/
        ./run_tpcc.sh <mysql_server> <mysql_port> <warehouse#> <hours>

        This is helpful in one-off ad-hoc testing.

    3). Run TPCC in a batch
        This is especially helpful for stable code.
        .../kvdb/patches/percona-server/tpcc/

        ./run_tpcc_batch.sh

        Before you run, please adjust user id and destination server 10g IP first.

        After run, the results are located at ~/log/tpcc/

        For an example:
            log.msl-dmap-perf33_to_msl-ads-dev36.kvrocks_30hr_fwQ1127_mr24k_a4_kc25m_df20K_20181215_11_45_12
        It contains the MySQL configuration file that's used to run the MySQL server, and various system
        resource consumption stats, KV operation stats, and device utilization before and after the run.
        It also contains generated TPCC/KV operation graph for KvRocks.

        Sample graphs are: 
            tpcc_asyncop_mr24k_a4_kc25m_df20k.png
            tpcc_prefetch_mr24k_a4_kc25m_df20k.png
            tpcc_syncop_mr24k_a4_kc25m_df20k.png
            tpcc_syscpu_mem_mr24k_a4_kc25m_df20k.png
            tpcc_trx_mr24k_a4_kc25m_df20k.png

        You may directly view them using "eog tpcc_asyncop_mr24k_a4_kc25m_df20k.png"
        Or copy them to windows for viewing.

        The graph generation depends on R setup, which has been done on msl-dpe-perf63.

    4). When MySQL crashes while running TPCC, then monitoring processes need to be stopped
        You can use convenient routines to do that:
        a). stop all sar system stats collection processes
            ./stopsar.sh

        b). stop mysql completely
            ./stopmysql.sh

        Please be aware that if you don't clean up properly after crash, those monitoring
        processes will be running for a long time and unnecessarily generate huge amount of log.

        If there is no crash, everything is supposed to be cleaned up nicely after each run.

8. How to run TPCH
    1). First start MySQL server after installation, please reference step 4 above.
    Assume you have set up MySQL server with admin user "root", password "root".

    2). Run TPCH
        a). set up warehouse number, modify "target_db_databaseNum=1", 10 is a reason number
            to get started with 80+ million records.

            kvdb/patches/percona-server/tpch/test-tools/config.properties

        b). run tpch
            cd $SRCTOP/kvdb/patches/percona-server/tpch/
            ./run_tpch.sh <kvrocks | myrocks> <logdir>

        c). run tpch in batch mode
            Please see sample file: tpch_w10_kvrocks_daily.sh

    After it's finished, it will generate a csv file in the log directory with timestamp.
    The log files are located at: ~/log/tpch/

    Please follow the same cleanup guidelines as in TPCC.

9. Easiest thing to do
    log on to msl-dpe-perf63 as root, and su jim.li, most routines are already set up in ~/bin
    directory, and you can easily customize by creating a new copy with modifications.

10. List of machines:
    msl-dpe-perf63 is the default build machine.
    msl-ads-dev21 and msl-ads-dev23 are for 4.4 kernel (loaned from JooHwan)
    msl-dmap-perf29 and msl-dmap-perf28 are for mainly for dbbench runs or used as TPCC clients
    msl-ads-dev35 and msl-ads-dev36 are used to run MySQL server
    msl-dmap-perf31 (mostly used by Hobin) and msl-dmap-perf33 are mostly used as TPCC clients
    TPCH is typically run directly at the MySQL server.

11. Any improvements are welcome!
