CONFIG_MODULE_SIG=7
obj-m   += nvme.o
nvme-y  := nvme-core.o nvme-scsi.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd` modules
clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean
