import time

GPIO1 = "539"
GPIO2 = "540"

def read_button_state(gpio_number):
    with open(f"/sys/class/gpio/gpio{gpio_number}/value", "r") as file:
        return int(file.read().strip())

def select_gpio():
    while True:
        selection = input(f"Select GPIO pin ({GPIO1}/{GPIO2}): ").strip()
        if selection in [GPIO1, GPIO2]:
            return selection
        print(f"Invalid selection. Please choose {GPIO1} or {GPIO2}.")

def main():
    selected_gpio = select_gpio()
    print(f"Reading from GPIO {selected_gpio}...")

    try:
        while True:
            state = read_button_state(selected_gpio)
            print(f"GPIO {selected_gpio} state: {state}")
            time.sleep(0.2)
    except KeyboardInterrupt:
        print("Terminating the script.")

if __name__ == "__main__":
    main()
