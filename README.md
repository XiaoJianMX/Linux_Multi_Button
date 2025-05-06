# Linux_Multi_Button
# multi_button

## 项目简介
multi_button 是一个基于 Linux 内核的多按键驱动示例，支持单击、双击、长按等多种按键事件检测，并通过字符设备与用户空间通信。可以注册多个按键。并且读取数据是阻塞的。

## 主要文件说明
- `linux_multi_button.c`：主驱动源码，实现了按键的 GPIO 初始化、中断处理、定时器去抖动、事件上报等功能。
- `test_app.c`：用户空间测试程序，用于读取按键事件并在终端打印。
- `example.dts`：设备树节点示例，展示如何在设备树中配置按键相关属性。
- `Makefile`：内核模块编译脚本。
- `make.sh`：交叉编译脚本示例。如使用交叉编译可以使用make.sh

## 编译与运行
1. 修改 `Makefile` 中的 `KERNELDIR` 路径为你的内核源码路径。
2. 运行 `make.sh` 进行交叉编译，生成 `linux_multi_button.ko` 内核模块。
3. 将模块和测试程序部署到目标板，插入模块并运行测试程序。

## 设备树配置
参考 `example.dts` 文件，根据实际硬件连接修改 GPIO 和中断号。

如需进一步完善文档内容，请补充具体使用说明、硬件连接图等信息。

应用程序读取按键示例:
```
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
enum BUTTON_EVENT{
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_CLICK,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS,
    BUTTON_EVENT_LONG_HOLD
};
int main(int argc, char *argv[])
{
    int fd = open("/dev/multi_button", O_RDONLY);
    if (fd == -1)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    int read_data[2] = {0};
    while (1)
    {
        int bytes_read = read(fd, read_data, sizeof(read_data));
        if (bytes_read < 0)
        {
            perror("read");
            exit(EXIT_FAILURE);
        }
        switch (read_data[1])
        {
        case BUTTON_EVENT_CLICK:
        printf("按钮编号为%d, 按键的事件为%s\n", read_data[0], "单击");
            break;
        case BUTTON_EVENT_DOUBLE_CLICK:
        printf("按钮编号为%d, 按键的事件为%s\n", read_data[0], "双击");
            break;
        case BUTTON_EVENT_LONG_PRESS:
        //printf("按钮编号为%d, 按键的事件为%s\n", read_data[0], "长按");
        case BUTTON_EVENT_LONG_HOLD:
        printf("按钮编号为%d, 按键的事件为%s\n", read_data[0], "长按保持");
            break;
        default:
            break;
        }
    }
    close(fd);
    return 0;
}
```
