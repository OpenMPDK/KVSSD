#!/usr/bin/python
import os
import math
import shutil
import sys
import re
import command
import time
from string import Template
import argparse
import time
import copy
import json
import pprint
#import configparser

command.dryrun = 0

#################################################
######## these are only for automatic runs
## a set of predefined test specs
thread_list = [8]
#thread_list = [1, 4]

### for fixed cache list
#cachesize_list = [500*1024*1024, 2*1024*1024*1024, 4*1024*1024*1024]
valuesize_keys_list = [
#    {
#        "value_size" : 100,
#        "numkeys" : 40000000,
#    },

    {
        "value_size" : 4096,
        "numkeys" : 5000000,
    },

    {
        "value_size" : 1024,
        "numkeys" : 5000000,
    },

    {
        "value_size" : 2048,
        "numkeys" : 5000000,
    },


#    {
#        "value_size" : 102400,
#        "numkeys" : 50000,
#        #"numkeys" : 50,
#    },
]
#################################################

class Runspecs:
    def __init__(self, value_size, numkeys, cache_size, threads, \
            sktable_keycount_low, sktable_keycount_high):
        self.value_size = value_size 
        self.numkeys = numkeys
        self.cache_size = cache_size
        self.threads = threads
        self.sktable_keycount_low = sktable_keycount_low
        self.sktable_keycount_high = sktable_keycount_high

##################################################
## XXX how cache is estimated for test
# return a list of cache sizes, low#, medium#, high#
def get_cache_sizes(value_size, numkeys, scales):
    #########################
    ## estimate all cache tier
    # full_cachesize_estimate = (value_size + 1024) * numkeys
    full_cachesize_estimate = (value_size) * numkeys

    tenthcache = int(math.ceil(full_cachesize_estimate/1024/1024/10))

    print("value_size " + str(value_size) \
        + " numkeys " + str(numkeys) \
        + "\n low cache size are: " + str(tenthcache) + "MB" \
        + "\n medium cache size are: " + str(5*tenthcache) + "MB" \
        + "\n high cache size are: " + str(10*tenthcache) + "MB")

    cachesize_list = []

    for i in scales:
        cachesize_list.append(i*tenthcache*1024*1024)

    return cachesize_list

def get_runspecs_with_fixed_cache(cachesize_list, thread_list, valuesize_keys_list):
    runspecs = []
    for valuesize_keys in valuesize_keys_list:
        for cachesize in cachesize_list:
            for threads in thread_list:
                numkeys = valuesize_keys["numkeys"]
                key_tiers_factor = 1
                sktable_keycount_low = numkeys * key_tiers_factor
                sktable_keycount_high = numkeys * (key_tiers_factor + 0.1)
                runspecs.append( Runspecs(
                            valuesize_keys["value_size"], \
                            valuesize_keys["numkeys"], \
                            cachesize, \
                            threads, \
                            sktable_keycount_low, \
                            sktable_keycount_high \
                            ))
    return runspecs


#############
# for automatic runs
# scales is a list of [0, 10]
def get_runspecs_with_dynamic_cache(options, thread_list, valuesize_keys_list, scales):
    runspecs = []
    for valuesize_keys in valuesize_keys_list:
        # dynamically generate cache sizes
        cachesize_list = get_cache_sizes(valuesize_keys["value_size"], valuesize_keys["numkeys"], scales)
        for cachesize in cachesize_list:
            for threads in thread_list:
                ## number of keys, the sktable count is the factor + 0.1
                numkeys = valuesize_keys["numkeys"]

                ## adjustable for kvdb XXX
                ## for kvdb only
                # key_tiers_factor = [1, 0.6]
                # XXX affect # of runs
                key_tiers_factor = [0.5]
                if options.dbtype == "rocksdb":
                    key_tiers_factor = [1]

                for sktable_keycount_factor in key_tiers_factor:
                    sktable_keycount_low = int(numkeys * sktable_keycount_factor)
                    sktable_keycount_high = int(numkeys * (sktable_keycount_factor + 0.1))
                    runspecs.append( Runspecs(
                            valuesize_keys["value_size"], \
                            valuesize_keys["numkeys"], \
                            cachesize, \
                            threads, \
                            sktable_keycount_low, \
                            sktable_keycount_high \
                            ))
    return runspecs

