obj-m = part3.o

KVERSION = $(shell uname -r)

all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) module
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean



