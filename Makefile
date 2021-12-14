obj-m += fifo_monitor.o
fifo_monitor-objs := chkfifo.o chkfifo_proc_reads.o

KERNELDIR       ?= /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.o core .depend .*.cmd *.ko *.mod.c
