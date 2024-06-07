#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/uaccess.h>

/**
 * Archivo: driver_sdc.c
 * Descripción: Este archivo contiene la definición de constantes utilizadas en el controlador de GPIO.
 *
 * Constantes:
 * - GPIO1: Valor de la primera GPIO.
 * - GPIO2: Valor de la segunda GPIO.
 * - DEVICE_NAME: Nombre del dispositivo.
 */
#define GPIO1 538
#define GPIO2 539
#define DEVICE_NAME "gpio_select"

/**
 * @brief Declaración de variables estáticas utilizadas en el controlador del dispositivo SDC.
 *
 * Este archivo contiene la declaración de varias variables estáticas utilizadas en el controlador del dispositivo SDC.
 * Estas variables incluyen el número de interrupción, el estado del botón, el GPIO seleccionado, la estructura de
 * tarea, la descripción del GPIO y la estructura del dispositivo de caracteres.
 */

static int irq_number1, irq_number2;              /**< Número de interrupción para los botones */
static int button_state = 0;                      /**< Estado actual del botón */
static int selected_gpio = GPIO1;                 /**< GPIO seleccionado predeterminado */
static struct task_struct *task;                  /**< Estructura de tarea para el controlador */
static struct gpio_desc *gpio_desc1, *gpio_desc2; /**< Descripción del GPIO para los botones */
static dev_t dev_num;                             /**< Número de dispositivo */
static struct cdev gpio_select_cdev;              /**< Estructura del dispositivo de caracteres */

/**
 * ISR para manejar las interrupciones del botón.
 *
 * Esta función se ejecuta cuando se produce una interrupción en el botón.
 * Obtiene el valor del botón y lo guarda en la variable button_state.
 *
 * @param irq El número de la interrupción.
 * @param data Puntero a los datos adicionales pasados a la función de interrupción.
 * @return El valor de retorno de la función de interrupción.
 */
static irqreturn_t button_isr(int irq, void *data)
{
    button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
    return IRQ_HANDLED;
}

/**
 * Hilo del kernel para leer el estado del botón periódicamente.
 *
 * Este hilo se encarga de leer el estado del botón seleccionado cada 200 ms.
 * Utiliza la función gpiod_get_value() para obtener el valor del botón.
 *
 * @param arg Puntero a los argumentos del hilo (no utilizado en este caso).
 * @return 0 si el hilo finaliza correctamente.
 */
static int button_thread(void *arg)
{
    while (!kthread_should_stop())
    {
        button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
        msleep(200); // Esperar 200 ms
    }
    return 0;
}

/**
 * Función de escritura del dispositivo de caracteres para seleccionar el GPIO.
 * 
 * Esta función se encarga de seleccionar el GPIO según el valor recibido en el búfer.
 * 
 * @param file Puntero al archivo del dispositivo.
 * @param buf Puntero al búfer que contiene los datos a escribir.
 * @param count Tamaño de los datos a escribir.
 * @param pos Puntero a la posición actual en el archivo.
 * @return ssize_t Número de bytes escritos o un código de error en caso de fallo.
 */
static ssize_t gpio_select_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    char kbuf[4];
    if (count > 3)
        return -EINVAL; // Comprobar longitud máxima
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;
    kbuf[count] = '\0';

    if (strcmp(kbuf, "538") == 0)
    {
        selected_gpio = GPIO1;
    }
    else if (strcmp(kbuf, "539") == 0)
    {
        selected_gpio = GPIO2;
    }
    else
    {
        return -EINVAL; // Entrada no válida
    }
    return count;
}

/**
 * Estructura de operaciones de archivo para el dispositivo de caracteres.
 * Esta estructura define las operaciones de archivo que se pueden realizar en el dispositivo de caracteres.
 * Incluye una función de escritura (write) que está asociada a la función gpio_select_write.
 */
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = gpio_select_write,
};

/**
 * @brief Inicializa el controlador del botón.
 *
 * Esta función se llama durante la inicialización del módulo del kernel para configurar y
 * solicitar los recursos necesarios para el controlador del botón. Realiza las siguientes tareas:
 * - Obtiene los descriptores de GPIO para los pines GPIO1 y GPIO2.
 * - Solicita los pines GPIO1 y GPIO2.
 * - Configura los pines GPIO1 y GPIO2 como entradas.
 * - Establece el debounce para evitar rebotes en los pines GPIO1 y GPIO2.
 * - Exporta los pines GPIO1 y GPIO2.
 * - Obtiene los números de IRQ para los pines GPIO1 y GPIO2.
 * - Solicita las IRQ para los pines GPIO1 y GPIO2.
 * - Crea un hilo del kernel para manejar las interrupciones de los botones.
 * - Asigna un número mayor y menor para el dispositivo de caracteres de forma dinamica segun disponibilidad.
 * - Registra el rango de números de dispositivo de caracteres.
 * - Inicializa y agrega el dispositivo de caracteres.
 *
 * @return 0 si la inicialización es exitosa, un valor negativo en caso de error.
 */
