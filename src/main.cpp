
#include <TFT_eSPI.h>               // Driver for the ILI9341 LCD controller
#include <XPT2046_Touchscreen.h>    // Driver for the XPT2046 touch controller

/* GPIO pins connected to the XPT2046 touch controller */
#define XPT2046_IRQ     36
#define XPT2046_MOSI    32
#define XPT2046_MISO    39
#define XPT2046_CLK     25
#define XPT2046_CS      33

SPIClass vSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

TFT_eSPI tft = TFT_eSPI();

void setup() {

    Serial.begin(115200);

    /* Initialise the touch controller */
    vSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(vSpi);
    ts.setRotation(1);

    /* Initialise the LCD controller */
    tft.init();
    tft.setRotation(1);

    /* Clear the screen and display example text */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawCentreString("Hello, World!", 320/2, 240/2, 4);
}

void loop() {

    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        Serial.printf("%d %d %d\n", p.x, p.y, p.z);
        tft.fillCircle(map(p.x, 0, 3600, 0, 320), map(p.y, 0, 4000, 0, 240), 3, TFT_RED);
    }
}
