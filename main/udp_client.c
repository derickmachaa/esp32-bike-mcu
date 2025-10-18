#include <string.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "esp_log.h"

#define HOST_IP_ADDR "192.168.43.92"
#define PORT 3337

static const char *TAG = "BMCU_UDP";

static int udp_sock = -1;
static struct sockaddr_in dest_addr;

void udp_socket_init(void)
{
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock < 0) {
     //   ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }

    // optional timeout
    struct timeval timeout = {
        .tv_sec = 5,
        .tv_usec = 0,
    };
    //setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  //  ESP_LOGI(TAG, "UDP socket ready for %s:%d", HOST_IP_ADDR, PORT);
}

void udp_send_data(const char *data)
{
    if (udp_sock < 0) {
       // ESP_LOGE(TAG, "Socket not initialized reinit");
        udp_socket_init();
        return;
    }

    int err = sendto(udp_sock, data, strlen(data), 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (err < 0) {
       // ESP_LOGE(TAG, "UDP send error: errno %d", errno);
    }
}

void udp_socket_close(void)
{
    if (udp_sock != -1) {
        close(udp_sock);
        udp_sock = -1;
        ESP_LOGI(TAG, "UDP socket closed");
    }
}
