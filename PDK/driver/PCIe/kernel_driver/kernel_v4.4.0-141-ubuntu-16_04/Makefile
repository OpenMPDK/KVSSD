
obj-m               += nvme.o
lightnvm-y			:= lightnvm.o
nvme-y				+= core.o pci.o $(lightnvm-y)
nvme-y              += scsi.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
