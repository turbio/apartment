#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#define GPIO_DOOR_BUZZER 23

#define GPIO_DOOR_LOCK 25

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (
      event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    printf("got ip: %s\n", ip4addr_ntoa(&event->ip_info.ip));
  }
}

void app_main() {

  nvs_flash_init();

  tcpip_adapter_init();

  wifi_init_config_t wifi_init_conf = WIFI_INIT_CONFIG_DEFAULT();

  esp_wifi_init(&wifi_init_conf);

  esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);

  /*
  wifi_config_t wifi_config = {
      .sta = {.ssid = "asdf", .password = "asdf"},
  };
  */
  esp_wifi_set_mode(WIFI_MODE_STA);

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = "",
              .password = "",
              .scan_method = WIFI_ALL_CHANNEL_SCAN,
              .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
              .threshold.rssi = -127,
              .threshold.authmode = WIFI_AUTH_OPEN,
          },
  };
  esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);

  /*wifi_scan_config_t scan_config;*/
  esp_wifi_start();

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

      TickType_t msecs = ticks * 10;

      if (msecs >= 700 && msecs <= 5000) {
        printf("on for %dms, access granted!\n", msecs);

        printf("turning on door...\n");

        gpio_set_level(GPIO_DOOR_LOCK, 1);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_DOOR_LOCK, 0);

        printf("turning off door...\n");

      } else {
        printf("on for %dms, bad duration\n", msecs);
      }
    }
  }
}
