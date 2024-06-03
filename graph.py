import matplotlib.pyplot as plt
import matplotlib.animation as animation
import time

BUTTON_GPIO = "539"

def read_button_state():
    with open(f"/sys/class/gpio/gpio{BUTTON_GPIO}/value", "r") as file:
        return int(file.read().strip())

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

start_time = time.time()
ani = animation.FuncAnimation(fig, update, init_func=init, blit=True, interval=200)
plt.show()