## how the options are updated from spec
## generate a different DB path file
def update_option(options, onespecs, i):
    options.value_size = onespecs.value_size
    options.numkeys = onespecs.numkeys
    options.cache_size = onespecs.cache_size
    options.threads = onespecs.threads
    options.dbpath = options.dbpathsave + "/" + options.dbtype + str(i%400)

    ## only applicable for kvdb
    if options.dbtype == "kvdb":
        options.sktable_keycount_low = onespecs.sktable_keycount_low
        options.sktable_keycount_high = onespecs.sktable_keycount_high
    else:
        options.sktable_keycount_low = 0
        options.sktable_keycount_high = 0

# from command line, then change values from above
def get_options():

    # skip for bandwidth
    operations_to_skip = [
    #    "readrandom",
    #    "seekrandom",
    #    "readwhilewriting",
        "compact",
    ]

    benchmark_options = [
#        "fillbatch",
        "fillseq", 
        "readrandom",
        "readseq",
        "fillrandom",
        "updaterandom",
        "readrandomwriterandom",
        "readwhilewriting",
#        "randomtransaction",
#        "fill100K",
#        "updaterandom",
#        "appendrandom",
#        "multireadrandom",
        ]

    dbtypes = [
            "insdb",
            "leveldb",
            "rocksdb",
            "kvdb",
            ]

    parser = argparse.ArgumentParser()


    parser.add_argument('--automatic', action='store_true', dest='automatic', default=False,
                        help='automatically generate value_size, cache_size, threads using predefined values, command line input for these settings will be ignored.')

    parser.add_argument('--dbpath', action='store', dest='dbpath', default='/tmp',
                        help='database path')
 
    parser.add_argument('--kvssd', action='store', dest='kvssd', default='/dev/nvme0n1',
                        help='kvssd path')
 
    parser.add_argument('--dbtype', action='store', dest='dbtype', required=True,
            help='please specify dbtype: insdb/leveldb/rocksdb/kvdb ')
 
    parser.add_argument('--benchmarks', action='store', dest='benchmarks',
            help='benchmarks: comma separated list(no spaces)')
    
    # run all or individual
    parser.add_argument('--runall', action='store_true', dest='runall', default=False,
                        help='run all benchmarks at once')
 
    parser.add_argument('--threads', type=int, action='store', dest='threads',
                        default=1,
                        help='number of threads')
    
    parser.add_argument('--num', type=int, action='store', dest='numkeys',
                        default=1000000,
                        help='number of keys')
 
    parser.add_argument('--value_size', type=int, action='store', default=100,
                        dest='value_size',
                        help='value size for insdb')

    parser.add_argument('--cache_size', type=int, action='store', default=67108864,
                        dest='cache_size',
                        help='cache size for db')

    parser.add_argument('--out_dir', action='store', default='.',
                        dest='out_dir',
                        help='default output dir')


    parser.add_argument('--dbbench_path', action='store', default='./db_bench',
                        dest='dbbench_path',
                        help='db_bench path')
 
    parser.add_argument('--dryrun', action='store_true', default=False,
                        dest='dryrun',
                        help='dry run')


    ## compression is default
    parser.add_argument('--compress', action='store_true', default=True,
                        dest='compress',
                        help='default compression (snappy)')

    ## compression is default
    parser.add_argument('--nocompress', action='store_false',
                        dest='compress',
                        help='default compression (snappy)')

    ## flush only controls kvdb, no flush control for rocksdb
    ## flush is default for kvdb
    parser.add_argument('--flush', action='store_true', default=True,
                        dest='flush',
                        help='default flush for kvdb only)')

    parser.add_argument('--noflush', action='store_false',
                        dest='flush',
                        help='no flush for kvdb')

