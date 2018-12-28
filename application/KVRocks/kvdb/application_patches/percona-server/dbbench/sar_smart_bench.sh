#!/bin/bash -xe
if [ "$#" -ne 3 ]
then
    echo "invalid # of argument"
    echo "use: $0 <kvrocks | myrocks> <time> <process_name>"
    exit 1
fi

if ! [[ "$1" == "kvrocks" || "$1" == "myrocks" ]]
then
    echo "invalid type: $1, use: kvrocks | myrocks"
    exit 1
fi

set +e
~/bin/nvme_smart.sh /dev/nvme0n1 2>&1 >> smart_start0.log
~/bin/nvme_smart.sh /dev/nvme1n1 2>&1 >> smart_start1.log
sar -S -t 1 2>&1 >> tee swap.log &
sar -r -t 1 2>&1 >> mem.log &
sar -u -t 1 2>&1 >> cpu.log &
#sar -u -P ALL -t 2 2>&1 | tee cpu_all.log &
sar -p -d -t 1 2>&1 >> disk.log &
#sar -n DEV -t 1 2>&1 | tee net.log &

export PYTHONPATH=~/python
if [[ "$1" == "kvrocks" ]]
then
    ~/bin/kvproc_schedule.py --kvrocks --time $2 --process_name $3 &
fi
if [[ "$1" == "myrocks" ]]
then
    ~/bin/kvproc_schedule.py --myrocks --time $2 --process_name $3 &
fi
#~/bin/nvme_smart.sh /dev/nvme0n1 2>&1 | tee smart_stop0.log
#~/bin/nvme_smart.sh /dev/nvme1n1 2>&1 | tee smart_stop1.log
#killall python
#killall sar
