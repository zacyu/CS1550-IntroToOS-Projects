CC    := gcc
CFLAGS:= -m32 -Os -Wall
KERNEL_DIR	:= linux-2.6.23.1

prodcons: prodcons.c kernel
	$(CC) $(CFLAGS) -o $@ -I $(PWD)/linux-2.6.23.1/include/ prodcons.c

kernel: linux-2.6.23.1/.config
	$(MAKE) -C $(KERNEL_DIR) ARCH=i386 bzImage

linux-2.6.23.1/.config:
	tar --skip-old-files -xjf original/linux-2.6.23.1.tar.bz2
	cp original/.config $(PWD)/linux-2.6.23.1/

compress: prodcons.c Makefile linux-2.6.23.1/kernel/sys.c linux-2.6.23.1/arch/i386/kernel/syscall_table.S linux-2.6.23.1/include/asm-i386/unistd.h
	tar -czvf zhy46-project2.tar.gz $^

clean:
	$(MAKE) -C $(KERNEL_DIR) clean
	rm -f prodcons zhy46-project2.tar.gz
