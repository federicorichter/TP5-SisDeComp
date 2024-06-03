#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#define GPIO1 538
#define GPIO2 539
#define DEVICE_NAME "gpio_select"
#define MAJOR_NUM 240
#define MINOR_NUM 0

static int irq_number1, irq_number2;
static int button_state = 0;
static int selected_gpio = GPIO1;  // GPIO predeterminado
static struct task_struct *task;
static struct gpio_desc *gpio_desc1, *gpio_desc2;
static dev_t dev_num;
static struct cdev gpio_select_cdev;

// ISR para manejar las interrupciones del botón
static irqreturn_t button_isr(int irq, void *data) {
    button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
    return IRQ_HANDLED;
}

// Hilo del kernel para leer el estado del botón periódicamente
static int button_thread(void *arg) {
    while (!kthread_should_stop()) {
        button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
        msleep(200);  // Esperar 200 ms
    }
    return 0;
}

// Función de escritura del dispositivo de caracteres para seleccionar el GPIO
static ssize_t gpio_select_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) {
    char kbuf[4];
    if (count > 3) return -EINVAL;  // Comprobar longitud máxima
    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    kbuf[count] = '\0';

    if (strcmp(kbuf, "538") == 0) {
        selected_gpio = GPIO1;
    } else if (strcmp(kbuf, "539") == 0) {
        selected_gpio = GPIO2;
    } else {
        return -EINVAL;  // Entrada no válida
    }
    return count;
}

// Estructura de operaciones de archivo para el dispositivo de caracteres
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = gpio_select_write,
};

static int __init button_init(void) {
    int result;

    // Obtener descriptores de GPIO
    gpio_desc1 = gpio_to_desc(GPIO1);
    gpio_desc2 = gpio_to_desc(GPIO2);
    if (!gpio_desc1 || !gpio_desc2) {
        printk(KERN_ERR "Invalid GPIO descriptor\n");
        return -ENODEV;
    }

    // Solicitar GPIO1
    result = gpio_request(GPIO1, "sysfs");
    if (result) {
        printk(KERN_ERR "Failed to request GPIO1: %d\n", result);
        return result;
    }

    // Solicitar GPIO2
    result = gpio_request(GPIO2, "sysfs");
    if (result) {
        printk(KERN_ERR "Failed to request GPIO2: %d\n", result);
        gpio_free(GPIO1);
        return result;
    }

    // Configurar GPIO1 como entrada
    result = gpiod_direction_input(gpio_desc1);
    if (result) {
        printk(KERN_ERR "Failed to set GPIO1 direction: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    // Configurar GPIO2 como entrada
    result = gpiod_direction_input(gpio_desc2);
    if (result) {
        printk(KERN_ERR "Failed to set GPIO2 direction: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    // Establecer debounce para evitar rebotes
    gpiod_set_debounce(gpio_desc1, 200);
    gpiod_set_debounce(gpio_desc2, 200);
    gpiod_export(gpio_desc1, false);
    gpiod_export(gpio_desc2, false);

    // Obtener números de IRQ para los GPIOs
    irq_number1 = gpiod_to_irq(gpio_desc1);
    irq_number2 = gpiod_to_irq(gpio_desc2);
    if (irq_number1 < 0 || irq_number2 < 0) {
        printk(KERN_ERR "Failed to get IRQ numbers\n");
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return -EINVAL;
    }

    // Solicitar IRQs
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

    // Crear hilo del kernel
    task = kthread_run(button_thread, NULL, "button_thread");
    if (IS_ERR(task)) {
        printk(KERN_ERR "Failed to create kernel thread: %ld\n", PTR_ERR(task));
        free_irq(irq_number1, NULL);
        free_irq(irq_number2, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return PTR_ERR(task);
    }

    // Asignar número mayor y menor para el dispositivo de caracteres
    dev_num = MKDEV(MAJOR_NUM, MINOR_NUM);
    result = register_chrdev_region(dev_num, 1, DEVICE_NAME);
    if (result < 0) {
        printk(KERN_ERR "Failed to register char device region: %d\n", result);
        kthread_stop(task);
        free_irq(irq_number1, NULL);
        free_irq(irq_number2, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    // Inicializar y agregar el dispositivo de caracteres
    cdev_init(&gpio_select_cdev, &fops);
    gpio_select_cdev.owner = THIS_MODULE;
    result = cdev_add(&gpio_select_cdev, dev_num, 1);
    if (result < 0) {
        printk(KERN_ERR "Failed to add char device: %d\n", result);
        unregister_chrdev_region(dev_num, 1);
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
    cdev_del(&gpio_select_cdev);
    unregister_chrdev_region(dev_num, 1);
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

