
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
    // Start the tft display and set it to black
    tft.init();
    tft.setRotation(1);

    // Clear the screen before writing to it
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawCentreString("Hello, World!", 320/2, 240/2, 4);
}

void loop() {
}
