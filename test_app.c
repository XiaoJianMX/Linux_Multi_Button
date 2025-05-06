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