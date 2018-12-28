#!/bin/bash 
set -e

numosd=1
numreplicas=1
os_type=$1
use_gdb=$2
 
numpgs=$((numosd * 100 / numreplicas))

if [ -z $os_type ]; then 
	os_type=kvsstore
fi

if [ -z $use_gdb ]; then 

	echo "## VSTART sudo CEPH_DEV=1 MON=1 MDS=0 OSD=$numosd ../src/vstart.sh -d -n -x --$os_type $use_gdb"
	sudo CEPH_DEV=1 MON=1 MDS=0 OSD=$numosd ../src/vstart.sh -n -x --$os_type $use_gdb
else

	echo "## VSTART sudo CEPH_DEV=1 MON=1 MDS=0 OSD=$numosd ../src/vstart.sh -d -n -x --$os_type $use_gdb"
	sudo CEPH_DEV=1 MON=1 MDS=0 OSD=$numosd ../src/vstart_dbg.sh -n -x --$os_type --gdb
fi

# as recommended in http://docs.ceph.com/docs/jewel/rados/configuration/pool-pg-config-ref/


