KERNELDIR := /home/xiaojian/linux/imx6ull/linux_src/NXP-linux/linux-imx-rel_imx_4.1.15_2.1.0_ga # 内核源码路径
CURRENT_PATH := $(shell pwd)
obj-m := linux_multi_button.o # 编译生成的模块名称
build: kernel_modules  # 编译模块
kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean
