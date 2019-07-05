#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define GPIO_DOOR_BUZZER 23

#define GPIO_DOOR_LOCK 25

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void app_main() {
  gpio_set_direction(GPIO_DOOR_LOCK, GPIO_MODE_DEF_OUTPUT);
  gpio_set_level(GPIO_DOOR_LOCK, 0);

  gpio_set_direction(GPIO_DOOR_BUZZER, GPIO_MODE_DEF_INPUT);

  gpio_set_intr_type(GPIO_DOOR_BUZZER, GPIO_INTR_ANYEDGE);

  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

  gpio_install_isr_service(0);

  gpio_isr_handler_add(
      GPIO_DOOR_BUZZER, gpio_isr_handler, (void *)GPIO_DOOR_BUZZER);

  printf("up!\n");

  TickType_t push_at = 0;
  int last_v = 0;

  uint32_t io_num;
  for (;;) {
    if (!xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      continue;
    }

    uint32_t l = gpio_get_level(GPIO_DOOR_BUZZER);

    if (last_v == l) {
      continue;
    }

    last_v = l;

    if (l) {
      push_at = xTaskGetTickCount();
    } else if (push_at) {
      TickType_t ticks = xTaskGetTickCount() - push_at;
      push_at = 0;

      TickType_t secs = (ticks / portTICK_PERIOD_MS) / 10;

      if (secs >= 2 && secs <= 5) {
        printf("on for %ds, access granted!\n", secs);

        printf("turning on door...\n");

        gpio_set_level(GPIO_DOOR_LOCK, 1);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_DOOR_LOCK, 0);

        printf("turning off door...\n");

      } else {
        printf("on for %ds, bad duration\n", secs);
      }
    }
  }
}
