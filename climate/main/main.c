#include "argtable3/argtable3.h"
#include "cmd_system.h"
#include "driver/i2c.h"
#include "esp_console.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/

static gpio_num_t i2c_gpio_sda = 18;
static gpio_num_t i2c_gpio_scl = 19;

static uint32_t i2c_frequency = 10000;
static i2c_port_t i2c_port = I2C_NUM_0;

static esp_err_t i2c_master_driver_initialize(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = i2c_gpio_sda,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_io_num = i2c_gpio_scl,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = i2c_frequency,
      // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_*
      // flags to choose i2c source clock here. */
  };
  return i2c_param_config(i2c_port, &conf);
}

#define SCD30_ADDRESS 0x61
#define ESP_SLAVE_ADDR 0x61

uint8_t computeCRC8(uint8_t data[], uint8_t len) {
  uint8_t crc = 0xFF; // Init with 0xFF

  for (uint8_t x = 0; x < len; x++) {
    crc ^= data[x]; // XOR-in the next input byte

    for (uint8_t i = 0; i < 8; i++) {
      if ((crc & 0x80) != 0) {
        crc = (uint8_t)((crc << 1) ^ 0x31);
      } else {
        crc <<= 1;
      }
    }
  }

  return crc; // No output reflection
}

void sendCommand(uint16_t command, uint16_t arguments, bool arg) {
  printf("send_command... %x %x\n", command, arg ? arguments : 0);

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  ESP_ERROR_CHECK(i2c_master_start(cmd));

  // write enabled to 0x61
  ESP_ERROR_CHECK(i2c_master_write_byte(
      cmd, (ESP_SLAVE_ADDR << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN));

  // send command
  ESP_ERROR_CHECK(i2c_master_write_byte(cmd, command >> 8, ACK_CHECK_EN));
  ESP_ERROR_CHECK(i2c_master_write_byte(cmd, command & 0xff, ACK_CHECK_EN));

  if (arg) {
    uint8_t data[2];
    data[0] = arguments >> 8;
    data[1] = arguments & 0xFF;
    uint8_t crc =
        computeCRC8(data, 2); // Calc CRC on the arguments only, not the command

    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, arguments >> 8, ACK_CHECK_EN));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, arguments & 0xff, ACK_CHECK_EN));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, crc, ACK_CHECK_EN));
  }

  ESP_ERROR_CHECK(i2c_master_stop(cmd));

  esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
  if (ret == ESP_OK) {
    printf("Write OK\n");
  } else if (ret == ESP_ERR_TIMEOUT) {
    printf("bussy is busy\n");
  } else {
    printf("Write Failed aaaae\n");
  }
}

uint8_t ReadFromSCD30(uint16_t command, uint16_t *val, uint8_t cnt) {
  sendCommand(command, 0, false);

  printf("performing read... %x\n", command);

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  ESP_ERROR_CHECK(i2c_master_start(cmd));

  ESP_ERROR_CHECK(i2c_master_write_byte(
      cmd, (ESP_SLAVE_ADDR << 1) | I2C_MASTER_READ, ACK_CHECK_EN));

  uint8_t data[cnt * 3];
  ESP_ERROR_CHECK(i2c_master_read(cmd, data, cnt * 3, I2C_MASTER_LAST_NACK));

  ESP_ERROR_CHECK(i2c_master_stop(cmd));

  esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
  if (ret == ESP_OK) {
    printf("Write OK\n");
  } else if (ret == ESP_ERR_TIMEOUT) {
    printf("bussy is busy uwu\n");
  } else {
    printf("Write Failed aaaa\n");
  }

  for (uint8_t i = 0; i < cnt; i++) {
    val[i] = (data[i * 3] << 8) | (data[i * 3 + 1]);
  }

  return cnt;
}

