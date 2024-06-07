# Trabajo Práctico nro 5: Device Drivers

Luego de haber investigado en el trabajo anterior los módulos de kernel de Linux, ahora profundizaremos en los device drivers o device controllers, piezas de software que se ejecutan como 
parte del kernel del Sistema operativo y que se encarga de manejar los recursos de un determinado dispositivo (device) que suele ser externo a la computadora principal. En nuestro caso,
el trabajo consta escribir un driver capaz de leer dos señales en dos entradas de algún dispositivo y luego a nivel de usuario determinar qué entrada leer y graficarla en función del 
tiempo. Para nuestro trabajo utilizamos la Raspberry Pi Zero 2W que cuenta con una serie de GPIOs donde leeremos las señales generadas por dos pulsadores. Luego, se utiliza un script 
de Python que le pide al usuario cuál de los dos pines leer, este se lo comunica al driver a través de un archivo en el file system y comienza el gráfico.


## Device Driver

Para la implementación del driver se construyó un módulo de kernel utilizando la librería de Linux para lectura de GPIOs comenzando por obtener sus descriptores y setearlos como entrada. Estos descriptores son estructuras de datos utilizadas en Linux para facilitar la interacción de las aplicaciones con los pines proporcionando así un nivel de 
abstracción mayor. 

```c
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

```

Luego procedemos a setearle una función de debounce para evitar problemáticas de rebotes en los pines de entrada y exportamos los descriptores de los pines obtenidos anteriormente al espacio de usuario con la funcion gpiod_export. Hacemos uso de estos descriptores también para setear las interrupciones con sus correspondientes IRQ, siempre haciendo el handling de los posibles errores


```c
// Establecer debounce para evitar rebotes
    gpiod_set_debounce(gpio_desc1, 200);
    gpiod_set_debounce(gpio_desc2, 200);
    gpiod_export(gpio_desc1, false); //false porque es una entrada
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

```
Vemos aca que también se setean las rutinas de interrupción para los pines, la cual, dependiendo de una variables global llamada selected_gpio que determina qué pin se leerá, escribirá el estado leído de la entrada en el descriptor correspondiente. 

```c
static irqreturn_t button_isr(int irq, void *data) {
    button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
    return IRQ_HANDLED;
}
```

Luego creamos un thread que correrá la función capaz de leer el pin seleccionado por el usuario de manera periódica

```c
static int button_thread(void *arg) {
    while (!kthread_should_stop()) {
        button_state = gpiod_get_value(selected_gpio == GPIO1 ? gpio_desc1 : gpio_desc2);
        msleep(200);  // Esperar 200 ms
    }
    return 0;
}

```

Para finalizar este primer paso de inicialización designamos los números mayor y menor de nuestro nuevo driver, correspondientes al tipo de módulo y su "instancia", que luego utlizaremos para levantar el driver como un dispositivo de caracteres.

```c
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
```

Una de las funciones más importantes de todo el código es la que determina qué pin ha indicado del usuario que se debe leer, la cual lee de un archivo en el file system la entrada escrita por el usuario. En nuestro caso, los pines que usaremos son aquellos mapeados con los números 539 o 538, por lo que en base a esto se determina la variabe global selected_gpio.

```c
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
```
