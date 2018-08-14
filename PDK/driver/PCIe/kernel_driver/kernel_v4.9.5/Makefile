obj-m					+= nvme-core.o
obj-m					+= nvme.o
nvme-core-y				:= core.o
nvme-core-y				+= scsi.o
nvme-core-y				+= lightnvm.o
nvme-y					+= pci.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean

