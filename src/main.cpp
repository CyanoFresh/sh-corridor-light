#include <AsyncMqttClient.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <EEPROM.h>
#include "config.h"

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

Ticker motionTimer;

uint8_t motionEnabled;

void connectToWifi() {
    Serial.println(F("Connecting to Wi-Fi..."));
    WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
}

void connectToMqtt() {
    Serial.println(F("Connecting to MQTT..."));
    mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event) {
    connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event) {
    Serial.print(F("Disconnected from Wi-Fi: "));
    Serial.println(event.reason);
    digitalWrite(LED_BUILTIN, LOW);

    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, connectToWifi);
}

void onMqttConnect(bool) {
    Serial.println(F("Connected to MQTT."));
    digitalWrite(LED_BUILTIN, HIGH);

    // Subscribe to topics:
    mqttClient.subscribe("motion-switch/corridor-light/set", 0);
    mqttClient.subscribe("motion-switch/corridor-light/toggle", 0);
    mqttClient.subscribe("motion-switch/corridor-light/motion/set", 0);

    // Send current state
    mqttClient.publish("motion-switch/corridor-light", 0, false, digitalRead(config::RELAY_PIN) == HIGH ? "true" : "false");
    mqttClient.publish("motion-switch/corridor-light/motion", 0, false, motionEnabled ? "true" : "false");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.print(F("Disconnected from MQTT. Reason: "));
    Serial.println((int) reason);

    digitalWrite(LED_BUILTIN, LOW);

    if (WiFi.isConnected()) {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
}

void turnOff() {
    digitalWrite(config::RELAY_PIN, LOW);
    mqttClient.publish("motion-switch/corridor-light", 0, false, "false");
}

char payloadBuffer[6];  // 6 chars are enough for json boolean

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties, size_t len, size_t, size_t) {
    uint8_t newState = LOW;

    if (strcmp(topic, "motion-switch/corridor-light/toggle") == 0) {
        if (digitalRead(config::RELAY_PIN) == LOW) {
            newState = HIGH;
        } else if (!motionEnabled) {    // motion is disabled and light is turning off
            motionTimer.detach();
        }

        mqttClient.publish("motion-switch/corridor-light", 0, false, newState == LOW ? "false" : "true");

        digitalWrite(config::RELAY_PIN, newState);

        EEPROM.put(0, newState);
        EEPROM.commit();
    } else if (strcmp(topic, "motion-switch/corridor-light/set") == 0) {
        payloadBuffer[len] = '\0';
        strncpy(payloadBuffer, payload, len);

        if (strncmp(payloadBuffer, "true", 4) == 0) {
            newState = HIGH;  // Turn on
        }

        mqttClient.publish("motion-switch/corridor-light", 0, false, payloadBuffer);

        digitalWrite(config::RELAY_PIN, newState);

        EEPROM.put(0, newState);
        EEPROM.commit();
    } else if (strcmp(topic, "motion-switch/corridor-light/motion/toggle") == 0) {
        motionEnabled = !motionEnabled;

        mqttClient.publish("motion-switch/corridor-light/motion", 0, false, motionEnabled ? "true" : "false");

        EEPROM.put(1, motionEnabled);
        EEPROM.commit();
    } else if (strcmp(topic, "motion-switch/corridor-light/motion/set") == 0) {
        payloadBuffer[len] = '\0';
        strncpy(payloadBuffer, payload, len);

        motionEnabled = strncmp(payloadBuffer, "true", 4) == 0;

        mqttClient.publish("motion-switch/corridor-light/motion", 0, false, payloadBuffer);

        EEPROM.put(1, motionEnabled);
        EEPROM.commit();
    } else {
        Serial.println("ERROR! Unknown message");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(config::RELAY_PIN, OUTPUT);
    pinMode(config::PIR_PIN, INPUT);

    digitalWrite(LED_BUILTIN, LOW);

    bool lastRelayState;

    EEPROM.begin(sizeof(bool) * 3); // store 3 booleans

    lastRelayState = EEPROM.read(0);
    motionEnabled = EEPROM.read(1);

    digitalWrite(config::RELAY_PIN, lastRelayState);

    if (motionEnabled == 255) {
        motionEnabled = 1;
        EEPROM.put(1, motionEnabled);
        EEPROM.commit();
    }

    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(config::MQTT_HOST, config::MQTT_PORT);
    mqttClient.setClientId(config::MQTT_ID);
    mqttClient.setCredentials("device", config::MQTT_PASSWORD);

    connectToWifi();
}

void loop() {
    if (motionEnabled) {
        uint8_t pirState = digitalRead(config::PIR_PIN);

        if (pirState == HIGH) {
            // Turn on if is off
            if (digitalRead(config::RELAY_PIN) == LOW) {
                digitalWrite(config::RELAY_PIN, HIGH);

                mqttClient.publish("motion-switch/corridor-light", 0, false, "true");
            }

            // (Re)Start timer to turn off
            motionTimer.once(config::MOTION_DELAY, turnOff);

            Serial.println("Motion detected");
        }
    }
}