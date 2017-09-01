// user_main.c
// Copyright 2017 Lukas Friedrichsen
// License: Modified BSD-License
//
// 2017-02-28

#include "ets_sys.h"
#include "mem.h"
#include "gpio.h"
#include "osapi.h"
#include "os_type.h"
#include "espconn.h"
#include "user_interface.h"
#include "../driver/ESP-I2C-OLED-SSD1306/include/ssd1306.h"
#include "../driver/ESP-I2C-OLED-SSD1306/include/i2c.h"
#include "../include/user_config.h"

// Initialize local copies of SSID and PASSWD
// NO CONNECTION TO THE ACCESS-POINT WILL BE POSSIBLE OTHERWISE!
const char ssid[32] = SSID;
const char passwd[64] = PASSWD;

// Set WiFI-scan-mode
static struct scan_config config = {(uint8_t*) &SSID, NULL, 0, 1};

os_event_t *task_queue;
os_timer_t blink_timer;

struct espconn* udp_socket = NULL;

// We'll have to use a string builder in form of os_sprintf(...) in the output
// task later on because of the data format in which data is communicated between
// tasks. Therefore we allocate a buffer here once to avoid having to do this on
// each call of the method anew.
char string_buffer[64]; // 128 bit screenwidth / (minimal width per character of
                        // 1 bit as well as 1 bit space between two characters)
                        // = 64 characters per line at max

char connection_status[64]; // Buffer to store the current connection-status;
                            // see string_buffer concerning the size

char msg[64]; // Buffer to store the received data; see string_buffer
              // concerning the size

// Prints the current connection-status into connection_status as long as no connection is
// established
uint8_t ICACHE_FLASH_ATTR connection_status_get(void) {
  if (wifi_station_get_connect_status() != STATION_GOT_IP) {
    if (wifi_station_get_connect_status() == STATION_CONNECTING) {
      os_sprintf(connection_status, "Connecting...");
      return STATION_CONNECTING;
    }
    else if (wifi_station_get_connect_status() == STATION_IDLE) {
      os_sprintf(connection_status, "Idle...");
      return STATION_IDLE;
    }
    else if (wifi_station_get_connect_status() == STATION_NO_AP_FOUND) {
      os_sprintf(connection_status, "AP not found!");
      return STATION_NO_AP_FOUND;
    }
    else if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD) {
      os_sprintf(connection_status, "Wrong password!");
      return STATION_WRONG_PASSWORD;
    }
    else if (wifi_station_get_connect_status() == STATION_CONNECT_FAIL) {
      os_sprintf(connection_status, "Connection failed!");
      return STATION_CONNECT_FAIL;
    }
  }
  else {
    return STATION_GOT_IP;
  }
}

// Measures the RSSI-value of the network currently connected to
void ICACHE_FLASH_ATTR measureRSSI_task(os_event_t *events) {
  sint8_t rssi = wifi_station_get_rssi();
  if (rssi != 31) { // 31 = failure; see documentation
    if (rssi > BLINK_THRESHOLD) {
      os_timer_arm(&blink_timer, 500, 1);
    }
    else {
      os_timer_disarm(&blink_timer);
    }
    system_os_post(TASK_PRIO_0, SIG_PRINT, rssi);
    }
  else {
    // Check if still connected to the AP since the RSSI-measurement failed
    connection_status_get();
    os_timer_disarm(&blink_timer);
    gpio_output_set(BIT2, 0, BIT2, 0);
    system_os_post(TASK_PRIO_0, SIG_PRINT, 0);
  }
}

// Prints the received message to the given data sink
void ICACHE_FLASH_ATTR output_task(os_event_t *events) {
  os_sprintf(string_buffer, (char *) "RSSI:   %d", (sint8) events->par);
  switch (events->sig) {
    case SIG_PRINT:
      //os_printf("RSSI of SSID %s:   %d\n", SSID, (sint8) events->par);
      ssd1306_clear(1);
      ssd1306_draw_string(1, 0, 0, (char *) SSID, SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT);
      if ((sint8_t) events->par) {
        ssd1306_draw_string(1, 0, 12, (char *) "Status:   OK", SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT);
      }
      else {
        ssd1306_draw_string(1, 0, 12, (char *) "Status:   Error", SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT);
      }
      ssd1306_draw_string(1, 0, 24, string_buffer, SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT);
      ssd1306_draw_string(1, 0, 36, connection_status, SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT);
      if (wifi_station_get_connect_status() == STATION_GOT_IP) {
        ssd1306_draw_string(1, 0, 48, msg, SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT);
      }
      ssd1306_refresh(1, 1);
    default:
      break;
  }
  system_os_post(TASK_PRIO_1, SIG_RUN, 0);
}

// Flashes the onboard-LED
void ICACHE_FLASH_ATTR blink_timerfunc(void *arg) {
  if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT2) {
    // Set gpio high
    gpio_output_set(0, BIT2, BIT2, 0);
  }
  else {
    // Set gpio low
    gpio_output_set(BIT2, 0, BIT2, 0);
  }
}

