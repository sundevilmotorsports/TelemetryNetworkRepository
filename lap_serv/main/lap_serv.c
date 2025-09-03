#include "esp_check.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <sys/socket.h>

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define EXAMPLE_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define EXAMPLE_GTK_REKEY_INTERVAL 0
#endif

#define UDP_PORT 3333

#define MAX_MESSAGE_LEN 128
#define QUEUE_SIZE 10

static const char *TAG = "wifi softAP";

//static httpd_req_t *sse_client_req = NULL;  // one client for now
static QueueHandle_t sse_queue;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
							   int32_t event_id, void* event_data)
{
	if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
		   MAC2STR(event->mac), event->aid);
	} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
		   MAC2STR(event->mac), event->aid, event->reason);
	}
}

void wifi_init_softap(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
													 ESP_EVENT_ANY_ID,
													 &wifi_event_handler,
													 NULL,
													 NULL));

	wifi_config_t wifi_config = {
		.ap = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
			.channel = EXAMPLE_ESP_WIFI_CHANNEL,
			.password = EXAMPLE_ESP_WIFI_PASS,
			.max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
			.authmode = WIFI_AUTH_WPA3_PSK,
			.sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
			#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
			.authmode = WIFI_AUTH_WPA2_PSK,
			#endif
			.pmf_cfg = {
				.required = true,
			},
#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
			.bss_max_idle_cfg = {
				.period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
				.protected_keep_alive = 1,
			},
			#endif
			.gtk_rekey_interval = EXAMPLE_GTK_REKEY_INTERVAL,
		},
	};
	if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
		  EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}


/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req)
{
	const char* resp_str = "<h1>Hello World ?</h1><p>call me goofy because i <b>ahh</b></p>";
	httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;

}


void print_client_ip(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
    socklen_t addr_size = sizeof(addr);
    
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(TAG, "Error getting client IP");
        return;
    }
    
    // Convert to IPv6 string
    //inet_ntop(AF_INET, &addr.sin6_addr, ipstr, sizeof(ipstr));
    //ESP_LOGI(TAG, "Client IP(6) => %s", ipstr);

	// Convert to IPv4 string
    inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr));
	//return *ipstr;
    ESP_LOGI(TAG, "Client IP(4) => %s", ipstr);

}

static esp_err_t sse_handler(httpd_req_t *req) {
	ESP_LOGW("SSE_HANDLER", "SSE client connected");
	print_client_ip(req);

	// Set the Content-Type for SSE
	httpd_resp_set_type(req, "text/event-stream");
	httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
	httpd_resp_set_hdr(req, "Connection", "keep-alive");
	char msg[MAX_MESSAGE_LEN];

	// keep sending from queue as long as client connected to this jaun
	while (1) {
		// NOTE: May have to look into the delay/timeout for disconnect
		if (xQueueReceive(sse_queue, &msg, portMAX_DELAY) == pdTRUE) {
			char buffer[MAX_MESSAGE_LEN + 16]; // 16 for sse overhead (data:...\n\n\0) and just extra
			snprintf(buffer, sizeof(buffer), "data: %s\n\n", msg);
			if (httpd_resp_send_chunk(req, buffer, strlen(buffer)) != ESP_OK) {
				ESP_LOGW("SSE_HANDLER", "Client disconnected");
				break;
			}
        }
    }

	// Loop to send data periodically
	//while (true) {
	//	char buffer[64];
	//	snprintf(buffer, sizeof(buffer), "data: Hello from ESP32 at %ld\n\n", esp_log_timestamp());
	//	esp_err_t ret = httpd_resp_send_chunk(req, buffer, strlen(buffer));

	//	if (ret != ESP_OK) {
	//		ESP_LOGE("SSE_HANDLER", "Error sending chunk: %d", ret);
	//		break; // Exit loop if the client disconnects
	//	}
	//	vTaskDelay(pdMS_TO_TICKS(1000)); // Send data every 1 second
	//}
	
    //ESP_LOGI(TAG, "Client IP(4) => %s", print_client_ip(&req));
	//ESP_LOGI("SSE_HANDLER", "SSE client disconnected");
	//print_client_ip(req);
	return ESP_OK;
}


httpd_handle_t start_webserver(){
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	//config.lru_purge_enable = true;

	if (httpd_start(&server, &config) == ESP_OK) {
		ESP_LOGI(TAG, "Server ok, registering the URI handlers...");
		
		// hello world page at root URI handler
		static const httpd_uri_t hello_world_uri= {
			.uri       = "/",
			.method    = HTTP_GET,
			.handler   = hello_get_handler,
			.user_ctx  = NULL
		};

		// Registers the SSE URI handler
		httpd_uri_t sse_uri = {
			.uri      = "/events",
			.method   = HTTP_GET,
			.handler  = sse_handler,
			.user_ctx = NULL
		};

		httpd_register_uri_handler(server, &hello_world_uri);
		httpd_register_uri_handler(server, &sse_uri);
		// To add additional route:
		// httpd_register_uri_handler(server, &handler_function);
		return server;
	}
	ESP_LOGI(TAG, "Error starting server");
	return NULL;
}


static void udp_server_task(void *pvParameters)
{
    char rx_buffer[MAX_MESSAGE_LEN];
    char addr_str[128];
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "UDP socket created");

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) { // error
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "UDP socket bound, port %d", UDP_PORT);

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        } else {
            rx_buffer[len] = 0;
            inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
            ESP_LOGI(TAG, "UDP recv from %s:%d: %s", addr_str, ntohs(source_addr.sin_port), rx_buffer);

            // push message to sse queue without blockage
			// NOTE: truncates message to MAX_MESSAGE_LEN length, so will show that -1 length of chars to client
            if (xQueueSend(sse_queue, rx_buffer, 0) != pdTRUE) {
                ESP_LOGW(TAG, "sse queue full, dropping message");
            }
        }
    }

    if (sock != -1) {
        ESP_LOGE(TAG, "Closing UDP socket");
        shutdown(sock, 0);
        close(sock);
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
	printf("=====================\nLAP_SERV....\n=====================");
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// create sse message queueueueu
    sse_queue = xQueueCreate(QUEUE_SIZE, MAX_MESSAGE_LEN);
    if (!sse_queue) {
        ESP_LOGE(TAG, "Failed to create SSE queue");
        return;
    }

	ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
	wifi_init_softap();

	httpd_handle_t server = start_webserver();
	xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
}
