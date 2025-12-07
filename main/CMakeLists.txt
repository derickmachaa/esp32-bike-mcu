# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "app_main.c" "ble_client.c" "i2c.c" "braking_algrothim.c" "wifi_connect.c" "storage.c" "ota.c" "bmi_spi.c"  "udp_client.c" "udp_server.c" "storage.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
