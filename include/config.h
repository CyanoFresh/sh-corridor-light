#ifndef CORRIDOR_LIGHT_CONFIG_H
#define CORRIDOR_LIGHT_CONFIG_H

#include <Arduino.h>

namespace config {
    const char WIFI_SSID[] = "Solomaha_2";
    const char WIFI_PASSWORD[] = "solomakha21";

    const auto MQTT_HOST = IPAddress(192, 168, 1, 230);
    const uint16_t MQTT_PORT = 1883;
    const char MQTT_ID[] = "corridor-light";
    const char MQTT_PASSWORD[] = "fdsnkjhjksdfhgkdfsghsvdffgddgf432342grefd32e";

    const uint8_t PIR_PIN = D1;
    const uint8_t RELAY_PIN = D2;
    const uint8_t MOTION_DELAY = 15;
}

#endif
