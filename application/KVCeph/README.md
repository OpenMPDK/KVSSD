
# KVCeph - CEPH for Samsung Key-Value SSDs

KVCeph introduces a new CEPH object store, KvsStore, that is designed to support Samsung KV SSDs. The following three components are added to Ceph's Luminous release:

   - KvsStore - an object store for KV SSDs
   - Onode Prefetcher - a utility class that can prefetch Onodes (object metadata) 
   - EventDriven Request Scheduler - a replacement for the existing sharded_opwq scheduler. 

## Checking out the source

You can clone from github with

	git clone git@github.com/OpenMPDK/KVSSD.git

or, if you are not a github user,

	git clone https://github.com/OpenMPDK/KVSSD.git

KVCeph is located at /KVSSD/application/KVCeph/.
	
## Prerequisites

### 1. Install dependent packages

    ./install-deps.sh

### 2. Install a KV kernel driver 

	cd /KVSSD/PDK/driver/PCIe/kernel_driver/kernel_vX.X.X
	make 
	make install 
	./re_insmod.sh

### 3. Format Samsung KV NVMe SSDs

	sudo nvme format (device path) -s1 -n1

## Building KVCeph

No extra steps are necessary to build KVCeph. The original Ceph build instructions can be used. 
	
For example:

	cd /KVSSD/application/KVCeph/
	cd build
	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./ceph-runtime ..
	make
	make install  (This will install the binaries under the ceph-runtime directory)

For a list of Ceph cmake options: [https://github.com/ceph/ceph/](https://github.com/ceph/ceph/)

## Configuring KVCeph

Prior to running KVCeph, the following two KvsStore configuration parameters need to be added to ceph.conf.

* osd objectstore = kvsstore    - sets the type of object store to use

* kvsstore\_dev\_path = /dev/path - sets the path to a Samsung KV SSD

where _osd objectstore_ needs to be defined in the [_global_] section in ceph.conf, and _kvsstore\_dev\_path_ should be set in each OSD section.
Please also see a sample configuration file, /KVCeph/build/sample.conf, which demonstrates the use of these parameters.

## Running a test cluster

For convenience, the Ceph utility vstart.sh is modified to support KvsStore. To run a test cluster with a vstart.sh:

### 1. Set the device path in vstart.sh 

	By default, kvsstore_dev_path is set to /dev/nvme0n1 in vstart.sh. 
	Please modify the line that defines kvsstore_dev_path in vstart.sh if needed.

### 2. Run a test cluster with KvsStore 
	
	cd build
	make vstart        # builds just enough to run vstart
    sudo CEPH_DEV=1 MON=1 MDS=0 OSD=1 ../src/vstart.sh -n -x --kvsstore 
	./bin/ceph -s

## Running a cluster in deploy mode

The instruction for setting up and deploying CEPH clusters can be found at [Original Ceph Deploy Manual Page ](http://docs.ceph.com/docs/master/install/manual-deployment/).

Additionally, KVCeph requires the parameter, kvsstore\_dev\_path, to be set per OSD in ceph.conf, and the KV SSD kernel driver needs to be loaded in each node. The ceph.conf for KVCeph would look like the following.

<pre>
	[global]
		...
		<b>osd_objectstore = kvsstore</b>
		...
	[osd.0]
		# settings affect osd.0 only
		<b>kvsstore_dev_path = (nvme device path 1)</b>
		cluster addr = (cluster network IP of Node 0)
		public addr =  (public network IP of Node 0)
		host = (hostname)
		...
	[osd.1]
	    ... 
		<b>kvsstore_dev_path = (nvme device path 2)</b>
		...
	[osd.2]
		...
		<b>kvsstore_dev_path = (nvme device path 3)</b>
		...
</pre>

## Unit tests 
	
	The following command will run kvsstore unittests.

	cd build
	./unittest_f.sh
















