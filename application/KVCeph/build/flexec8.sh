#!/bin/bash
#sudo CEPH_DEV=1 MON=1 MDS=0 OSD=1 ../src/vstart.sh -n -x --kvsstore
#CEPH_HEAP_PROFILER_INIT=true sudo CEPH_DEV=1 MON=1 MDS=0 OSD=1 ../src/vstart.sh -n -x --kvsstore
#sudo CEPH_DEV=1 MON=1 MDS=0 OSD=1 ../src/vstart.sh -n -x --kvsstore
#sleep 6
#echo ""
ps -aux | grep ceph-osd
echo ""
#sudo ceph osd pool create rbd 100 100 -c /local/cephuser/latest_ceph/ceph-kvssd/build/ceph.conf
#sleep 4
#sudo ceph -s -c /local/cephuser/latest_ceph/ceph-kvssd/build/ceph.conf
#sudo ceph tell osd.0 heap stats
#sudo ceph tell osd.0 heap start_profiler
#sleep 3
#sudo ceph tell osd.0 heap dump
free -h
for k in `seq 1 $((8))`; do
	#sudo ./ceph-runtime/bin/rados bench -p rbd -b 4096 -t 128 30 write --run-name client${k} --no-cleanup -c ./ceph.conf

	sudo ./ceph-runtime/bin/rados bench -p rbd -b 4096 -t 128 30 write --run-name client${k} --no-cleanup -c ./ceph.conf & pids+=($!)
done

if [[ ! -z $pids ]]; then
         wait "${pids[@]}"
fi

#sudo ceph tell osd.0 heap stats
free -h
#sudo ceph tell osd.0 heap dump
#sudo ceph tell osd.0 heap stop_profiler 