#    parser.add_argument('--fullsktable_keycount', action='store_true', default=True,
#                        dest='fullsktable_keycount',
#                        help='default full table keycount, for kvdb only)')
#
#    parser.add_argument('--nofullsktable_keycount', action='store_false',
#                        dest='fullsktable_keycount',
#                        help='no full table keycount, for kvdb only)')

    parser.add_argument('--repeat', type=int, action='store', default=1,
                        dest='repeat',
                        help='How many time to repeat the runs')
 
    options = parser.parse_args()
    if options.dryrun:
        command.dryrun = 1

    print 'runall =', options.runall
    print 'out_dir=', options.out_dir

    #if os.path.isdir(options.out_dir) == False:
    #    print("Invalid output path " + options.out_dir + "\n")
    #    sys.exit(1)

    if options.dbtype not in dbtypes:
        print("Invalid dbtype " + options.dbtype + ", must be one of: " + ", ".join(dbtypes) + "\n")
        sys.exit(1)

#    os.environ['LD_LIBRARY_PATH'] = options.ceph_dir + "/lib:" + os.getenv("LD_LIBRARY_PATH")
#    os.environ['PATH'] = options.ceph_dir + "/bin:" + os.getenv("PATH")

    options.benchmark_options = benchmark_options
    if options.benchmarks:
        options.benchmark_options = options.benchmarks.split(",")

    options.operations_to_skip = operations_to_skip 

    return options
 
def createFolder(directory):
    try:
        if not os.path.exists(directory):
            os.makedirs(directory)
    except OSError:
        print ('Error: Creating directory. ' +  directory)

# for repeated runs, can generate an array to keep all reports.
# add a special fillseq array for average
# XXX not done
def add_stats_array(time_stats, bandwidth_stats, operation, latency, bandwidth):
    return

# get stats
def get_stats(options, time_stats, bandwidth_stats, line):

    ######### first match with special characters ^M from db_bench
    match = re.match(r"^.*\W(\w+)\s*:\s*(\d*[.]?\d*)\s*micros\/op.*;\s*(\d*[.]?\d*)\s*MB\/s.*$", line, re.IGNORECASE)
    if (match):
        operation = match.group(1)
        if operation in options.operations_to_skip:
            return -1
        latency = match.group(2)
        bandwidth = match.group(3)
        # print("op= " + operation + ", " + "time= " + latency + ", " + "throughput = " + bandwidth + "MB\n");
        time_stats[operation] = latency
        bandwidth_stats[operation] = bandwidth 
        return 0


    match = re.match(r"^.*\W(\w+)\s*:\s*(\d*[.]?\d*)\s*micros\/op.*;.*$", line, re.IGNORECASE)
    if (match):
        operation = match.group(1)
        if operation in options.operations_to_skip:
            return -1
        latency = match.group(2)
        time_stats[operation] = latency
        bandwidth_stats[operation] = 0
        # print("op= " + operation + ", " + "time= " + latency + "\n");
        return 0;

    ######### then match without special characters ^M from db_bench
    match = re.match(r"^(\w+)\s*:\s*(\d*[.]?\d*)\s*micros\/op.*;\s*(\d*[.]?\d*)\s*MB\/s.*$", line, re.IGNORECASE)
    if (match):
        operation = match.group(1)
        if operation in options.operations_to_skip:
            return -1
        #print("got operation: " + operation + "####\n")
        latency = match.group(2)
        bandwidth = match.group(3)
        # print("op= " + operation + ", " + "time= " + latency + ", " + "throughput = " + bandwidth + "MB\n");
        time_stats[operation] = latency
        bandwidth_stats[operation] = bandwidth 
        return 0


    match = re.match(r"^(\w+)\s*:\s*(\d*[.]?\d*)\s*micros\/op.*;.*$", line, re.IGNORECASE)
    if (match):
        operation = match.group(1)
        if operation in options.operations_to_skip:
            return -1
        #print("got operation: " + operation + "####\n")
        latency = match.group(2)
        time_stats[operation] = latency
        bandwidth_stats[operation] = 0
        # print("op= " + operation + ", " + "time= " + latency + "\n");
        return 0;

    return -1


#==================
# actually run db command
# will set options.failure flag if command failed
def run_db_bench_cmd(options, cmd, log, datafile_csv, time_stats, bandwidth_stats):
    if options.dryrun:
        print(cmd + "\n")
        return

    # parse results
    for line in command.run_cmd_get_line([cmd], log, options):
        print(line)
        get_stats(options, time_stats, bandwidth_stats, line)

    return 0


