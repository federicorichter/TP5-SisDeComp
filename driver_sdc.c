#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h> 

#define GPIO1 538
#define GPIO2 539

static int irq_number1, irq_number2;
static int button_state = 0;
static int selected_gpio = GPIO1;  // Default GPIO pin
static struct task_struct *task;
static struct gpio_desc *gpio_desc1, *gpio_desc2;

static irqreturn_t button_isr(int irq, void *data) {
    button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
    return IRQ_HANDLED;
}

static int button_thread(void *arg) {
    while (!kthread_should_stop()) {
        button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
        msleep(200);
    }
    return 0;
}

static ssize_t gpio_select_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) {
    char kbuf[4];
    if (count > 3) return -EINVAL;
    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    kbuf[count] = '\0';

    if (strcmp(kbuf, "538") == 0) {
        selected_gpio = GPIO1;
    } else if (strcmp(kbuf, "539") == 0) {
        selected_gpio = GPIO2;
    } else {
        return -EINVAL;
    }
    return count;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = gpio_select_write,
};

static struct miscdevice gpio_select_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "gpio_select",
    .fops = &fops,
};

static int __init button_init(void) {
    int result;

    gpio_desc1 = gpio_to_desc(GPIO1);
    gpio_desc2 = gpio_to_desc(GPIO2);
    if (!gpio_desc1 || !gpio_desc2) {
        printk(KERN_ERR "Invalid GPIO descriptor\n");
        return -ENODEV;
    }

    result = gpio_request(GPIO1, "sysfs");
    if (result) {
        printk(KERN_ERR "Failed to request GPIO1: %d\n", result);
        return result;
    }

    result = gpio_request(GPIO2, "sysfs");
    if (result) {
        printk(KERN_ERR "Failed to request GPIO2: %d\n", result);
        gpio_free(GPIO1);
        return result;
    }

    result = gpiod_direction_input(gpio_desc1);
    if (result) {
        printk(KERN_ERR "Failed to set GPIO1 direction: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    result = gpiod_direction_input(gpio_desc2);
    if (result) {
        printk(KERN_ERR "Failed to set GPIO2 direction: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    gpiod_set_debounce(gpio_desc1, 200);
    gpiod_set_debounce(gpio_desc2, 200);
    gpiod_export(gpio_desc1, false);
    gpiod_export(gpio_desc2, false);

    irq_number1 = gpiod_to_irq(gpio_desc1);
    irq_number2 = gpiod_to_irq(gpio_desc2);
    if (irq_number1 < 0 || irq_number2 < 0) {
        printk(KERN_ERR "Failed to get IRQ numbers\n");
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return -EINVAL;
    }

    result = request_irq(irq_number1, button_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "button_gpio1_handler", NULL);
    if (result) {
        printk(KERN_ERR "Failed to request IRQ1: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    result = request_irq(irq_number2, button_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "button_gpio2_handler", NULL);
    if (result) {
        printk(KERN_ERR "Failed to request IRQ2: %d\n", result);
        free_irq(irq_number1, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    task = kthread_run(button_thread, NULL, "button_thread");
    if (IS_ERR(task)) {
        printk(KERN_ERR "Failed to create kernel thread: %ld\n", PTR_ERR(task));
        free_irq(irq_number1, NULL);
        free_irq(irq_number2, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return PTR_ERR(task);
    }

    result = misc_register(&gpio_select_device);
    if (result) {
        printk(KERN_ERR "Failed to register device: %d\n", result);
        kthread_stop(task);
        free_irq(irq_number1, NULL);
        free_irq(irq_number2, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    printk(KERN_INFO "Button driver initialized\n");
    return 0;
}

static void __exit button_exit(void) {
    misc_deregister(&gpio_select_device);
    kthread_stop(task);
    free_irq(irq_number1, NULL);
    free_irq(irq_number2, NULL);
    gpiod_unexport(gpio_desc1);
    gpiod_unexport(gpio_desc2);
    gpio_free(GPIO1);
    gpio_free(GPIO2);
    printk(KERN_INFO "Button driver exited\n");
}

module_init(button_init);
module_exit(button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SistDeCompTP5");
MODULE_DESCRIPTION("A Button Driver for Raspberry Pi");
