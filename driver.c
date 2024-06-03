#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define BUTTON_GPIO 539  // Example GPIO pin number, change as necessary

static int irq_number;
static int button_state = 0;
static struct task_struct *task;
static struct gpio_desc *button_gpio_desc;

static irqreturn_t button_isr(int irq, void *data) {
    button_state = gpiod_get_value(button_gpio_desc);
    //printk(KERN_INFO "Button state: %d\n", button_state);
    return IRQ_HANDLED;
}

static int button_thread(void *arg) {
    while (!kthread_should_stop()) {
        button_state = gpiod_get_value(button_gpio_desc);
       // printk(KERN_INFO "Button state: %d\n", button_state);
        msleep(200);  // Sleep for 200ms
    }
    return 0;
}

static int __init button_init(void) {
    int result;

    // Request the GPIO and get its descriptor
    button_gpio_desc = gpio_to_desc(BUTTON_GPIO);
    if (!button_gpio_desc) {
        printk(KERN_ERR "Invalid GPIO descriptor\n");
        return -ENODEV;
    }

    result = gpio_request(BUTTON_GPIO, "sysfs");
    if (result) {
        printk(KERN_ERR "Failed to request GPIO: %d\n", result);
        return result;
    }

    result = gpiod_direction_input(button_gpio_desc);
    if (result) {
        printk(KERN_ERR "Failed to set GPIO direction: %d\n", result);
        gpiod_put(button_gpio_desc);
        return result;
    }

    gpiod_set_debounce(button_gpio_desc, 200);
    gpiod_export(button_gpio_desc, false);

    irq_number = gpiod_to_irq(button_gpio_desc);
    if (irq_number < 0) {
        printk(KERN_ERR "Failed to get IRQ number: %d\n", irq_number);
        gpiod_put(button_gpio_desc);
        return irq_number;
    }

    result = request_irq(irq_number, button_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "button_gpio_handler", NULL);
    if (result) {
        printk(KERN_ERR "Failed to request IRQ: %d\n", result);
        gpiod_put(button_gpio_desc);
        return result;
    }

    task = kthread_run(button_thread, NULL, "button_thread");
    if (IS_ERR(task)) {
        printk(KERN_ERR "Failed to create kernel thread: %ld\n", PTR_ERR(task));
        free_irq(irq_number, NULL);
        gpiod_put(button_gpio_desc);
        return PTR_ERR(task);
    }

    printk(KERN_INFO "Button driver initialized\n");
    return 0;
}

static void __exit button_exit(void) {
    kthread_stop(task);
    free_irq(irq_number, NULL);
    gpiod_unexport(button_gpio_desc);
    gpiod_put(button_gpio_desc);
    printk(KERN_INFO "Button driver exited\n");
}

module_init(button_init);
module_exit(button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A Button Driver for Raspberry Pi");