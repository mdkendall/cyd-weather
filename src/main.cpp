
#include <TFT_eSPI.h>               // Driver for the ILI9341 LCD controller
#include <XPT2046_Touchscreen.h>    // Driver for the XPT2046 touch controller
#include <Wifi.h>                   // Driver for the ESP32 Wifi controller
#include <PubSubClient.h>           // MQTT client library
#include <WiFiClient.h>             // Wifi client library
#include "secrets.h"                // Credentials

/* GPIO pins connected to the XPT2046 touch controller */
#define XPT2046_IRQ     36
#define XPT2046_MOSI    32
#define XPT2046_MISO    39
#define XPT2046_CLK     25
#define XPT2046_CS      33

SPIClass vSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

TFT_eSPI tft = TFT_eSPI();

typedef struct dataRecord_s {
    float current;
    float minimum;
    float maximum;
} dataRecord_t;

typedef struct dataSet_s {
    dataRecord_t temperature;
    dataRecord_t humidity;
    dataRecord_t pressure;
    dataRecord_t windSpeed;
} dataSet_t;

typedef struct data_s {
    dataSet_t indoor;
    dataSet_t outdoor;
    dataSet_t reported;
} data_t;

data_t data = {
    {{0,999,-999},{50,100,0},{1000,1100,900},{0,100,0}},
    {{0,999,-999},{50,100,0},{1000,1100,900},{0,100,0}},
    {{0,999,-999},{50,100,0},{1000,1100,900},{0,100,0}}
};

/* Function prototypes */

void wifiInit();
void mqttInit();
void mqttTask(void *param);
void mqttHandleMessage(char* topic, uint8_t* payload, unsigned int len);
void updateValue(dataRecord_t* data, float value);

/* Main functionality */

void setup() {

    Serial.begin(115200);

    /* Initialise the touch controller */
    vSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(vSpi);
    ts.setRotation(1);

    /* Initialise the LCD controller */
    tft.init();
    tft.setRotation(1);

    wifiInit();
    mqttInit();

    /* Clear the screen and display example text */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.drawString("Hello World", 320/2, 240/2, 1);
//  tft.drawString(WiFi.localIP().toString(), 320/2, 240/2, 1);

}

void loop() {

    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        Serial.printf("%d %d %d\n", p.x, p.y, p.z);
        tft.fillCircle(map(p.x, 180, 3660, 0, 320), map(p.y, 240, 3840, 0, 240), 3, TFT_RED);
    }
}

/* ----- WiFi ----- */

void wifiInit() {

    Serial.printf("[WiFi] connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    Serial.printf("[WiFi] connected with IP %s\n", WiFi.localIP().toString().c_str());
    configTime(0, 0, "0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org");
}

/* ----- MQTT Task ----- */

#define MQTT_RETRY_INTERVAL 10

void mqttInit() {

    TaskHandle_t taskHandle;
    xTaskCreatePinnedToCore(mqttTask, "Subscriber", 8192, nullptr, 2, &taskHandle, 1);
}

void mqttTask(void *param) {

    time_t now;
    time_t connectRetryTime = (time_t)0;
    WiFiClient espClient;
    PubSubClient pubsubclient(espClient);

    char sDeviceID[26];
    uint64_t chipid = ESP.getEfuseMac();
    snprintf(sDeviceID, sizeof(sDeviceID), "Weather-%04X%08X", (uint16_t)(chipid>>32), (uint32_t)chipid);

    while (true) {

        /* If not connected, try connecting */
        if (!pubsubclient.connected() && (now = time(nullptr)) > connectRetryTime) {
            connectRetryTime = now + MQTT_RETRY_INTERVAL;
            Serial.printf("[MQTT] connecting to %s\n", MQTT_BROKER);
            pubsubclient.setServer(MQTT_BROKER, MQTT_PORT);
            pubsubclient.setCallback(mqttHandleMessage);
            if (pubsubclient.connect(sDeviceID, MQTT_USER, MQTT_PASS)) {
                Serial.printf("[MQTT] connected as %s\n", sDeviceID);
                pubsubclient.subscribe("enviro/#");
            } else {
                Serial.println("[MQTT] connection failed");
            }
        }

        pubsubclient.loop();
        vTaskDelay(1);
    }
}

void mqttHandleMessage(char* topic, uint8_t* payload, unsigned int len) {

    payload[len] = 0;
    Serial.printf("[MQTT] received %s: %s\n", topic, payload);
    float value = atof((char*)payload);

    if (!strcmp(topic, "enviro/indoor/temperature")) { updateValue(&data.indoor.temperature, value); }
    else if (!strcmp(topic, "enviro/indoor/humidity")) { updateValue(&data.indoor.humidity, value); }
    else if (!strcmp(topic, "enviro/indoor/pressure")) { updateValue(&data.indoor.pressure, value); }
    else if (!strcmp(topic, "enviro/indoor/windSpeed")) { updateValue(&data.indoor.windSpeed, value); }
    else if (!strcmp(topic, "enviro/outdoor/temperature")) { updateValue(&data.outdoor.temperature, value); }
    else if (!strcmp(topic, "enviro/outdoor/humidity")) { updateValue(&data.outdoor.humidity, value); }
    else if (!strcmp(topic, "enviro/outdoor/pressure")) { updateValue(&data.outdoor.pressure, value); }
    else if (!strcmp(topic, "enviro/outdoor/windSpeed")) { updateValue(&data.outdoor.windSpeed, value); }
    else if (!strcmp(topic, "enviro/reported/temperature")) { updateValue(&data.reported.temperature, value); }
    else if (!strcmp(topic, "enviro/reported/humidity")) { updateValue(&data.reported.humidity, value); }
    else if (!strcmp(topic, "enviro/reported/pressure")) { updateValue(&data.reported.pressure, value); }
    else if (!strcmp(topic, "enviro/reported/windSpeed")) { updateValue(&data.reported.windSpeed, value); }
}

void updateValue(dataRecord_t* data, float value) {

    data->current = value;
    if (value < data->minimum) data->minimum = value;
    if (value > data->maximum) data->maximum = value;
}
