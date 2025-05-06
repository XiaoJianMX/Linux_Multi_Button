#include <linux/types.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/string.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#define DRIVER_NAME             "multi_button"
#define DRIVER_VERSION          "v1.0"
#define DRIVER_AUTHOR           "XiaoJian"
#define DEBOUNCE_TIME           (20) // 去抖动时间，单位ms
#define DOUBLE_CLICK_TIME       (100) // 双击时间，单位ms
#define LONG_PRESS_TIME         (1000) // 长按时间，单位ms
#define MAX_BUTTONS             (1) // 最大按键数   按需修改
// 按键状态
enum BUTTON_STATE{
    BUTTON_IDLE = 0,
    BUTTON_DEBOUNCE,
    BUTTON_PRESSED,
    BUTTON_CLICK_RELEASED,
    BUTTON_LONG_PRESSED
};
// 按键事件
enum BUTTON_EVENT{
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_CLICK,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS,
    BUTTON_EVENT_LONG_HOLD
};
/* 按钮中断的配置以及状态等 */
struct irq_button_desc{
    //wait_queue_t button_wait_queue;
    int state;
    unsigned long press_time;       //按下的时间
    unsigned long release_time;     //松开的时间
    int last_event;                 //上一次的事件
    int click_count;                //点击次数
    int irq_num;
    int gpio_num;
    char name[10];
    irq_handler_t irq_handler;      //中断处理函数
};
// 设备结构体
struct buttons_dev{
    dev_t dev_id;
    int major;
    int minor;
    int event_ready;                                            //事件是否就绪
    wait_queue_head_t button_wait_head;
    struct irq_button_desc irq_button_desc[MAX_BUTTONS];        //多少个按键就有多少个irq_button_desc
    struct timer_list timer;
    struct cdev buttons_cdev;
    struct class *buttons_class;
    struct device *buttons_device;
    struct device_node *buttons_node;
};
static struct buttons_dev *buttons_device;

