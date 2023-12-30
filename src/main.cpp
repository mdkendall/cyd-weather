
#include <TFT_eSPI.h>               // Driver for the ILI9341 LCD controller
#include <XPT2046_Touchscreen.h>    // Driver for the XPT2046 touch controller
#include <Wifi.h>                   // Driver for the ESP32 Wifi controller
#include <PubSubClient.h>           // MQTT client library
#include <WiFiClient.h>             // Wifi client library
#include <time.h>                   // Time library
#include <vector>                   // Vector library

#include "secrets.h"                // Credentials

/* Fonts from https://fonts.google.com/noto licensed under the Open Font License,
 * converted with TFT-eSPI/tools/Create_Smooth_Font Processing script */
#include "NotoSansBold12.h"
#include "NotoSansBold18.h"
#include "NotoSansBold24.h"
#include "NotoSansBold36.h"

/* Record of data to be displayed */
struct DataValue {
    time_t timestamp;
    float value;
};

class DataRecord {
    public:
        void setValue(float value) {
            time_t now = time(nullptr);
            history.push_back({now, value});
            time_t cutoff = now - 60*60*24;
            while (history.size() && history.front().timestamp < cutoff) {
                history.erase(history.begin());
            }
        };
        float getValue() { return history.size() ? history.back().value : 0.0; }
        float getMinimum() {
            float min = getValue();
            for (auto &v : history) if (v.value < min) { min = v.value; }
            return min;
        }
        float getMaximum() {
            float max = getValue();
            for (auto &v : history) if (v.value > max) { max = v.value; }
            return max;
        }
    private:
        std::vector<DataValue> history;
};

class DataSet {
    public:
        DataRecord temperature;
        DataRecord humidity;
        DataRecord pressure;
};

class Data {
    public:
        DataSet indoor;
        DataSet outdoor;
        bool dirty = false;
} data;

/* Function prototypes */
void touchInit();
void touchTask(void *param);
void dispInit();
void dispTask(void *param);
void dispValueWidget(TFT_eSprite *spr, const char *label, DataRecord *data, uint8_t dp);
void wifiInit();
void mqttInit();
void mqttTask(void *param);
void mqttHandleMessage(char* topic, uint8_t* payload, unsigned int len);

/* Main functionality */

void setup() {

    Serial.begin(115200);
    touchInit();
    dispInit();
    wifiInit();
    mqttInit();
}

void loop() {
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

    if      (!strcmp(topic, "enviro/indoor/temperature")) { data.indoor.temperature.setValue(value); }
    else if (!strcmp(topic, "enviro/indoor/humidity")) { data.indoor.humidity.setValue(value); }
    else if (!strcmp(topic, "enviro/indoor/pressure")) { data.indoor.pressure.setValue(value); }
    else if (!strcmp(topic, "enviro/outdoor/temperature")) { data.outdoor.temperature.setValue(value); }
    else if (!strcmp(topic, "enviro/outdoor/humidity")) { data.outdoor.humidity.setValue(value); }
    else if (!strcmp(topic, "enviro/outdoor/pressure")) { data.outdoor.pressure.setValue(value); }
    data.dirty = true;
}

/* ----- Touch task ----- */

#define XPT2046_IRQ     36
#define XPT2046_MOSI    32
#define XPT2046_MISO    39
#define XPT2046_CLK     25
#define XPT2046_CS      33

void touchInit() {

    TaskHandle_t taskHandle;
    xTaskCreatePinnedToCore(touchTask, "Touch", 8192, nullptr, 2, &taskHandle, 1);
}

void touchTask(void *param) {

    /* Initialise the touch controller */
    SPIClass vSpi = SPIClass(VSPI);
    XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
    vSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(vSpi);
    ts.setRotation(1);

    while (true) {
        if (ts.tirqTouched() && ts.touched()) {
            TS_Point p = ts.getPoint();
            // Serial.printf("[Touch] %d %d %d\n", p.x, p.y, p.z);
        }
        vTaskDelay(1);
    }
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
    tft.drawString("Inside", 80, 15);
    tft.drawString("Outside", 240, 15);
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

void dispValueWidget(TFT_eSprite *spr, const char *label, DataRecord *data, uint8_t dp) {

    spr->fillSprite(TFT_BLACK);
    spr->setTextDatum(MC_DATUM);

    spr->loadFont(NotoSansBold36);
    spr->setTextColor(TFT_GREEN, TFT_BLACK);
    spr->drawFloat(data->getValue(), dp, 50, 40);
    spr->unloadFont();

    spr->loadFont(NotoSansBold12);
    spr->setTextColor(0x03E0, TFT_BLACK);
    spr->drawString(label, 50, 10);
    spr->unloadFont();

    spr->loadFont(NotoSansBold24);
    spr->setTextColor(TFT_MAROON, TFT_BLACK);
    spr->drawFloat(data->getMaximum(), dp, 130, 15);
    spr->setTextColor(TFT_NAVY, TFT_BLACK);
    spr->drawFloat(data->getMinimum(), dp, 130, 45);
    spr->unloadFont();
}
