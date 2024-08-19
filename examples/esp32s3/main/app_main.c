#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>
#include <sys/param.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "peer.h"

#define DEFAULT_SCAN_LIST_SIZE 20

static const char *TAG = "webrtc";

// Event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static bool ISH264 = true;
static TaskHandle_t xPcTaskHandle = NULL;
static TaskHandle_t xAudioTaskHandle = NULL;
static TaskHandle_t xVideoTaskHandle = NULL;
static TaskHandle_t xCameraTaskHandle = NULL;
static TaskHandle_t xPsTaskHandle = NULL;

extern esp_err_t audio_init();
extern esp_err_t video_init();
extern esp_err_t camera_init();
extern void audio_task(void *pvParameters);
extern void video_task(void *pvParameters);
extern void camera_task(void *pvParameters);

// Event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

PeerConnection *g_pc;
PeerConnectionState eState = PEER_CONNECTION_CLOSED;
int gDataChannelOpened = 0;

int64_t get_timestamp()
{

  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

static void oniceconnectionstatechange(PeerConnectionState state, void *user_data)
{

  ESP_LOGI(TAG, "PeerConnectionState: %d", state);
  eState = state;
  // not support datachannel close event
  if (eState != PEER_CONNECTION_COMPLETED)
  {
    gDataChannelOpened = 0;
  }
}

static void onmessasge(char *msg, size_t len, void *userdata)
{

  ESP_LOGI(TAG, "Datachannel message: %.*s, size", len, msg);
}

void onopen(void *userdata)
{

  ESP_LOGI(TAG, "Datachannel opened");
  gDataChannelOpened = 1;
}

static void onclose(void *userdata)
{
}

void peer_signaling_task(void *arg)
{

  ESP_LOGI(TAG, "peer_signaling_task started");

  for (;;)
  {

    peer_signaling_loop();

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void peer_connection_task(void *arg)
{

  ESP_LOGI(TAG, "peer_connection_task started");

  for (;;)
  {

    peer_connection_loop(g_pc);

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (s_retry_num < 5)
    {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    }
    else
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    char buf[16]; // Genügend Platz für die längste mögliche IPv4-Adresse
    ESP_LOGI(TAG, "got ip:%s", esp_ip4addr_ntoa(&event->ip_info.ip, buf, sizeof(buf)));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

const char *get_auth_mode_name(wifi_auth_mode_t auth_mode)
{
  switch (auth_mode)
  {
  case WIFI_AUTH_OPEN:
    return "WIFI_AUTH_OPEN";
  case WIFI_AUTH_WEP:
    return "WIFI_AUTH_WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WIFI_AUTH_WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WIFI_AUTH_WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WIFI_AUTH_WPA_WPA2_PSK";
  case WIFI_AUTH_WPA3_PSK:
    return "WIFI_AUTH_WPA3_PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WIFI_AUTH_WPA2_WPA3_PSK";
  default:
    return "UNKNOWN";
  }
}

void wifi_scan()
{
  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = true};

  ESP_LOGI(TAG, "Starting WiFi scan");

  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret == ESP_ERR_WIFI_STATE)
  {
    ESP_LOGW(TAG, "WiFi is in an invalid state to start scanning. Make sure WiFi is not connecting or already connected.");
    return;
  }
  else if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
    return;
  }

  uint16_t number = DEFAULT_SCAN_LIST_SIZE;
  wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
  uint16_t ap_count = 0;
  memset(ap_info, 0, sizeof(ap_info));

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
  ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
  for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++)
  {
    ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
    ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
    ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
    ESP_LOGI(TAG, "Authmode \t\t%s", get_auth_mode_name(ap_info[i].authmode));
  }
}

// Custom connect function
esp_err_t custom_connect()
{
  ESP_LOGI(TAG, "Custom connection function called.");

  // Initialize the TCP/IP stack
  ESP_ERROR_CHECK(esp_netif_init());

  // Create default WiFi station
  esp_netif_create_default_wifi_sta();

  // Initialize WiFi with default configuration
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Create the event group to handle WiFi events
  s_wifi_event_group = xEventGroupCreate();
  if (s_wifi_event_group == NULL)
  {
    ESP_LOGE(TAG, "Failed to create event group");
    return ESP_FAIL;
  }

  // Register event handler for WiFi and IP events
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &event_handler,
                                                      NULL,
                                                      NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &event_handler,
                                                      NULL,
                                                      NULL));

  // Configure the WiFi connection
  wifi_config_t wifi_config = {
      .sta = {
          //.ssid = "IPHT-Internet",
          //.password = "D3vk_?RkH!25nz", 
          
          .ssid = "Blynk", //"BenMur",         //
          .password = "12345678", //"MurBen3128", //
          
          
         /*.ssid = "BenMur",         //
          .password = "MurBen3128", //
          .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          */
          
      },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Scanning Wifi: %s", wifi_config.sta.ssid);
  ESP_LOGI(TAG, "Connecting to SSID: %s", wifi_config.sta.ssid);

  // Wait for the connection to establish
  esp_err_t ret = ESP_OK;
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT)
  {
    ESP_LOGI(TAG, "Connected to SSID: %s", wifi_config.sta.ssid);
  }
  else if (bits & WIFI_FAIL_BIT)
  {
    ESP_LOGI(TAG, "Failed to connect to SSID: %s", wifi_config.sta.ssid);
    ret = ESP_FAIL;
  }
  else
  {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    ret = ESP_FAIL;
  }

  // Cleanup event handlers
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
  vEventGroupDelete(s_wifi_event_group);

  return ret;
}

void app_main(void)
{
  static char deviceid[32] = {0};
  uint8_t mac[8] = {0};

  PeerConfiguration config = {
      .ice_servers = {
          {.urls = "stun:stun.l.google.com:19302"}},
      .datachannel = DATA_CHANNEL_BINARY,
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_H264
  };

  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
  esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
  esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
  esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
  esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

  wifi_scan();

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(mdns_init());
  ESP_ERROR_CHECK(custom_connect()); // Using custom_connect instead of example_connect

  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
  {
    sprintf(deviceid, "esp32-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device ID: %s", deviceid);
  }

  peer_init();
  if (ISH264)
  {
    audio_init();
    video_init();
  }
  else
  {
    camera_init();
  }

  g_pc = peer_connection_create(&config);
  peer_connection_oniceconnectionstatechange(g_pc, oniceconnectionstatechange);
  peer_connection_ondatachannel(g_pc, onmessasge, onopen, onclose);

  peer_signaling_join_channel(deviceid, g_pc);

  xTaskCreatePinnedToCore(peer_connection_task, "peer_connection", 8192, NULL, 10, &xPcTaskHandle, 1);
  xTaskCreatePinnedToCore(peer_signaling_task, "peer_signaling", 8192, NULL, 10, &xPsTaskHandle, 0);

  if (ISH264)
  {
    xTaskCreatePinnedToCore(audio_task, "audio", 20480, NULL, 5, &xAudioTaskHandle, 0);

    xTaskCreatePinnedToCore(video_task, "video", 10240, NULL, 6, &xVideoTaskHandle, 0);


  }
  else
  {
    xTaskCreatePinnedToCore(camera_task, "camera", 4096, NULL, 6, &xCameraTaskHandle, 0);
  }

  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "open https://sepfy.github.io/webrtc?deviceId=%s", deviceid);
  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());

  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
