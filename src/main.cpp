#include <AsyncMqttClient.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <EEPROM.h>

#define WIFI_SSID "Solomaha_2"
#define WIFI_PASSWORD "solomakha21"

#define MQTT_HOST IPAddress(192, 168, 1, 230)
#define MQTT_PORT 1883
#define MQTT_ID "corridor-light"
#define MQTT_PASSWORD "fdsnkjhjksdfhgkdfsghsvdffgddgf432342grefd32e"

#define PIR_PIN D1
#define RELAY_PIN D2
#define DOOR_PIN D3
#define ARM_BUTTON_PIN D4
#define BUZZER_PIN D5
#define RED_LIGHT_PIN D6
#define MOTION_DELAY 15

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

Ticker motionTimer;

uint8_t armed = false;
uint8_t motionEnabled;
uint8_t sendDoorStateOnConnect = 1;

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
    mqttClient.subscribe("motion-switch/corridor-light/set", 0);
    mqttClient.subscribe("motion-switch/corridor-light/toggle", 0);
    mqttClient.subscribe("motion-switch/corridor-light/motion/set", 0);
    mqttClient.subscribe("device/corridor-light", 0);

    // Send current state
    mqttClient.publish("motion-switch/corridor-light", 0, false, digitalRead(RELAY_PIN) == HIGH ? "true" : "false");
    mqttClient.publish("motion-switch/corridor-light/motion", 0, false, motionEnabled == 1 ? "true" : "false");

    if (sendDoorStateOnConnect) {
        mqttClient.publish("variable/door", 0, false, digitalRead(DOOR_PIN) == HIGH ? "\"open\"" : "\"closed\"");
        sendDoorStateOnConnect = 0;
    }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.print("Disconnected from MQTT. Reason: ");
    Serial.println((int) reason);
    digitalWrite(LED_BUILTIN, HIGH);

    if (WiFi.isConnected()) {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
}

void turnOff() {
    digitalWrite(RELAY_PIN, LOW);
    mqttClient.publish("motion-switch/corridor-light", 0, false, "false");
}

void alarmOn() {
    digitalWrite(RED_LIGHT_PIN, 1);
    tone(BUZZER_PIN, 1000);
}

void alarmOff() {
    digitalWrite(RED_LIGHT_PIN, 0);
    noTone(BUZZER_PIN);
}

char payloadBuffer[6];  // enough for json boolean

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties, size_t len, size_t, size_t) {
    uint8_t newState = LOW;

    if (strcmp(topic, "motion-switch/corridor-light/toggle") == 0) {
        if (digitalRead(RELAY_PIN) == LOW) {
            newState = HIGH;
        }

        mqttClient.publish("motion-switch/corridor-light", 0, false, newState == LOW ? "false" : "true");

        digitalWrite(RELAY_PIN, newState);

        EEPROM.put(0, newState);
        EEPROM.commit();
    } else if (strcmp(topic, "motion-switch/corridor-light/set") == 0) {
        payloadBuffer[len] = '\0';
        strncpy(payloadBuffer, payload, len);

        if (strncmp(payloadBuffer, "true", 4) == 0) {
            newState = HIGH;  // Turn on
        }

        mqttClient.publish("motion-switch/corridor-light", 0, false, payloadBuffer);

        digitalWrite(RELAY_PIN, newState);

        EEPROM.put(0, newState);
        EEPROM.commit();
    } else {
        payloadBuffer[len] = '\0';
        strncpy(payloadBuffer, payload, len);

        motionEnabled = strncmp(payloadBuffer, "true", 4) == 0;

        mqttClient.publish("motion-switch/corridor-light/motion", 0, false, payloadBuffer);

        EEPROM.put(1, motionEnabled);   // motionEnabled state address
        EEPROM.commit();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println();

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(RED_LIGHT_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    pinMode(PIR_PIN, INPUT);
    pinMode(DOOR_PIN, INPUT_PULLUP);
    pinMode(ARM_BUTTON_PIN, INPUT_PULLUP);

    EEPROM.begin(sizeof(motionEnabled) * 2);

    uint8_t lastState = EEPROM.read(0);
    motionEnabled = EEPROM.read(1);

    digitalWrite(RELAY_PIN, lastState);

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
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setClientId(MQTT_ID);
    mqttClient.setCredentials("device", MQTT_PASSWORD);

    connectToWifi();
}

int lastDoorState = 1;
int lastArmBtnState = HIGH;

void loop() {
    if (motionEnabled) {
        int pirState = digitalRead(PIR_PIN);

        if (pirState == HIGH) {
            // Turn on if is off
            if (digitalRead(RELAY_PIN) == LOW) {
                digitalWrite(RELAY_PIN, HIGH);

                mqttClient.publish("motion-switch/corridor-light", 0, false, "true");
            }

            // (Re)Start timer for turning off
            motionTimer.once(MOTION_DELAY, turnOff);
        }
    }

    int doorState = digitalRead(DOOR_PIN);

    if (doorState != lastDoorState) {
        if (doorState == HIGH) {
            Serial.println("Door opened");

            if (armed) {
                alarmOn();
            }
        } else {
            Serial.println("Door closed");
        }

        if (mqttClient.connected()) {
            mqttClient.publish("variable/door", 0, false, doorState == HIGH ? "\"open\"" : "\"closed\"");
            sendDoorStateOnConnect = 0;
        } else {
            sendDoorStateOnConnect = 1;
        }

        lastDoorState = doorState;
    }

    int armBtnState = digitalRead(ARM_BUTTON_PIN);

    if (armBtnState != lastArmBtnState) {
        if (armBtnState == LOW) {
            Serial.println("ARM button pushed");

            armed = !armed;

            if (armed) {
                Serial.println(" - ARMED - ");
            } else {
                Serial.println(" - DISARMED - ");
                alarmOff();
            }
        }

        lastArmBtnState = armBtnState;
    }
}