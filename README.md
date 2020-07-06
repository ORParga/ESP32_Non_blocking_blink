# ESP32_Non_blocking_blink
ESP example of how to use a Timer-Task combination to make a led blink without blocking ESP32.
The program is written for the ESP32 framework. Its main functions are:
xTaskCreate () for the interrupt watcher
timer_isr_register () and her friends to get a regular interval
xQueueSendFromISR () for communication between tasks