//中断处理函数
static irqreturn_t buttons_irq_handler(int irq, void *dev_id)
{
    struct irq_button_desc *button_desc = (struct irq_button_desc *)dev_id;
    
    if(button_desc->state == BUTTON_IDLE){
        button_desc->state = BUTTON_DEBOUNCE;
        mod_timer(&buttons_device->timer, jiffies + msecs_to_jiffies(DEBOUNCE_TIME));
    }
    return IRQ_HANDLED;
}
//定时器处理函数
static void buttons_timer_handler(unsigned long data)
{   
    struct buttons_dev *dev = (struct buttons_dev *)data;
    int i;
    unsigned long now = jiffies;
    int need_restart_timer = 0;
    buttons_device->event_ready = 0;
    for(i = 0; i < MAX_BUTTONS; i++){
        if(dev->irq_button_desc[i].state != BUTTON_IDLE){
            int value = gpio_get_value(dev->irq_button_desc[i].gpio_num);
            switch (dev->irq_button_desc[i].state)
            {
            case BUTTON_DEBOUNCE:
                if(value == 0){//按下
                    dev->irq_button_desc[i].state = BUTTON_PRESSED;
                    dev->irq_button_desc[i].press_time = now;
                    need_restart_timer = 1;
                }else{
                    dev->irq_button_desc[i].state = BUTTON_IDLE;
                }
                break;
            case BUTTON_PRESSED:
                if(value == 0){//按下检测长按还是短按（双击）
                    if(time_after(now, dev->irq_button_desc[i].press_time + msecs_to_jiffies(LONG_PRESS_TIME))){
                        dev->irq_button_desc[i].last_event = BUTTON_EVENT_LONG_PRESS;
                        buttons_device->event_ready = 1;
                        dev->irq_button_desc[i].state = BUTTON_LONG_PRESSED;
                        printk(KERN_INFO"button%d long pressed\n", i);
                        need_restart_timer = 1;
                    }else{
                        mod_timer(&buttons_device->timer, jiffies + msecs_to_jiffies(100));
                    }
                }else{
                    dev->irq_button_desc[i].release_time = now;
                    dev->irq_button_desc[i].click_count ++;
                    dev->irq_button_desc[i].state = BUTTON_CLICK_RELEASED;
                    /* 双击检测 */
                    mod_timer(&buttons_device->timer, jiffies + msecs_to_jiffies(DOUBLE_CLICK_TIME));
                }
                break;
            case BUTTON_CLICK_RELEASED:
                if(value == 0){//按键松开
                    dev->irq_button_desc[i].state = BUTTON_DEBOUNCE;
                    mod_timer(&buttons_device->timer, jiffies + msecs_to_jiffies(DEBOUNCE_TIME));
                }else{//检测是否双击
                    if(dev->irq_button_desc[i].click_count == 1){
                        dev->irq_button_desc[i].last_event = BUTTON_EVENT_CLICK;
                        printk(KERN_INFO"button%d clicked\n", i);
                    }else if(dev->irq_button_desc[i].click_count == 2){
                        dev->irq_button_desc[i].last_event = BUTTON_EVENT_DOUBLE_CLICK;
                        printk(KERN_INFO"button%d double clicked\n", i);
                    }
                    dev->irq_button_desc[i].click_count = 0;
                    wake_up_interruptible(&buttons_device->button_wait_head);
                    buttons_device->event_ready = 1;
                    dev->irq_button_desc[i].state = BUTTON_IDLE;
                }
                break;
            case BUTTON_LONG_PRESSED:
                if(value == 1){//长按释放
                    dev->irq_button_desc[i].state = BUTTON_IDLE;
                }else{//一直保持长按
                    dev->irq_button_desc[i].last_event = BUTTON_EVENT_LONG_HOLD;
                    wake_up_interruptible(&buttons_device->button_wait_head);
                    buttons_device->event_ready = 1;
                    printk(KERN_INFO"button%d long hold\n", i);
                    mod_timer(&buttons_device->timer, jiffies + msecs_to_jiffies(300));
                }
                break;
            default:
                printk(KERN_INFO"default\n");
                dev->irq_button_desc[i].state = BUTTON_IDLE;
                break;
            }
        }
    }
    //重新启动定时器，检测按键状态
    if(need_restart_timer) {
        mod_timer(&buttons_device->timer, jiffies + msecs_to_jiffies(100));
    }
}
//初始化GPIO
static int buttons_gpio_init(void)
{   
    int i ,ret;
    buttons_device->buttons_node = of_find_node_by_path("/buttons");//设备树路径
    if(buttons_device->buttons_node == NULL){
        printk(KERN_ERR"of_find_node_by_path error\n");
        return -EFAULT;
    }
    memset(buttons_device->irq_button_desc, 0, sizeof(struct irq_button_desc) * MAX_BUTTONS);
    for(i = 0; i < MAX_BUTTONS; i++){//为每个按键分配GPIO，以及GPIO中断
        //获取gpio编号
        struct device_node *child_node = of_get_next_child(buttons_device->buttons_node, NULL);
        if(child_node == NULL){
            printk(KERN_ERR"of_get_next_child error\n");
            return -EFAULT;
        }
        buttons_device->irq_button_desc[i].gpio_num = of_get_named_gpio(child_node, "button-gpios", 0);
        if(buttons_device->irq_button_desc[i].gpio_num < 0){
            printk(KERN_ERR"of_get_named_gpio error\n");
            return -EFAULT;
        }
        //获取irq编号
        sprintf(buttons_device->irq_button_desc[i].name, "button%d", i);
        ret = gpio_request(buttons_device->irq_button_desc[i].gpio_num, buttons_device->irq_button_desc[i].name);
        if(ret < 0){
            printk(KERN_ERR"gpio_request error\n");
            return -EFAULT;
        }
        gpio_direction_input(buttons_device->irq_button_desc[i].gpio_num);
        buttons_device->irq_button_desc[i].irq_num = gpio_to_irq(buttons_device->irq_button_desc[i].gpio_num);
        if(buttons_device->irq_button_desc[i].irq_num < 0){
            printk(KERN_ERR"gpio_to_irq error\n");
            return -EFAULT;
        }
        printk(KERN_INFO"button%d gpio_num = %d, irq_num = %d\n", i, 
                buttons_device->irq_button_desc[i].gpio_num, 
                buttons_device->irq_button_desc[i].irq_num);
        buttons_device->irq_button_desc[i].irq_handler = buttons_irq_handler;
        //注册中断
        ret = request_irq(buttons_device->irq_button_desc[i].irq_num,
                        buttons_device->irq_button_desc[i].irq_handler,
                        IRQF_TRIGGER_FALLING,
                        buttons_device->irq_button_desc[i].name,
                        (void *)&buttons_device->irq_button_desc[i]);
        if(ret < 0){
            printk(KERN_ERR"request_irq error\n");
            return -EFAULT;
        }
        //添加等待队列到等待队列头
        //add_wait_queue(&buttons_device->button_wait_head, &buttons_device->irq_button_desc[i].button_wait_queue);
    }
    //创建定时器
    init_timer(&buttons_device->timer);
    buttons_device->timer.function = buttons_timer_handler;
    buttons_device->timer.data = (unsigned long)buttons_device;
    buttons_device->timer.expires = jiffies + msecs_to_jiffies(DEBOUNCE_TIME);
    add_timer(&buttons_device->timer);
    return 0;
}
//字符设备驱动 file_ops open函数
static int buttons_open(struct inode *inode, struct file *filp)
{   
    struct buttons_dev *dev = container_of(inode->i_cdev, struct buttons_dev, buttons_cdev);
    filp->private_data = dev;
    return 0;
}
//字符设备驱动 file_ops read函数
static ssize_t buttons_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{   
    int i;
    ssize_t ret = 0;
    int send_data[2] = {0};
    struct buttons_dev *dev = filp->private_data;
    if(filp->f_flags & O_NONBLOCK){//不支持非阻塞模式
        return -EAGAIN;
    }else{
        wait_event_interruptible(buttons_device->button_wait_head, buttons_device->event_ready);
        printk(KERN_INFO"read event_ready\n");
        buttons_device->event_ready = 0;
        for(i = 0;i < MAX_BUTTONS; i++){
            if(dev->irq_button_desc[i].last_event != BUTTON_EVENT_NONE){
                send_data[0] = i;send_data[1] = dev->irq_button_desc[i].last_event;//第一个为按键编号，第二个为事件类型
                ret = copy_to_user(buf, send_data, sizeof(send_data));//发送数据到用户空间
                if(ret < 0){
                    printk(KERN_ERR"copy_to_user error\n");
                    return -EFAULT;
                }
                dev->irq_button_desc[i].last_event = BUTTON_EVENT_NONE;
            }
        }
    }
    
    return ret;
}
//字符设备驱动 file_ops release函数
static int buttons_release(struct inode *inode, struct file *filp)
{
    return 0;
}
//字符设备驱动 file_ops结构体
struct file_operations buttons_fops = {
    .owner = THIS_MODULE,
    .open = buttons_open,
    .read = buttons_read,
    .release = buttons_release,
};