def get_db_bench_cmds(options):
    cmds = []
    compress_option = ""
    # just test one operator

    extra_options = ""
    if options.dbtype != "insdb" and options.dbtype != "leveldb":
        extra_options = " --merge_operator=put --cache_numshardbits=6"# --use_existing_db=true"

    if not options.compress:
        compress_option = " --compression_type=none"

    # only need to do flush for kvdb or insdb
    #if options.dbtype == "kvdb" or options.dbtype == "insdb":
    ## " --max_uval_iter_buffered_size="
    ## set to be cache size
    ## XXX fixed setting for INSDB, may need adjustment based on changes
    ## this is based on 6K kernel driver queue size
    ## 20% for max_uval_iter_buffered_size
    ## 10% for max_uval_prefetched_size
    if options.dbtype == "kvdb":
        extra_options = extra_options \
            + " --tablecache_low=" + str(options.sktable_keycount_low) \
            + " --tablecache_high=" + str(options.sktable_keycount_high) \
            + " --max_req_size=24576" \
            + " --num_write_worker=8" \
            + " --align_size=4" \
            + " --flush_threshold=" + str(60 * 1024 * options.value_size) \
            + " --min_update_cnt_table_flush=" + str(6*1024) \
            + " --slowdown_trigger=" + str(64 * 1024) \
            + " --max_uval_prefetched_size=" + str(options.numkeys*options.value_size/10) \
            + " --max_uval_iter_buffered_size=" + str(options.numkeys*options.value_size/5) \
            + " --max_table_deviceformat=" + str(options.numkeys*options.threads/700/3) \
            + " --kv_ssd=" + str(options.kvssd) \
            + " --iter_prefetch_hint=1"

        if options.flush:
            extra_options = extra_options + " --flush=true"


    ## XXX for rocksdb options only, should be comparable to kvdb settings
    if options.dbtype == "rocksdb":
        extra_options = extra_options + " --max_background_flushes=8 "
        # don't use direct IO when using limited memory option
        #--compressed_cache_size=0 --use_direct_io_for_flush_and_compaction=1 --use_direct_reads=1 "

    if (not options.runall):
        # run each operation with fillseq first except fillsync
        for operation in options.benchmark_options:
            use_existing_db_option = ""

            ## for rocksdb clear page cache first
            if options.dbtype == "rocksdb":
                cmds.append("sudo sync")
                cmds.append("sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'")

            # for isolated mode, fillseq never to be run separately
            # as it's run already for each operation
            if operation == "fillseq":
                continue

            # run these individually, no fillseq first
            # as they are creating fresh DB anyway (deleting existing ones if exist)
            elif  operation == "fillsync" \
                or operation == "fillbatch" \
                or operation == "fillrandom":

                cmds.append(options.dbbench_path + "/db_bench" 
                        + " --db=" + options.dbpath 
                        + " --benchmarks=" + operation 
                        + " --num=" + str(options.numkeys) 
                        + " --threads=" + str(options.threads) 
                        + " --value_size=" + str(options.value_size)  
                        + " --cache_size=" + str(options.cache_size)  
                        + compress_option 
                        + extra_options
                        )

            # all else run fillseq first
            else:
                ## assumes db name is the same
                ## make sure read from existing DB, XXX

                ## if it's readseq and readrandom, always do fillseq as a standalone commmand
                ## then immediately after run readseq and readrandom with using existing db option
                if operation == "readseq" or operation == "readrandom":
                    ## fillseq first
                    cmds.append(options.dbbench_path + "/db_bench"
                            + " --db=" + options.dbpath 
                            + " --benchmarks=fillseq"
                            + " --num=" + str(options.numkeys) 
                            + " --threads=" + str(options.threads) 
                            + " --value_size=" + str(options.value_size)  
                            + " --cache_size=" + str(options.cache_size)  
                            + compress_option 
                            + extra_options
                            )

                    ## for rocksdb clear page cache first
                    if options.dbtype == "rocksdb":
                        cmds.append("sudo sync")
                        cmds.append("sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'")

                    ## then read back
                    use_existing_db_option = " --use_existing_db=1"
                    cmds.append(options.dbbench_path + "/db_bench"
                            + " --db=" + options.dbpath 
                            + " --benchmarks=" + operation 
                            + " --num=" + str(options.numkeys) 
                            + " --threads=" + str(options.threads) 
                            + " --value_size=" + str(options.value_size)  
                            + " --cache_size=" + str(options.cache_size)  
                            + compress_option 
                            + use_existing_db_option
                            + extra_options
                            )

                ## for non readseq readrandom
                else:
                    ## fillseq first
                    cmds.append(options.dbbench_path + "/db_bench"
                            + " --db=" + options.dbpath 
                            + " --benchmarks=fillseq"
                            + " --num=" + str(options.numkeys) 
                            + " --threads=" + str(options.threads) 
                            + " --value_size=" + str(options.value_size)  
                            + " --cache_size=" + str(options.cache_size)  
                            + compress_option 
                            + extra_options
                            )

                    ## for rocksdb clear page cache first
                    if options.dbtype == "rocksdb":
                        cmds.append("sudo sync")
                        cmds.append("sudo bash -c 'echo 1 > /proc/sys/vm/drop_caches'")

                    ## then another operation with using existing DB
                    use_existing_db_option = " --use_existing_db=1"
                    cmds.append(options.dbbench_path + "/db_bench"
                            + " --db=" + options.dbpath 
                            + " --benchmarks=" + operation 
                            + " --num=" + str(options.numkeys) 
                            + " --threads=" + str(options.threads) 
                            + " --value_size=" + str(options.value_size)  
                            + " --cache_size=" + str(options.cache_size)  
                            + compress_option 
                            + use_existing_db_option
                            + extra_options
                            )

    else:
        # run all together
        all_ops = ""
        for operation in options.benchmark_options:
            all_ops = all_ops + operation + ","
            
        cmds.append(options.dbbench_path + "/db_bench" 
                + " --db=" + options.dbpath 
                + " --benchmarks=" + all_ops 
                + " --num=" + str(options.numkeys) 
                + " --threads=" + str(options.threads) 
                + " --value_size=" + str(options.value_size) 
                + " --cache_size=" + str(options.cache_size) 
                + compress_option 
                + extra_options
                )

    return cmds

