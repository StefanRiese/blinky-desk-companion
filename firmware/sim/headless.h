// An LVGL display that renders into memory, with a scriptable touch input. Used by the
// screenshot tool and the tests, so the UI can be checked without a window or hardware.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

namespace sim {

// init starts LVGL with a millisecond tick from the system clock. Call once.
void init();

class Headless {
public:
    Headless(int width, int height);
    ~Headless();
    Headless(const Headless &) = delete;
    Headless &operator=(const Headless &) = delete;

    lv_display_t *display() const { return disp_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // render redraws everything now.
    void render();

    // touch sets the touch state and lets LVGL read it (press, move, release).
    void touch(int x, int y, bool pressed);

    // pixel returns the colour at x, y as 0xRRGGBB (after render).
    uint32_t pixel(int x, int y) const;

    // write_png saves the current frame.
    bool write_png(const std::string &path) const;

private:
    static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px);
    static void read(lv_indev_t *indev, lv_indev_data_t *data);

    int width_, height_;
    std::vector<uint8_t> frame_; // XRGB8888, full frame
    lv_display_t *disp_ = nullptr;
    lv_indev_t *indev_ = nullptr;
    lv_point_t point_{};
    bool pressed_ = false;
};

} // namespace sim
