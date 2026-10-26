#include "ui/app.h"

#include <string>

#include "ui/fonts.h"
#include "ui/hello.h"
#include "ui/theme.h"

namespace ui {
namespace {

lv_obj_t *make_screen()
{
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, theme::background(), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

} // namespace

App *App::create(const AppInfo &info)
{
    auto *app = new App();
    for (auto &s : app->screens_) {
        s = make_screen();
        lv_obj_add_event_cb(s, on_event, LV_EVENT_ALL, app);
    }

    app->eyes_ = Eyes::create(app->screens_[kEyes], info.seed);
    app->mood_label_ = lv_label_create(app->screens_[kEyes]);
    lv_obj_set_style_text_font(app->mood_label_, font::small(), 0);
    lv_obj_set_style_text_color(app->mood_label_, theme::muted(), 0);
    lv_obj_align(app->mood_label_, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_label_set_text(app->mood_label_, "neutral");

    app->dashboard_ = Dashboard::create(app->screens_[kDashboard], {info.store, info.settings, info.local_time});
    create_hello(app->screens_[kInfo], {.version = info.version, .board = info.board});

    lv_screen_load(app->screens_[kEyes]);
    return app;
}

void App::set_mood(std::string_view mood)
{
    eyes_->set_mood(mood);
    const auto all = moods();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i] == eyes_->mood()) {
            mood_index_ = i;
        }
    }
    lv_label_set_text(mood_label_, std::string(eyes_->mood()).c_str());
}

void App::next_mood()
{
    const auto all = moods();
    set_mood(all[(mood_index_ + 1) % all.size()]);
}

void App::go(int step)
{
    current_ = (current_ + step + kScreens) % kScreens;
    lv_screen_load_anim(screens_[current_], step > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        250, 0, false);
}

void App::on_event(lv_event_t *e)
{
    auto *app = static_cast<App *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        app->gesture_ = false;
        break;
    case LV_EVENT_GESTURE: {
        const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            app->gesture_ = true;
            app->go(dir == LV_DIR_LEFT ? 1 : -1);
        }
        break;
    }
    case LV_EVENT_CLICKED:
        if (!app->gesture_ && lv_event_get_current_target(e) == app->screens_[kEyes]) {
            app->next_mood();
        }
        break;
    default:
        break;
    }
}

} // namespace ui