static int __init button_init(void)
{
    int result;

    // Obtener descriptores de GPIO
    gpio_desc1 = gpio_to_desc(GPIO1);
    gpio_desc2 = gpio_to_desc(GPIO2);
    if (!gpio_desc1 || !gpio_desc2)
    {
        printk(KERN_ERR "Invalid GPIO descriptor\n");
        return -ENODEV;
    }

    // Solicitar GPIO1
    result = gpio_request(GPIO1, "sysfs");
    if (result)
    {
        printk(KERN_ERR "Failed to request GPIO1: %d\n", result);
        return result;
    }

    // Solicitar GPIO2
    result = gpio_request(GPIO2, "sysfs");
    if (result)
    {
        printk(KERN_ERR "Failed to request GPIO2: %d\n", result);
        gpio_free(GPIO1);
        return result;
    }

    // Configurar GPIO1 como entrada
    result = gpiod_direction_input(gpio_desc1);
    if (result)
    {
        printk(KERN_ERR "Failed to set GPIO1 direction: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    // Configurar GPIO2 como entrada
    result = gpiod_direction_input(gpio_desc2);
    if (result)
    {
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
    if (irq_number1 < 0 || irq_number2 < 0)
    {
        printk(KERN_ERR "Failed to get IRQ numbers\n");
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return -EINVAL;
    }

    // Solicitar IRQs
    result =
        request_irq(irq_number1, button_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "button_gpio1_handler", NULL);
    if (result)
    {
        printk(KERN_ERR "Failed to request IRQ1: %d\n", result);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    result =
        request_irq(irq_number2, button_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "button_gpio2_handler", NULL);
    if (result)
    {
        printk(KERN_ERR "Failed to request IRQ2: %d\n", result);
        free_irq(irq_number1, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }

    // Crear hilo del kernel
    task = kthread_run(button_thread, NULL, "button_thread");
    if (IS_ERR(task))
    {
        printk(KERN_ERR "Failed to create kernel thread: %ld\n", PTR_ERR(task));
        free_irq(irq_number1, NULL);
        free_irq(irq_number2, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return PTR_ERR(task);
    }

    // Registra una región de dispositivos de caracteres utilizando la función alloc_chrdev_region.
    // Esta función asigna un número de dispositivo mayor y menor para el controlador de dispositivo.
    // Si la asignación es exitosa, se guarda el número de dispositivo mayor y menor en las variables major y minor respectivamente.
    // Si la asignación falla, se imprime un mensaje de error y se realiza la limpieza necesaria antes de devolver el resultado negativo.
    result = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (result < 0)
    {
        printk(KERN_ERR "Failed to register char device region: %d\n", result);
        kthread_stop(task);
        free_irq(irq_number1, NULL);
        free_irq(irq_number2, NULL);
        gpio_free(GPIO1);
        gpio_free(GPIO2);
        return result;
    }
    int major = MAJOR(dev_num);
    int minor = MINOR(dev_num);

    // Inicializar y agregar el dispositivo de caracteres
    cdev_init(&gpio_select_cdev, &fops);
    gpio_select_cdev.owner = THIS_MODULE;
    result = cdev_add(&gpio_select_cdev, dev_num, 1);
    if (result < 0)
    {
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

/**
 * @brief Función para liberar los recursos utilizados por el controlador de botones.
 *
 * Esta función se encarga de liberar los recursos utilizados por el controlador de botones.
 * Realiza las siguientes acciones:
 * - Elimina el dispositivo de caracteres asociado al controlador.
 * - Desregistra la región de números de dispositivo asignada.
 * - Detiene la tarea del kernel asociada al controlador.
 * - Libera las interrupciones asignadas a los botones.
 * - Desexporta los GPIO asociados a los botones.
 * - Libera los GPIO utilizados por los botones.
 * - Imprime un mensaje informativo en el kernel indicando que el controlador de botones ha sido cerrado.
 */
static void __exit button_exit(void)
{
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