//驱动初始化函数
static int __init buttons_init(void)
{   
    int ret = 0;
    buttons_device = kmalloc(sizeof(struct buttons_dev), GFP_KERNEL);
    //初始化等待队列
    init_waitqueue_head(&buttons_device->button_wait_head);
    // 初始化设备号
    if(buttons_device->major){
        buttons_device->dev_id = MKDEV(buttons_device->major, 0);
        ret = register_chrdev_region(buttons_device->dev_id, 1, DRIVER_NAME);
        if(ret < 0){
            printk("register_chrdev_region error\n");
            goto dev_error;
        }
    }else{
        ret = alloc_chrdev_region(&buttons_device->dev_id, 0, 1, DRIVER_NAME);
        if(ret < 0){
            printk("alloc_chrdev_region error\n");
            goto dev_error;
        }
        buttons_device->major = MAJOR(buttons_device->dev_id);
        buttons_device->minor = MINOR(buttons_device->dev_id);
    }
    // 初始化cdev
    cdev_init(&buttons_device->buttons_cdev, &buttons_fops);
    ret = cdev_add(&buttons_device->buttons_cdev, buttons_device->dev_id, 1);
    if(ret < 0){
        printk("cdev_add error\n");
        goto cdev_error;
    }
    //设备类
    buttons_device->buttons_class = class_create(THIS_MODULE, DRIVER_NAME);
    if(IS_ERR(buttons_device->buttons_class)){
        printk("class_create error\n");
        goto class_error;
    }
    //创建设备
    buttons_device->buttons_device = device_create(buttons_device->buttons_class, NULL, 
                                                    buttons_device->dev_id, NULL, DRIVER_NAME);
    if(IS_ERR(buttons_device->buttons_device)){
        printk("device_create error\n");
        goto device_error;
    }
    ret = buttons_gpio_init();
    if(ret < 0){
        goto device_error;
    }
    printk("buttons_init\n");
    return ret;
    //错误处理
device_error:
    class_destroy(buttons_device->buttons_class);
class_error:
    cdev_del(&buttons_device->buttons_cdev);
cdev_error:
    unregister_chrdev_region(buttons_device->dev_id, 1);
dev_error:
    return -EFAULT;
}
//驱动卸载函数
static void __exit buttons_exit(void)
{   
    int i;
    del_timer(&buttons_device->timer);
    for(i = 0; i < MAX_BUTTONS; i++){
        free_irq(buttons_device->irq_button_desc[i].irq_num, (void *)(void *)&buttons_device->irq_button_desc[i]);
        gpio_free(buttons_device->irq_button_desc[i].gpio_num);
    }
    device_destroy(buttons_device->buttons_class, buttons_device->dev_id);
    class_destroy(buttons_device->buttons_class);
    cdev_del(&buttons_device->buttons_cdev);
    unregister_chrdev_region(buttons_device->dev_id, 1);
    kfree(buttons_device);
    printk("buttons_exit\n");
}

module_init(buttons_init);
module_exit(buttons_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);