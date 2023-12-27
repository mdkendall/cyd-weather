
#include <TFT_eSPI.h>               // Driver for the ILI9341 LCD controller
#include <XPT2046_Touchscreen.h>    // Driver for the XPT2046 touch controller
#include <lvgl.h>                   // LVGL library

/* GPIO pins connected to the XPT2046 touch controller */
#define XPT2046_IRQ     36
#define XPT2046_MOSI    32
#define XPT2046_MISO    39
#define XPT2046_CLK     25
#define XPT2046_CS      33

SPIClass vSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

TFT_eSPI tft = TFT_eSPI();

/* LVGL display device */
#define LV_BUF_SIZE     (TFT_WIDTH * TFT_HEIGHT / 10)
lv_disp_drv_t disp_drv;             // display driver descriptor
lv_disp_draw_buf_t draw_buf;        // draw buffer descriptor
lv_color_t buf[LV_BUF_SIZE];        // actual draw buffer

/* LVGL input device (touchpad) */
lv_indev_drv_t indev_drv;           // input device driver descriptor

/* Callback for LVGL to write the draw buffer to the display */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
    tft.pushColors(&color_p->full, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

/* Callback for LVGL to read the touchpad */
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {

    if (ts.tirqTouched() && ts.touched()) {
        TS_Point p = ts.getPoint();
        data->state = LV_INDEV_STATE_PR;
        data->point.x = map(p.x, 180, 3660, 0, 320);
        data->point.y = map(p.y, 240, 3840, 0, 240);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setup() {

    Serial.begin(115200);

    /* Initialise the touch controller */
    vSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(vSpi);
    ts.setRotation(1);

    /* Initialise the LCD controller */
    tft.init();
    tft.setRotation(1);

    /* Initialise the LVGL library */
    lv_init();

    /* Initialise the LVGL display */
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_BUF_SIZE);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Initialise the LVGL input device */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    /* Clear the screen and display example text */
    // tft.fillScreen(TFT_BLACK);
    // tft.setTextColor(TFT_GREEN, TFT_BLACK);
    // tft.drawCentreString("Hello, World!", 320/2, 240/2, 4);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text( label, "Hello, World!");
    lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
}

void loop() {

    /* Call the LVGL task handler */
    lv_task_handler();

    // if (ts.touched()) {
    //     TS_Point p = ts.getPoint();
    //     Serial.printf("%d %d %d\n", p.x, p.y, p.z);
    //     tft.fillCircle(map(p.x, 180, 3660, 0, 320), map(p.y, 240, 3840, 0, 240), 3, TFT_RED);
    // }
}