#define COMMAND_CONTINUOUS_MEASUREMENT 0x0010
#define COMMAND_GET_DATA_READY 0x0202
#define COMMAND_SET_MEASUREMENT_INTERVAL 0x4600
#define COMMAND_AUTOMATIC_SELF_CALIBRATION 0x5306
#define COMMAND_READ_MEASUREMENT 0x0300

union b2f {
  uint16_t b[2];
  float f;
};

static uint8_t g_ssid[32] = "🐧";
static uint8_t g_pass[32] = "wewladddd";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define EXAMPLE_ESP_MAXIMUM_RETRY 3

static EventGroupHandle_t s_wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  static int s_retry_num = 0;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
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

esp_err_t _http_event_handle(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    printf("HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    printf("HTTP_EVENT_ON_CONNECTED\n");
    break;
  case HTTP_EVENT_HEADER_SENT:
    printf("HTTP_EVENT_HEADER_SENT\n");
    break;
  case HTTP_EVENT_ON_HEADER:
    printf("HTTP_EVENT_ON_HEADER\n");
    printf("%.*s", evt->data_len, (char *)evt->data);
    break;
  case HTTP_EVENT_ON_DATA:
    printf("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
    if (!esp_http_client_is_chunked_response(evt->client)) {
      printf("%.*s", evt->data_len, (char *)evt->data);
    }

    break;
  case HTTP_EVENT_ON_FINISH:
    printf("HTTP_EVENT_ON_FINISH\n");
    break;
  case HTTP_EVENT_DISCONNECTED:
    printf("HTTP_EVENT_DISCONNECTED\n");
    break;
  }

  return ESP_OK;
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

void push_str(const char *body) {
  esp_http_client_config_t config = {
      .url = "https://push.turb.io/metrics/job/co2_sensor",
      .method = HTTP_METHOD_POST,
  };

  esp_http_client_handle_t http_client = esp_http_client_init(&config);

  esp_http_client_set_post_field(http_client, body, strlen(body));

  esp_err_t err = esp_http_client_perform(http_client);

  esp_http_client_cleanup(http_client);

  if (err == ESP_OK) {
    printf("Status = %d, content_length = %d\n",
           esp_http_client_get_status_code(http_client),
           esp_http_client_get_content_length(http_client));
  } else {
    printf("forcing reboot, failed to push str\n");
    esp_restart();
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

  i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE,
                     I2C_MASTER_TX_BUF_DISABLE, 0);
  i2c_master_driver_initialize();

  // start measuring...
  uint16_t pressureOffset = 0;
  sendCommand(COMMAND_CONTINUOUS_MEASUREMENT, pressureOffset, false);

  sendCommand(COMMAND_SET_MEASUREMENT_INTERVAL, 2, true); // every 2s
  sendCommand(COMMAND_AUTOMATIC_SELF_CALIBRATION, 1,
              true); // enable self calibration

  uint16_t ticks = 0;

  for (;;) {
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    uint16_t rdy;
    ReadFromSCD30(COMMAND_GET_DATA_READY, &rdy, 1);

    if (!rdy) {
      printf("data not ready %d\n", rdy);
      continue;
    }

    uint16_t measure[6];
    ReadFromSCD30(COMMAND_READ_MEASUREMENT, &measure, 6);

    union b2f co2 = {.b = {measure[1], measure[0]}};
    union b2f temp = {.b = {measure[3], measure[2]}};
    union b2f hum = {.b = {measure[5], measure[4]}};

    // this happens sometimes xD
    if (co2.f <= 0 || temp.f <= 0 || humidity <= 0) {
      continue;
    }

    printf("co2?  %f\n", co2.f);
    printf("temp? %f\n", temp.f);
    printf("hume? %f\n", hum.f);

    char buf[300];
    snprintf(buf, sizeof(buf),
             "co2_ppm %f\n"
             "temperature %f\n"
             "humidity %f\n"
             "ticks %d\n",
             co2.f, temp.f, hum.f, ticks);

    push_str(buf);

    ticks++;
  }
}