// Prints the message to the LCD-screen and to the serial port if a UDP-
// datagram is received
void ICACHE_FLASH_ATTR udp_recv_cb(void *arg, char *data, unsigned short len) {
  os_sprintf(msg, (char *) "Received: %d\n", *data);
  os_printf(msg);
}

// Initialize UDP-socket
void ICACHE_FLASH_ATTR udp_init(void) {
  // Set up udp-socket-configuration
  udp_socket = (struct espconn *) os_zalloc(sizeof(struct espconn));
  udp_socket->type = ESPCONN_UDP;
  udp_socket->state = ESPCONN_NONE;
  udp_socket->proto.udp = (esp_udp *) os_zalloc(sizeof(esp_udp));
  udp_socket->proto.udp->local_port = LOCAL_PORT;

  // Create UDP-socket and register sent-callback
  espconn_create(udp_socket);
  espconn_regist_recvcb(udp_socket, udp_recv_cb);
}

// Wifi-event-callback; prints the current connection-status and initializes
// the UDP-socket on a successfully established connection
void ICACHE_FLASH_ATTR wifi_event_cb(System_Event_t *evt) {
  os_printf("WiFi-event: %x\n", evt->event);
  switch (evt->event) {
    case EVENT_STAMODE_GOT_IP:
      os_printf("Got IP-address!\n");
      os_sprintf(connection_status, (char *) "Got IP-address!\n");
      udp_init();
      break;
    case EVENT_STAMODE_CONNECTED:
      os_printf("Connected!\n");
      os_sprintf(connection_status, (char *) "Connected!\n");
      break;
    default:
      break;
  }
}

// Initialize WiFi-station-mode and go into sleep-mode until a connection is
// established; periodically (once per second) retries if no connection is
// established yet
void ICACHE_FLASH_ATTR wifi_init() {
  // Clear possible connections before trying to set up a new connection
  wifi_station_disconnect();

  // Set up station-configuration
  struct station_config station_conf;
  os_memcpy(&station_conf.ssid, ssid, 32);
  os_memcpy(&station_conf.password, passwd, 64);
  station_conf.bssid_set = 0; // No need to check for the MAC-adress of the AP here

  // Set station-mode, load station-configuration and configure reconnect-policy
  // Restart the system, if one of the above fails!
  if (!wifi_set_opmode(STATION_MODE) || !wifi_station_set_config_current(&station_conf) || !wifi_station_set_auto_connect(true) || !wifi_station_set_reconnect_policy(true)) {
    os_printf("Error while initializing station-mode! Rebooting...\n");
    system_restart();
  }

  // Set wifi-event-callback
  wifi_set_event_handler_cb(wifi_event_cb);

  // Sleep until a wifi-event occures
  wifi_set_sleep_type(MODEM_SLEEP_T);
}

// Initializiation
void ICACHE_FLASH_ATTR user_init() {
  os_printf("Initializing...\n");

  // Malloc message queue
  task_queue = (os_event_t *) os_malloc(sizeof(os_event_t)*TASK_QUEUE_LENGTH);

  // Initialize gpio, i2c and WiFi
  gpio_init();
  i2c_init();
  wifi_init();

  // Initialize display
  ssd1306_init(1);
  ssd1306_clear(1);
  ssd1306_select_font(1, 1);

  // Set GPIO2 (onboard-LED) to output mode
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
  gpio_output_set(BIT2, 0, BIT2, 0);

  os_printf("Setting callback for the timer...\n");

  // Set up timer
  os_timer_disarm(&blink_timer);
  os_timer_setfn(&blink_timer, (os_timer_func_t *) blink_timerfunc, NULL);

  os_printf("Registering tasks...\n");

  // Register tasks
  system_os_task(measureRSSI_task, TASK_PRIO_1, task_queue, TASK_QUEUE_LENGTH);
  system_os_task(output_task, TASK_PRIO_0, task_queue, TASK_QUEUE_LENGTH);

  // Start the tasks
  system_os_post(TASK_PRIO_1, SIG_RUN, 0);
}

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABBBCDDD
 *                A : rf cal
 *                B : at parameters
 *                C : rf init data
 *                D : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void) {
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
      case FLASH_SIZE_4M_MAP_256_256:
        rf_cal_sec = 128 - 8;
        break;

      case FLASH_SIZE_8M_MAP_512_512:
        rf_cal_sec = 256 - 5;
        break;

      case FLASH_SIZE_16M_MAP_512_512:
      case FLASH_SIZE_16M_MAP_1024_1024:
        rf_cal_sec = 512 - 5;
        break;

      case FLASH_SIZE_32M_MAP_512_512:
      case FLASH_SIZE_32M_MAP_1024_1024:
        rf_cal_sec = 1024 - 5;
        break;

      default:
        rf_cal_sec = 0;
        break;
    }

    return rf_cal_sec;
}

void ICACHE_FLASH_ATTR user_rf_pre_init(void) {
  // Nothing to do...
}
