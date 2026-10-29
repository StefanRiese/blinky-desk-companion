#include "headless.h"

#include <algorithm>
#include <chrono>

#include "png.h"

namespace sim {

namespace {
uint32_t now_ms()
{
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}
} // namespace

void init()
{
    static bool done = false;
    if (!done) {
        lv_init();
        lv_tick_set_cb(now_ms);
        done = true;
    }
}

Headless::Headless(int width, int height)
    : width_(width), height_(height), frame_(static_cast<size_t>(width) * height * 4),
      render_(static_cast<size_t>(width) * height * 4)
{
    init();
    disp_ = lv_display_create(width, height);
    lv_display_set_color_format(disp_, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(disp_, render_.data(), nullptr, static_cast<uint32_t>(render_.size()),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp_, this);
    lv_display_set_flush_cb(disp_, flush);
    lv_display_set_default(disp_);

    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, read);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_display(indev_, disp_);
}

Headless::~Headless()
{
    lv_indev_delete(indev_);
    lv_display_delete(disp_);
}

void Headless::render()
{
    lv_obj_invalidate(lv_display_get_screen_active(disp_));
    lv_refr_now(disp_);
}

void Headless::touch(int x, int y, bool pressed)
{
    point_ = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
    pressed_ = pressed;
    lv_indev_read(indev_);
}

uint32_t Headless::pixel(int x, int y) const
{
    const uint8_t *p = &frame_[(static_cast<size_t>(y) * width_ + x) * 4]; // B, G, R, X
    return (uint32_t{p[2]} << 16) | (uint32_t{p[1]} << 8) | p[0];
}

std::vector<uint8_t> Headless::rgb() const
{
    std::vector<uint8_t> out(static_cast<size_t>(width_) * height_ * 3);
    for (size_t i = 0, n = static_cast<size_t>(width_) * height_; i < n; i++) {
        out[i * 3 + 0] = frame_[i * 4 + 2];
        out[i * 3 + 1] = frame_[i * 4 + 1];
        out[i * 3 + 2] = frame_[i * 4 + 0];
    }
    return out;
}

bool Headless::write_png(const std::string &path) const
{
    return png::write_rgb(path, width_, height_, rgb());
}

// flush copies the drawn frame to the screen, turned like the display (as the boards'
// software rotation does).
void Headless::flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    auto *self = static_cast<Headless *>(lv_display_get_user_data(disp));
    const lv_display_rotation_t rot = lv_display_get_rotation(disp);
    if (rot == LV_DISPLAY_ROTATION_0) {
        std::copy(px, px + self->frame_.size(), self->frame_.begin());
    } else {
        const int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
        const uint32_t src_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_XRGB8888);
        const uint32_t dst_stride = lv_draw_buf_width_to_stride(self->width_, LV_COLOR_FORMAT_XRGB8888);
        lv_draw_rotate(px, self->frame_.data(), w, h, src_stride, dst_stride, rot, LV_COLOR_FORMAT_XRGB8888);
    }
    lv_display_flush_ready(disp);
}

void Headless::read(lv_indev_t *indev, lv_indev_data_t *data)
{
    auto *self = static_cast<Headless *>(lv_indev_get_user_data(indev));
    data->point = self->point_;
    data->state = self->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace sim
