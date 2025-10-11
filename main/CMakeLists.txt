# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "wifi_connect.c" "storage.c" "ota.c" "app_main.c" "bmi_spi.c" "i2c.c" "udp_client.c" "udp_server.c" "storage.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