# get unique attributes, but keep original order
def get_unique_attrs(attrs):
    attr_ret = []
    for attr in attrs:
        if not attr in attr_ret:
            attr_ret.append(attr)

    return attr_ret


def run_all_db_bench_cmd(options, log, datafile_csv, time_stats, bandwidth_stats):
    cmds = get_db_bench_cmds(options)
    for cmd in cmds:
        ## start time
        log.write("started " + cmd + "\n")
        start_time = time.time()
        run_db_bench_cmd(options, cmd, log, datafile_csv, time_stats, bandwidth_stats)
        end_time = time.time()
        timemsg = "command duration: " + cmd + " takes " + str(int(end_time - start_time)) + " seconds\n"
        print(timemsg)
        log.write(timemsg)

def save_as_csvheader(options, log, datafile_csv, time_stats, bandwidth_stats, timestr):

    # first a few column names
    key_str = "time, numkeys/thread, stats_type, dbtype, value_size, threads, cache_size, runtype, combined, flush"

    # keys = time_stats.keys()
    # for fixed order
    keys = ['failure'] + get_unique_attrs(options.benchmark_options)

    # format: time, dbtype, op1, op2, ...
    # header first line
    for op in keys:
        key_str = key_str + ", " + op

    ## add extra fields here
    ## for kvdb only with table low and high key count
    key_str = key_str + ", sktable_keycount_low, sktable_keycount_high"

    datafile_csv.write(key_str + "\n")

