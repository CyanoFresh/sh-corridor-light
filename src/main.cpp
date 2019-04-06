#include <AsyncMqttClient.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <EEPROM.h>

#define WIFI_SSID "Solomaha"
#define WIFI_PASSWORD "solomakha21"

#define MQTT_HOST IPAddress(192, 168, 1, 230)
#define MQTT_PORT 1883
#define MQTT_ID "corridor-light"
#define MQTT_PASSWORD "fdsnkjhjksdfhgkdfsghsvdffgddgf432342grefd32e"

#define PIR_PIN D1
#define RELAY_PIN D2
#define MOTION_DELAY 15

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

Ticker motionTimer;

uint8_t motionEnabled;
char payloadBuffer[6];  // enough for json boolean

void connectToWifi() {
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event) {
    Serial.println("Connected to Wi-Fi.");
    digitalWrite(LED_BUILTIN, LOW);

    connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event) {
    Serial.print("Disconnected from Wi-Fi: ");
    Serial.println(event.reason);
    digitalWrite(LED_BUILTIN, HIGH);

    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, connectToWifi);
}

void onMqttConnect(bool) {
    Serial.println("Connected to MQTT.");
    digitalWrite(LED_BUILTIN, LOW);

    // Subscribe to topics:
    mqttClient.subscribe("switch/corridor-light/set", 0);
    mqttClient.subscribe("switch/corridor-light/toggle", 0);
    mqttClient.subscribe("switch/corridor-light/motion/set", 0);
    mqttClient.subscribe("device/corridor-light", 0);

    // Send current state
    mqttClient.publish("switch/corridor-light", 0, false, digitalRead(RELAY_PIN) == HIGH ? "false" : "true");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.print("Disconnected from MQTT. Reason: ");
    Serial.println((int) reason);
    digitalWrite(LED_BUILTIN, HIGH);

    if (WiFi.isConnected()) {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties, size_t len, size_t, size_t) {
    uint8_t newState = HIGH;

    if (strcmp(topic, "switch/corridor-light/toggle") == 0) {
        if (digitalRead(RELAY_PIN) == HIGH) {
            newState = LOW;
        }

        mqttClient.publish("switch/corridor-light", 0, false, newState == HIGH ? "false" : "true");

        digitalWrite(RELAY_PIN, newState);

        EEPROM.put(0, newState);
        EEPROM.commit();
    } else if (strcmp(topic, "switch/corridor-light/set") == 0) {
        payloadBuffer[len] = '\0';
        strncpy(payloadBuffer, payload, len);

        if (strncmp(payloadBuffer, "true", 4) == 0) {
            newState = LOW;  // Turn on
        }

        mqttClient.publish("switch/corridor-light", 0, false, payloadBuffer);

        digitalWrite(RELAY_PIN, newState);

        EEPROM.put(0, newState);
        EEPROM.commit();
    } else {
        payloadBuffer[len] = '\0';
        strncpy(payloadBuffer, payload, len);

        motionEnabled = strncmp(payloadBuffer, "true", 4) == 0;

        mqttClient.publish("switch/corridor-light/motion", 0, false, payloadBuffer);

        EEPROM.put(1, motionEnabled);
        EEPROM.commit();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println();

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);

    EEPROM.begin(sizeof(motionEnabled) * 2);

    uint8_t lastState = EEPROM.read(0);
    motionEnabled = EEPROM.read(1);

    digitalWrite(RELAY_PIN, lastState);
    Serial.print("MotionEnabled from memory: ");
    Serial.println(motionEnabled, DEC);
    Serial.println(motionEnabled);

    if (motionEnabled == 255) {
        Serial.print("was not set. Loading default");
        motionEnabled = 1;
        EEPROM.put(1, motionEnabled);
        EEPROM.commit();
    }

    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setClientId(MQTT_ID);
    mqttClient.setCredentials("device", MQTT_PASSWORD);

    connectToWifi();
}

void motionEnded() {
    digitalWrite(RELAY_PIN, LOW);
    mqttClient.publish("switch/corridor-light", 0, false, "false");
}

void loop() {
    if (motionEnabled) {
        int pirState = digitalRead(PIR_PIN);

        if (pirState == HIGH) {
            // Turn on if is off
            if (digitalRead(RELAY_PIN) == LOW) {
                digitalWrite(RELAY_PIN, HIGH);

                mqttClient.publish("switch/corridor-light", 0, false, "true");
            }

            // (Re)Start timer for turning off
            motionTimer.detach();
            motionTimer.once(MOTION_DELAY, motionEnded);
        }
    }
}