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

    app->eyes_screen_ = make_screen();
    app->eyes_ = Eyes::create(app->eyes_screen_, info.seed);
    app->mood_label_ = lv_label_create(app->eyes_screen_);
    lv_obj_set_style_text_font(app->mood_label_, font::small(), 0);
    lv_obj_set_style_text_color(app->mood_label_, theme::muted(), 0);
    lv_obj_align(app->mood_label_, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_label_set_text(app->mood_label_, "neutral");
    lv_obj_add_event_cb(app->eyes_screen_, on_eyes_event, LV_EVENT_ALL, app);

    app->info_screen_ = make_screen();
    create_hello(app->info_screen_, {.version = info.version, .board = info.board});
    lv_obj_add_event_cb(app->info_screen_, on_info_event, LV_EVENT_GESTURE, app);

    lv_screen_load(app->eyes_screen_);
    return app;
}

void App::next_mood()
{
    const auto all = moods();
    mood_index_ = (mood_index_ + 1) % all.size();
    eyes_->set_mood(all[mood_index_]);
    lv_label_set_text(mood_label_, std::string(all[mood_index_]).c_str());
}

void App::show(lv_obj_t *screen, lv_dir_t dir)
{
    lv_screen_load_anim(screen, dir == LV_DIR_LEFT ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        250, 0, false);
}

void App::on_eyes_event(lv_event_t *e)
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
            app->show(app->info_screen_, dir);
        }
        break;
    }
    case LV_EVENT_CLICKED:
        if (!app->gesture_) {
            app->next_mood();
        }
        break;
    default:
        break;
    }
}

void App::on_info_event(lv_event_t *e)
{
    auto *app = static_cast<App *>(lv_event_get_user_data(e));
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        app->show(app->eyes_screen_, dir);
    }
}

} // namespace ui