## old way of using cache size
#def get_runtype_name(options):
#    value_size_str = str(options.value_size) + "B"
#    if options.value_size >= 1024*1024:
#        value_size_str = str(options.value_size/1024/1024) + "MB"
#    elif options.value_size >= 1024:
#        value_size_str = str(options.value_size/1024) + "KB"
#
#
#    cache_size_str = str(options.cache_size) + "B"
#    if options.cache_size >= 1024*1024:
#        cache_size_str = str(options.cache_size/1024/1024) + "MB"
#    elif options.cache_size >= 1024:
#        cache_size_str = str(options.cache_size/1024) + "KB"
#
#    runtype = "v" + value_size_str + \
#                    "_c" + cache_size_str \
#                         + "_" + str(options.threads) \
#                         + "threads"
#    return runtype

# new way of using table keycount size
def get_runtype_name(options):
    value_size_str = str(options.value_size) + "B"
    if options.value_size >= 1024*1024:
        value_size_str = str(options.value_size/1024/1024) + "MB"
    elif options.value_size >= 1024:
        value_size_str = str(options.value_size/1024) + "KB"


    kc_size_str = str(options.sktable_keycount_low)
    if options.sktable_keycount_low >= 1000*1000:
        kc_size_str = "{0:.1f}".format(options.sktable_keycount_low/1000/1000.0) + "M"
    elif options.cache_size >= 1000:
        kc_size_str = "{0:.1f}".format(options.sktable_keycount_low/1000.0) + "K"

    runtype = "v" + value_size_str + \
                    "_kc" + kc_size_str \
                         + "_" + str(options.threads) \
                         + "threads"
    return runtype


def save_as_csv(options, log, datafile_csv, time_stats, bandwidth_stats, timestr):

    is_flush = 0
    if options.flush:
        is_flush = 1

    combined = 0
    if options.runall:
        combined = 1


    # first a few column names
    # key_str = "time, numkeys, stats_type, dbtype, value_size, threads, cache_size, runtype, combined, flush"

    # run spec info
    runtype = get_runtype_name(options)

    log.write("finished running: " + runtype + "\n")
    # latency first a few column values
    value_str_lead = timestr + ", " \
                 + str(options.numkeys) \
                 + ", time_per_op, " \
                 + options.dbtype \
                 + ", " + str(options.value_size) \
                 + ", " + str(options.threads) \
                 + ", " + str(options.cache_size) \
                 + ", " + runtype \
                 + ", " + str(combined) \
                 + ", " + str(is_flush)

    # keys = time_stats.keys()
    # for fixed order
    keys = ['failure'] + get_unique_attrs(options.benchmark_options)

    # format: time, dbtype, op1, op2, ...
    # header first line
    # for op in keys:
    #     key_str = key_str + ", " + op
    # datafile_csv.write(key_str + "\n")

    # value second line
    # for latency
    value_str = value_str_lead
    for op in keys:
        #print("check op XXX " + op + "\n")
        value_str = value_str + ", " + str(time_stats[op])

    ## add extra fields sktable_keycount_low and sktable_keycount_high
    value_str = value_str + ", " + str(options.sktable_keycount_low) \
                          + ", " + str(options.sktable_keycount_high)

    #############
    # for latency
    datafile_csv.write(value_str + "\n")


    # bandwidth first a few column values
    value_str_lead = timestr + ", " \
                 + str(options.numkeys) \
                 + ", bandwidth, " \
                 + options.dbtype \
                 + ", " + str(options.value_size) \
                 + ", " + str(options.threads) \
                 + ", " + str(options.cache_size) \
                 + ", " + runtype \
                 + ", " + str(combined) \
                 + ", " + str(is_flush)

    #############
    # for bandwidth
    value_str = value_str_lead
    for op in keys:
        value_str = value_str + ", " + str(bandwidth_stats[op])

    ## add extra fields sktable_keycount_low and sktable_keycount_high
    value_str = value_str + ", " + str(options.sktable_keycount_low) \
                          + ", " + str(options.sktable_keycount_high)

    datafile_csv.write(value_str + "\n")

