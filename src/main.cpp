
#include <TFT_eSPI.h>               // Driver for the ILI9341 LCD controller
#include <XPT2046_Touchscreen.h>    // Driver for the XPT2046 touch controller
#include <Wifi.h>                   // Driver for the ESP32 Wifi controller
#include <PubSubClient.h>           // MQTT client library
#include <WiFiClient.h>             // Wifi client library

#include "secrets.h"                // Credentials
#include "NotoSansBold12.h"
#include "NotoSansBold18.h"
#include "NotoSansBold24.h"
#include "NotoSansBold36.h"

/* GPIO pins connected to the XPT2046 touch controller */
#define XPT2046_IRQ     36
#define XPT2046_MOSI    32
#define XPT2046_MISO    39
#define XPT2046_CLK     25
#define XPT2046_CS      33

SPIClass vSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

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
    bool dirty;
} data_t;

data_t data = {
    {{0,999,-999},{50,100,0},{1000,1100,900},{0,100,0}},
    {{0,999,-999},{50,100,0},{1000,1100,900},{0,100,0}},
    {{0,999,-999},{50,100,0},{1000,1100,900},{0,100,0}},
    false
};

/* Function prototypes */
void dispInit();
void dispTask(void *param);
void dispValueWidget(TFT_eSprite *spr, const char *label, dataRecord_t *data, uint8_t dp);
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

    dispInit();
    wifiInit();
    mqttInit();
}

void loop() {

    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        Serial.printf("%d %d %d\n", p.x, p.y, p.z);
//        tft.fillCircle(map(p.x, 180, 3660, 0, 320), map(p.y, 240, 3840, 0, 240), 3, TFT_RED);
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
    data.dirty = true;
}

void updateValue(dataRecord_t* data, float value) {

    data->current = value;
    if (value < data->minimum) data->minimum = value;
    if (value > data->maximum) data->maximum = value;
}

/* ----- Display Task ---- */

void dispInit() {

    TaskHandle_t taskHandle;
    xTaskCreatePinnedToCore(dispTask, "Display", 8192, nullptr, 2, &taskHandle, 1);
}

void dispTask(void *param) {

    TFT_eSPI tft = TFT_eSPI();
    TFT_eSprite spr = TFT_eSprite(&tft);

    /* Initialise the LCD controller */
    tft.init();
    tft.setRotation(1);

    /* Clear the screen and draw the fixed items */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(0x73EF, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.loadFont(NotoSansBold18);
    tft.drawString("INSIDE", 80, 15);
    tft.drawString("OUTSIDE", 240, 15);
    tft.unloadFont();

    /* Create the sprite for rendering the widgets */
    spr.createSprite(160, 60);

    while (true) {
        if (data.dirty) {
            dispValueWidget(&spr, "Temperature", &data.indoor.temperature, 1); spr.pushSprite(0, 30);
            dispValueWidget(&spr, "Humidity", &data.indoor.humidity, 0); spr.pushSprite(0, 100);
            dispValueWidget(&spr, "Pressure", &data.indoor.pressure, 0); spr.pushSprite(0, 170);
            dispValueWidget(&spr, "Temperature", &data.outdoor.temperature, 1); spr.pushSprite(160, 30);
            dispValueWidget(&spr, "Humidity", &data.outdoor.humidity, 0); spr.pushSprite(160, 100);
            dispValueWidget(&spr, "Pressure", &data.outdoor.pressure, 0); spr.pushSprite(160, 170);
            data.dirty = false;
        }
        vTaskDelay(1000);
    }
}

void dispValueWidget(TFT_eSprite *spr, const char *label, dataRecord_t *data, uint8_t dp) {

    spr->fillSprite(TFT_BLACK);
    spr->setTextDatum(MC_DATUM);

    spr->loadFont(NotoSansBold36);
    spr->setTextColor(TFT_GREEN, TFT_BLACK);
    spr->drawFloat(data->current, dp, 50, 40);
    spr->unloadFont();

    spr->loadFont(NotoSansBold12);
    spr->setTextColor(0x03E0, TFT_BLACK);
    spr->drawString(label, 50, 10);
    spr->unloadFont();

    spr->loadFont(NotoSansBold18);
    spr->setTextColor(TFT_MAROON, TFT_BLACK);
    spr->drawFloat(data->maximum, dp, 130, 15);
    spr->setTextColor(TFT_NAVY, TFT_BLACK);
    spr->drawFloat(data->minimum, dp, 130, 45);
    spr->unloadFont();
}
