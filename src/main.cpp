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
//#define DOOR_PIN D7
//#define ARM_BUTTON_PIN D4
//#define BUZZER_PIN D5
//#define RED_LIGHT_PIN D6

#define MOTION_DELAY 15
//#define ALARM_DELAY 5
#define STATUS_INTERVAL 0.7

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

Ticker motionTimer;

//Ticker alarmTimer;
//Ticker statusTimer;

//uint8_t armed;
//uint8_t alarming = false;
//uint8_t armAfterDoorClose = false;
//uint8_t sendDoorStateOnConnect = true;
uint8_t motionEnabled;

void connectToWifi() {
    Serial.println(F("Connecting to Wi-Fi..."));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
//    mqttClient.subscribe("device/corridor-light", 0);

    // Send current state
    mqttClient.publish("motion-switch/corridor-light", 0, false, digitalRead(RELAY_PIN) == HIGH ? "true" : "false");
    mqttClient.publish("motion-switch/corridor-light/motion", 0, false, motionEnabled ? "true" : "false");

//    if (sendDoorStateOnConnect) {
//        mqttClient.publish("variable/door", 0, false, digitalRead(DOOR_PIN) == HIGH ? "\"open\"" : "\"closed\"");
//        sendDoorStateOnConnect = false;
//    }
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
    digitalWrite(RELAY_PIN, LOW);
    mqttClient.publish("motion-switch/corridor-light", 0, false, "false");
}

//void statusInterval() {
//    if (armAfterDoorClose || alarmTimer.active()) {
//        if (digitalRead(RED_LIGHT_PIN) == LOW) {
//            digitalWrite(RED_LIGHT_PIN, HIGH);
//        } else {
//            digitalWrite(RED_LIGHT_PIN, LOW);
//        }
//    }
//}

//void alarmOn() {
//    alarming = true;
//    armed = false;
//
//    digitalWrite(RED_LIGHT_PIN, HIGH);
//    tone(BUZZER_PIN, 1000);
//
//    Serial.println("Alarm started");
//}

//void alarmOff() {
//    alarming = false;
//
//    digitalWrite(RED_LIGHT_PIN, LOW);
//    noTone(BUZZER_PIN);
//
//    Serial.println("Alarm ended");
//}

char payloadBuffer[6];  // 6 chars are enough for json boolean

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties, size_t len, size_t, size_t) {
    uint8_t newState = LOW;

    if (strcmp(topic, "motion-switch/corridor-light/toggle") == 0) {
        if (digitalRead(RELAY_PIN) == LOW) {
            newState = HIGH;
        } else if (!motionEnabled) {    // motion is disabled and light is turning off
            motionTimer.detach();
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
    Serial.println();

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
//    pinMode(RED_LIGHT_PIN, OUTPUT);
//    pinMode(BUZZER_PIN, OUTPUT);

    pinMode(PIR_PIN, INPUT);
//    pinMode(DOOR_PIN, INPUT_PULLUP);
//    pinMode(ARM_BUTTON_PIN, INPUT_PULLUP);

    digitalWrite(LED_BUILTIN, LOW);

    bool lastRelayState;

    EEPROM.begin(sizeof(bool) * 3); // store 3 booleans

    lastRelayState = EEPROM.read(0);
    motionEnabled = EEPROM.read(1);
//    armed = EEPROM.read(2);

    digitalWrite(RELAY_PIN, lastRelayState);

    if (motionEnabled == 255) {
        motionEnabled = 1;
        EEPROM.put(1, motionEnabled);
        EEPROM.commit();
    }

//    if (armed == 255) {
//        armed = false;
//        EEPROM.put(2, armed);
//        EEPROM.commit();
//    }

//    if (armed) {
//        Serial.println(" - ARMED (from memory) - ");
//    } else {
//        Serial.println(" - NOT ARMED (from memory) - ");
//    }

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

//uint8_t lastDoorState = HIGH;
//uint8_t lastArmBtnState = HIGH;

void loop() {
    if (motionEnabled) {
        uint8_t pirState = digitalRead(PIR_PIN);

        if (pirState == HIGH) {
            // Turn on if is off
            if (digitalRead(RELAY_PIN) == LOW) {
                digitalWrite(RELAY_PIN, HIGH);

                mqttClient.publish("motion-switch/corridor-light", 0, false, "true");
            }

            // (Re)Start timer to turn off
            motionTimer.once(MOTION_DELAY, turnOff);

            Serial.println("Motion detected");
        }
    }

//    uint8_t doorState = digitalRead(DOOR_PIN);
//
//    if (doorState != lastDoorState) {
//        if (doorState == HIGH) {
//            Serial.println("Door opened");
//
//            if (armed) {
//                alarmTimer.once(ALARM_DELAY, alarmOn);
//                statusTimer.attach(STATUS_INTERVAL, statusInterval);
//
//                Serial.println("Waiting for disarm...");
//            }
//        } else {
//            Serial.println("Door closed");
//
//            if (armAfterDoorClose) {
//                armAfterDoorClose = false;
//                armed = true;
//
//                statusTimer.detach();
//                digitalWrite(RED_LIGHT_PIN, LOW);
//
//                Serial.println(" - ARMED - ");
//            }
//        }
//
//        if (mqttClient.connected()) {
//            mqttClient.publish("variable/door", 0, false, doorState == HIGH ? "\"0\"" : "\"1\"");
//            sendDoorStateOnConnect = false;
//        } else {
//            sendDoorStateOnConnect = true;
//        }
//
//        lastDoorState = doorState;
//    }
//
//    uint8_t armBtnState = digitalRead(ARM_BUTTON_PIN);
//
//    if (armBtnState != lastArmBtnState) {
//        if (armBtnState == LOW) {   // button pressed
//            if (armed || armAfterDoorClose) {
//                Serial.println(" - DISARMED - ");
//
//                armed = false;
//                armAfterDoorClose = false;
//
//                alarmTimer.detach();
//                statusTimer.detach();
//                digitalWrite(RED_LIGHT_PIN, LOW);
//
//                EEPROM.put(2, false);
//            } else if (alarming) {
//                alarmOff();
//                statusTimer.detach();
//                digitalWrite(RED_LIGHT_PIN, LOW);
//
//                EEPROM.put(2, false);
//            } else {
//                Serial.println(" - ARMED (after door close) - ");
//
//                armAfterDoorClose = true;
//
//                statusTimer.attach(STATUS_INTERVAL, statusInterval);
//
//                EEPROM.put(2, true);
//            }
//
//            EEPROM.commit();
//        }
//
//        lastArmBtnState = armBtnState;
//    }
}