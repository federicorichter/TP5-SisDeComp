#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define BUTTON_GPIO 17  // Example GPIO pin number, change as necessary

static int irq_number;
static int button_state = 0;
static struct task_struct *task;

static irqreturn_t button_isr(int irq, void *data) {
    button_state = gpio_get_value(BUTTON_GPIO);
    printk(KERN_INFO "Button state: %d\n", button_state);
    return IRQ_HANDLED;
}

static int button_thread(void *arg) {
    while (!kthread_should_stop()) {
        button_state = gpio_get_value(BUTTON_GPIO);
        printk(KERN_INFO "Button state: %d\n", button_state);
        msleep(200);  // Sleep for 200ms
    }
    return 0;
}

static int __init button_init(void) {
    int result;

    if (!gpio_is_valid(BUTTON_GPIO)) {
        printk(KERN_INFO "Invalid GPIO\n");
        return -ENODEV;
    }

    gpio_request(BUTTON_GPIO, "sysfs");
    gpio_direction_input(BUTTON_GPIO);
    gpio_set_debounce(BUTTON_GPIO, 200);
    gpio_export(BUTTON_GPIO, false);

    irq_number = gpio_to_irq(BUTTON_GPIO);
    result = request_irq(irq_number, button_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "button_gpio_handler", NULL);

    if (result) {
        printk(KERN_INFO "Failed to request IRQ\n");
        return result;
    }

    task = kthread_run(button_thread, NULL, "button_thread");
    if (IS_ERR(task)) {
        printk(KERN_INFO "Failed to create kernel thread\n");
        return PTR_ERR(task);
    }

    printk(KERN_INFO "Button driver initialized\n");
    return 0;
}

static void __exit button_exit(void) {
    kthread_stop(task);
    free_irq(irq_number, NULL);
    gpio_unexport(BUTTON_GPIO);
    gpio_free(BUTTON_GPIO);
    printk(KERN_INFO "Button driver exited\n");
}

module_init(button_init);
module_exit(button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A Button Driver for Raspberry Pi");
