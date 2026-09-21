#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "mcp_server.h"

#ifndef WIFI_SSID
#error WIFI_SSID must be defined
#endif
#ifndef WIFI_PASSWORD
#error WIFI_PASSWORD must be defined
#endif

MCPServer mcp("pico_w_mcp");

static bool setRelay(MCPArgs &args, char *out, size_t out_size, void *) {
    bool state = args.getBool("state");
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, state);
    snprintf(out, out_size, "Relay is %s", state ? "ON" : "OFF");
    return true;
}

static bool readTemperature(char *out, size_t out_size, void *) {
    snprintf(out, out_size, "23.5 degrees C");
    return true;
}

int main() {
    stdio_init_all();
    sleep_ms(1500);

    printf("\npico_w_mcp C++ example\n");
    printf("----------------------\n");

    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
    int rc = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);

    if (rc) {
        printf("Wi-Fi connection failed: %d\n", rc);
        return 1;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    printf("Connected. Pico IP: %s\n",
           ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));

    // This is all the application developer needs for MCP.
    mcp.tool(
        "set_relay",
        "Turn the Pico W built-in LED on or off.",
        setRelay
    ).param<bool>(
        "state",
        "True turns the relay on; false turns it off."
    );

    mcp.resource(
        "sensor://temperature",
        "temperature",
        "Current temperature in degrees Celsius.",
        readTemperature
    );

    cyw43_arch_lwip_begin();
    bool ok = mcp.begin(8000);
    cyw43_arch_lwip_end();

    if (!ok) {
        printf("Failed to start MCP server\n");
        return 1;
    }

    printf("MCP ready at http://<PICO-IP>:8000/mcp\n\n");

    while (true) {
        sleep_ms(1000);
    }
}
