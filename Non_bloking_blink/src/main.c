

#include <stdio.h>
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "driver/gpio.h"



#define PINRED 22
#define PINGREEN 23
#define PINPUSH 15


#define GPIO_OUTPUT_YELLOW    2
#define GPIO_OUTPUT_GREEN    23
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_YELLOW) | (1ULL<<GPIO_OUTPUT_GREEN))
#define GPIO_INPUT_PUSHBUTTON     GPIO_NUM_15
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_PUSHBUTTON) )

#define TIMER_DIVIDER         16        //  Hardware timer clock divider
#define TIMER_INTERVAL0_SEC   (0.5) // sample test interval for the first timer
//#define TIMER_INTERVAL0_SEC   (3.4179) // sample test interval for the first timer
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TEST_WITHOUT_RELOAD   0        // testing will be done without auto reload
#define TEST_WITH_RELOAD      1        // testing will be done with auto reload
#define SELECTED_TEST      TEST_WITHOUT_RELOAD        // testing will be done with auto reload
/*
 * A sample structure to pass events
 * from the timer interrupt handler to the main program.
 */
typedef struct {
    int type;  // the type of timer's event
    timer_group_t timer_group;
    timer_idx_t timer_idx;
    uint64_t timer_counter_value;
} timer_event_t;

xQueueHandle timer_queue;

/*
 * A simple helper function to print the raw timer counter value
 * and the counter value converted to seconds
 */
static void inline print_timer_counter(uint64_t counter_value)
{
    printf("Counter: 0x%08x%08x\n", (uint32_t) (counter_value >> 32),
                                    (uint32_t) (counter_value));
    printf("Time   : %.8f s\n", (double) counter_value / TIMER_SCALE);
}

/*
 * Timer group0 ISR handler
 *
 * Note:
 * We don't call the timer API here because they are not declared with IRAM_ATTR.
 * If we're okay with the timer irq not being serviced while SPI flash cache is disabled,
 * we can allocate this interrupt without the ESP_INTR_FLAG_IRAM flag and use the normal API.
 */
void IRAM_ATTR timer_group0_isr(void *para)
{
    
    int comodin=(int)para;
    timer_idx_t timer_idx = (timer_idx_t) comodin;

    /* Retrieve the interrupt status and the counter value
       from the timer that reported the interrupt */
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    TIMERG0.hw_timer[timer_idx].update = 1;
    uint64_t timer_counter_value = 
        ((uint64_t) TIMERG0.hw_timer[timer_idx].cnt_high) << 32
        | TIMERG0.hw_timer[timer_idx].cnt_low;

        
    /* Prepare basic event data
       that will be then sent back to the main program task */
    timer_event_t evt;
    evt.timer_group = TIMER_GROUP_0 ;
    evt.timer_idx = timer_idx;
    evt.timer_counter_value = timer_counter_value;

 /* Clear the interrupt
       and update the alarm time for the timer with without reload */
    if ((intr_status & BIT(timer_idx)) && timer_idx == TIMER_0) {
        evt.type = SELECTED_TEST;
        TIMERG0.int_clr_timers.t0 = 1;
        if(SELECTED_TEST==TEST_WITHOUT_RELOAD)
        {
            timer_counter_value += (uint64_t) (TIMER_INTERVAL0_SEC * TIMER_SCALE);
            TIMERG0.hw_timer[timer_idx].alarm_high = (uint32_t) (timer_counter_value >> 32);
            TIMERG0.hw_timer[timer_idx].alarm_low = (uint32_t) timer_counter_value;
        }
    } 
     else {
        evt.type = -1; // not supported even type
    }
    /* After the alarm has been triggered
      we need enable it again, so it is triggered the next time */
    TIMERG0.hw_timer[timer_idx].config.alarm_en = TIMER_ALARM_EN;

    /* Now just send the event data back to the main program task */
    xQueueSendFromISR(timer_queue, &evt, NULL);

}

/*
 * Initialize selected timer of the timer group 0
 *
 * timer_idx - the timer number to initialize
 * auto_reload - should the timer auto reload on alarm?
 * timer_interval_sec - the interval of alarm to set
 */
static void example_tg0_timer_init(timer_idx_t timer_idx, 
    bool auto_reload, double timer_interval_sec){
        /* Select and initialize basic parameters of the timer */
            timer_config_t config;
            config.divider = TIMER_DIVIDER;
            config.counter_dir = TIMER_COUNT_UP;
            config.counter_en = TIMER_PAUSE;
            config.alarm_en = TIMER_ALARM_EN;
            config.intr_type = TIMER_INTR_LEVEL;
            config.auto_reload = auto_reload;
            timer_init(TIMER_GROUP_0, timer_idx, &config);
        /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
            timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000ULL);

            /* Configure the alarm value and the interrupt on alarm. */
            timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_sec * TIMER_SCALE);
            timer_enable_intr(TIMER_GROUP_0, timer_idx);
            timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr, 
                (void *) timer_idx, ESP_INTR_FLAG_IRAM, NULL);

            timer_start(TIMER_GROUP_0, timer_idx);
    }


/*
 * The main task of this example program
 */
static void timer_example_evt_task(void *arg)
{

        int cnt=1;
    while (1) {
        timer_event_t evt;
        xQueueReceive(timer_queue, &evt, portMAX_DELAY);

        /* Print information that the timer reported an event */
        if (evt.type == TEST_WITHOUT_RELOAD) {
            printf("\n    Example timer without reload\n");
        } else if (evt.type == TEST_WITH_RELOAD) {
            printf("\n    Example timer with auto reload\n");
        } else {
            printf("\n    UNKNOWN EVENT TYPE\n");
        }
        printf("Group[%d], timer[%d] alarm event\n", evt.timer_group, evt.timer_idx);

        /* Print the timer values passed by event */
        printf("------- EVENT TIME --------\n");
        print_timer_counter(evt.timer_counter_value);

        /* Print the timer values as visible by this task */
        printf("-------- TASK TIME --------\n");
        uint64_t task_counter_value;
        timer_get_counter_value(evt.timer_group, evt.timer_idx, &task_counter_value);
        print_timer_counter(task_counter_value);

        //Invierte el valor de la salida
        cnt=(cnt==1)?0:1;
        gpio_set_level(GPIO_OUTPUT_GREEN, cnt );
    }
}

void app_main() {
    esp_err_t error;
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));
    example_tg0_timer_init(TIMER_0, SELECTED_TEST, TIMER_INTERVAL0_SEC);
    xTaskCreate(timer_example_evt_task, "timer_evt_task", 2048, NULL, 5, NULL);

     gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;//disable interrupt
    io_conf.mode = GPIO_MODE_OUTPUT;//set as output mode
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;//bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;//disable pull-down mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;//disable pull-up mode
    error=gpio_config(&io_conf);//configure GPIO with the given settings

    if(error!=ESP_OK){
        printf("error al configurar las salidas de LED\n");
    }

    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;//disable interrupt
    io_conf.mode = GPIO_MODE_INPUT;//set as input mode
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;//bit mask of the pins
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;//enable pull-down mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;//disable pull-up mode
    error=gpio_config(&io_conf);//configure GPIO with the given settings

    if(error!=ESP_OK){
        printf("error al configurar las entradas de LED\n");
    }
    while(1){
        int cnt =gpio_get_level(GPIO_INPUT_PUSHBUTTON);
        gpio_set_level(GPIO_OUTPUT_YELLOW, cnt );
    }
}