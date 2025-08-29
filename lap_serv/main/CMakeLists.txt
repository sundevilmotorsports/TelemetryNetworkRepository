idf_component_register(SRCS "lap_serv.c"
					REQUIRES esp_wifi nvs_flash esp_http_server
                    INCLUDE_DIRS "."
				)