# run for one round given an option, collect and save stats
def run(timestr, options):

    ## add a few flags before passing as an argument
    options.failure = 0
    options.header_written = 0

    logdir = options.out_dir + "/log"
    createFolder(logdir);
    createFolder(os.path.dirname(options.dbpath + options.dbtype))
    #if not os.path.exists(logdir):
    #    os.makedirs(logdir)

    ## log file for IOPS
    log = open(logdir + "/" + timestr + ".log", 'a', 0)

    runtype = get_runtype_name(options)
    log.write("starting dbbench with key# " + str(options.numkeys) + " " + runtype)

    # datafile_csv = open(logdir + "/dbbench" + timestr + ".csv", 'a', 0)
    datafile_csv = open(logdir + "/dbbench.csv", 'a', 0)
    datafile_header = open(logdir + "/dbbench_header.csv", 'a', 0)
    
    ## populate first
    keys = options.benchmark_options
    time_stats = {}
    bandwidth_stats = {}
    for key in keys:
        time_stats[key] = 0
        bandwidth_stats[key] = 0

    ## special internal key for fillseq
    ## XXX not done
    time_stats["fillseq_array"] = []
    bandwidth_stats["fillseq_array"] = []

    run_all_db_bench_cmd(options, log, datafile_csv, time_stats, bandwidth_stats)

    if options.failure == 1:
        time_stats['failure'] = 1
        bandwidth_stats['failure'] = 1
        # reset
        options.failure == 0
    else:
        time_stats['failure'] = 0
        bandwidth_stats['failure'] = 0

    # print result
    # pprint.pprint(time_stats)
    # pprint.pprint(bandwidth_stats)
    print("latency in ns\n")
    pprint.pprint(time_stats)
    print("\n")

    log.write("latency in ns\n")
    log.write(pprint.pformat(time_stats) + "\n")

    print("bandwidth in MB/s\n")
    pprint.pprint(bandwidth_stats)
    print("\n")

    log.write("bandwidth in MB/s\n")
    log.write(pprint.pformat(bandwidth_stats) + "\n")

    ## use a single timestamp to record stats
    if options.header_written == 0:
        save_as_csvheader(options, log, datafile_header, time_stats, bandwidth_stats, timestr)
        options.header_written = 1

    save_as_csv(options, log, datafile_csv, time_stats, bandwidth_stats, timestr)

    return 0


#==============
def main(options):
    timestr = time.strftime("%Y%m%d-%H-%M-%S")

    ## automatically generate parameters
    if options.automatic:
        #runspecs = []
        # for small value sizes
        #runspecs_small = get_runspecs(cachesize_list_small, thread_list, valuesize_keys_list_small)
        #runspecs.extend(runspecs_small)

        # for bigger value sizes
        #runspecs = get_runspecs_with_fixed_cache(cachesize_list, thread_list, valuesize_keys_list)
        #scales = [5, 10]
        ## XXX affect # of runs, 1 to 10 for 10% to 100% cache
        #scales = [7, 10]
        scales = [2]
        runspecs = get_runspecs_with_dynamic_cache(options, thread_list, valuesize_keys_list, scales)

        #pprint.pprint(runspecs)
        i = 1
        for onespec in runspecs:
            #print("before")
            #pprint.pprint(options)
            #pprint.pprint(onespec)
            update_option(options, onespec, i)
            runtype = get_runtype_name(options)
            print("starting dbbench with key# " + str(options.numkeys) + " " + runtype)
            #print("after")
            #pprint.pprint(options)
            run(timestr, options)
            i = i + 1


## reduce runs for daily
#        if options.dbtype == "kvdb" or options.dbtype == "insdb":
#            scales = [0,2,6,8]
#        else:
#            scales = [2,6,8]
#
#        runspecs = get_runspecs_with_dynamic_cache(options, thread_list, valuesize_keys_list, scales)
#
#        #pprint.pprint(runspecs)
#        for onespec in runspecs:
#            #print("before")
#            #pprint.pprint(options)
#            #pprint.pprint(onespec)
#            update_option(options, onespec, i)
#            runtype = get_runtype_name(options)
#            print("run sequence#" + str(i) + " starting dbbench with key# " + str(options.numkeys) + " " + runtype)
#            #print("after")
#            #pprint.pprint(options)
#            run(timestr, options)
#            i = i + 1

    else:
        # command line driven
        run(timestr, options)

    return 0

## main function
if __name__ == "__main__":
    options = get_options()
    options.dbpathsave = options.dbpath
    main(options)
