#!/usr/bin/python
import os
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
import threading


parser = argparse.ArgumentParser()
parser.add_argument('--time', type=int, action='store', dest='time',
    default=36000,
    help='time to run')
parser.add_argument('--kvrocks', action='store_true', dest='kvrocks',
    default=False,
    help='is KvRocks')
parser.add_argument('--myrocks', action='store_true', dest='myrocks',
    default=False,
    help='is MyRocks')
parser.add_argument('--process_name', action='store', dest='process_name',
    default='mysqld',
    help='process name')

options = parser.parse_args()

log = open("kv_proc.log", 'a', 0)
log1 = open("mem_cpu_" + options.process_name + ".log", 'a', 0)

def dowork(cmd, log):
    #print "Howdy"
    #print('hello {:.4f}'.format(time.time()))
    timestr = time.strftime("%Y%m%d %H-%M-%S")
    log.write(timestr + "\n")
    command.run_cmd_clean([cmd], log)

def dowork_clean(cmd, log):
    #print "Howdy"
    #print('hello {:.4f}'.format(time.time()))
    command.run_cmd_clean([cmd], log)


utc = time.time()
start = utc
end = start + options.time
#end = start + 20
while (utc < end):
    if options.kvrocks:
        dowork("cat /proc/kv_proc", log)
    cmd = "ps -p $(pidof " + options.process_name + ") -o pcpu,pmem,rss"
    dowork_clean(cmd, log1)
    time.sleep(1)
    utc = time.time()

log.close()
log1.close()
