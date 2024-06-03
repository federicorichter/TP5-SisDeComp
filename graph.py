import matplotlib.pyplot as plt
import matplotlib.animation as animation
import time
import tkinter as tk
from tkinter import ttk

# Define initial GPIO pin to read
BUTTON_GPIO = "538"

def read_button_state():
    try:
        with open(f"/sys/class/gpio/gpio{BUTTON_GPIO}/value", "r") as file:
            return int(file.read().strip())
    except FileNotFoundError:
        return 0  # Default state if file not found

def write_gpio_select(gpio_pin):
    with open("/dev/gpio_select", "w") as file:
        file.write(gpio_pin)

fig, ax = plt.subplots()
x_data, y_data = [], []
line, = ax.plot([], [], lw=2)
ax.set_ylim(-0.1, 1.1)
ax.set_xlim(0, 10)
ax.set_xlabel('Time (s)')
ax.set_ylabel('Button State')

def init():
    ax.set_xlim(0, 10)
    return line,

def update(frame):
    current_time = time.time() - start_time
    x_data.append(current_time)
    y_data.append(read_button_state())
    line.set_data(x_data, y_data)
    ax.set_xlim(0, max(10, current_time + 1))
    return line,

def set_gpio(gpio_pin):
    global BUTTON_GPIO
    BUTTON_GPIO = gpio_pin
    write_gpio_select(gpio_pin)
    x_data.clear()
    y_data.clear()
    start_time = time.time()

def start_animation():
    global start_time
    start_time = time.time()
    ani = animation.FuncAnimation(fig, update, init_func=init, blit=True, interval=200)
    plt.show()

# Create the main window
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
