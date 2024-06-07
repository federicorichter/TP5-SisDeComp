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

## Graficador

A nivel de usuario era requerido determinar qué pin se lee y graficarlo, por lo que se diseño un script en Python utilizando Matplotlib para la graficación y tkinter para la interfaz gráfica donde el usuario de la aplicación selecciona uno de los dos pines disponibles.

![image](https://github.com/federicorichter/TP5-SisDeComp/assets/82000054/3f1eef3a-41b5-43bc-b90a-f0a825248ef0)

Utilizando la librería tkinter creamos los botones y la pantalla, cuando uno de los botones correspondientes a los pines es presionado, se llama a la función set_gpio, la cual a su vez llama a write_gpio_select que escribe en el file system el archivo donde se indica al módulo qué pin leer.

```python
def write_gpio_select(gpio_pin):
    with open("/dev/gpio_select", "w") as file:
        file.write(gpio_pin)
def set_gpio(gpio_pin):
    global BUTTON_GPIO
    BUTTON_GPIO = gpio_pin
    write_gpio_select(gpio_pin)
    x_data.clear()
    y_data.clear()
    start_time = time.time()

```

Luego que el botón de Start Plot es presionado se comenzará a dibujar el gráfico utilizando los datos leídos por la función read_button_state y actualizándose de manera paulatina gracias a la función update que llama a read_button_state periódicamente.

```python
def read_button_state():
    try:
        with open(f"/sys/class/gpio/gpio{BUTTON_GPIO}/value", "r") as file:
            return int(file.read().strip())
    except FileNotFoundError:
        return 0  # Default state if file not found

def update(frame):
    current_time = time.time() - start_time
    x_data.append(current_time)
    y_data.append(read_button_state())
    line.set_data(x_data, y_data)
    ax.set_xlim(0, max(10, current_time + 1))
    return line,
def start_animation():
    global start_time
    start_time = time.time()
    ani = animation.FuncAnimation(fig, update, init_func=init, blit=True, interval=200)
    plt.show()

root = tk.Tk()
root.title("GPIO Button Selector")

# Create buttons for selecting GPIO pins
button_1 = ttk.Button(root, text="GPIO 538", command=lambda: set_gpio("538"))
button_2 = ttk.Button(root, text="GPIO 539", command=lambda: set_gpio("539"))

button_1.pack(side=tk.LEFT, padx=10, pady=10)
button_2.pack(side=tk.LEFT, padx=10, pady=10)

# Create a button to start the animation
start_button = ttk.Button(root, text="Start Plot", command=start_animation)
start_button.pack(side=tk.LEFT, padx=10, pady=10)

# Run the GUI event loop
root.mainloop()

```

## Video demostración

Debido a que la placa se reiniciaba cada vez que intentabamos descargar la librería para graficar, creamos otro script llamado nograph.py que solo imprime las lecturas en la terminal:

[https://github.com/federicorichter/TP5-SisDeComp/blob/master/WhatsApp%20Video%202024-06-03%20at%2017.08.28.mp4](https://drive.google.com/drive/folders/1SHNGwAcvtljjAn3TY2y2D7YUtF-i4ulp?usp=drive_link)

