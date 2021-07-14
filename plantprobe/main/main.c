#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_adc_cal.h"
#include "esp_console.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_VREF 1100 // Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES 1000 // Multisampling

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/

static uint8_t g_ssid[32] = "🐧";
static uint8_t g_pass[32] = "wewladddd";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAXIMUM_RETRY 3

#define CALIB_AIR 1450
#define CALIB_WATER 730

static EventGroupHandle_t s_wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  static int s_retry_num = 0;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

bool wifi_connect(void) {
  s_wifi_event_group = xEventGroupCreate();

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .pmf_cfg = {.capable = true, .required = false},
          },
  };

  memcpy(wifi_config.sta.ssid, g_ssid, 32);
  memcpy(wifi_config.sta.password, g_pass, 32);

  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(s_wifi_event_group);

  if (bits & WIFI_CONNECTED_BIT) {
    return true;
  } else if (bits & WIFI_FAIL_BIT) {
    return false;
  }

  return false;
}

void init_wifi(void) {
  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  assert(esp_netif_create_default_wifi_sta());

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
  ESP_ERROR_CHECK(esp_wifi_start());
}

esp_http_client_handle_t http_client;

void init_http(void) {
  esp_http_client_config_t config = {
      .url = "http://turb.io:9091/metrics/job/plant",
      .method = HTTP_METHOD_POST,
  };
  http_client = esp_http_client_init(&config);
}

void push_str(const char *body) {
  esp_http_client_set_post_field(http_client, body, strlen(body));

  esp_err_t err = esp_http_client_perform(http_client);

  if (err == ESP_OK) {
    printf("Status = %d, content_length = %d\n",
           esp_http_client_get_status_code(http_client),
           esp_http_client_get_content_length(http_client));
  }
}

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel =
    ADC_CHANNEL_6; // GPIO34 if ADC1, GPIO14 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;

static void print_char_val_type(esp_adc_cal_value_t val_type) {
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    printf("Characterized using Two Point Value\n");
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    printf("Characterized using eFuse Vref\n");
  } else {
    printf("Characterized using Default Vref\n");
  }
}

void app_main(void) {
  printf("init flash...\n");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  printf("init wifi...\n");

  init_wifi();

  printf("connecting to wifi...\n");
  wifi_connect();
  printf("connected!\n");

  init_http();

  // Configure ADC
  adc1_config_width(width);
  adc1_config_channel_atten(channel, atten);

  // Characterize ADC
  adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
      ADC_UNIT_1, atten, width, DEFAULT_VREF, adc_chars);
  print_char_val_type(val_type);

  uint16_t ticks = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    uint32_t adc_reading = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
      adc_reading += adc1_get_raw((adc1_channel_t)channel);
    }

    adc_reading /= NO_OF_SAMPLES;
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

    float moist = ((float)voltage - CALIB_AIR) / (CALIB_WATER - CALIB_AIR);

    printf("Raw: %d\tVoltage: %dmV %f\n", adc_reading, voltage, moist);

    char buf[300];
    snprintf(buf, sizeof(buf),
             "moisture_level %f\n"
             "plant_ticks %d\n",
             moist, ticks);

    push_str(buf);

    ticks++;
  }
}
