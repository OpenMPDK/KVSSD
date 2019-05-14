obj-m					+= nvme-core.o
obj-m					+= nvme.o
#obj-m					+= nvme-fabrics.o
#obj-m					+= nvme-rdma.o
#obj-m					+= nvme-fc.o

nvme-core-y				:= core.o
nvme-core-y				+= scsi.o
nvme-y					+= pci.o
#nvme-fabrics-y				+= fabrics.o
#nvme-rdma-y				+= rdma.o
#nvme-fc-y				+= fc.o
all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean

