# Trabajo Práctico nro 5: Device Drivers

Luego de haber investigado en el trabajo anterior los módulos de kernel de Linux, ahora profundizaremos en los device drivers o device controllers, piezas de software que se ejecutan como 
parte del kernel del Sistema operativo y que se encarga de manejar los recursos de un determinado dispositivo (device) que suele ser externo a la computadora principal. En nuestro caso,
el trabajo consta escribir un driver capaz de leer dos señales en dos entradas de algún dispositivo y luego a nivel de usuario determinar qué entrada leer y graficarla en función del 
tiempo. Para nuestro trabajo utilizamos la Raspberry Pi Zero 2W que cuenta con una serie de GPIOs donde leeremos las señales generadas por dos pulsadores. Luego, se utiliza un script 
de Python que le pide al usuario cuál de los dos pines leer, este se lo comunica al driver a través de un archivo en el file system y comienza el gráfico.


## Device Driver

Para la implementación del driver se construyó un módulo de kernel utilizando la librería de Linux para lectura de GPIOs comenzando por obtener sus descriptores y setearlos como entrada:

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

Luego procedemos a setearle una función de debounce para evitar problemáticas de rebotes en los pines de entrada y exportamos los descriptores de los pines obtenidos anteriormente 
para poder setear también 
