// The data pages, as in the emulator: the forecast (today and the next two days), the
// indoor climate with its trend over the last half hour, and the values that hosts and
// scripts send (everything in the DataStore but weather.* and indoor.*). Each reads the
// DataStore when the App refreshes it (once a second while shown) and changes a label only
// when its text changes.
#pragma once

#include <array>
#include <deque>
#include <string>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "lvgl.h"
#include "ui/page.h"

namespace ui {

struct DataPageContext {
    const core::DataStore *store;
    const core::Settings *settings;
};

class ForecastPage : public Page {
public:
    static constexpr int kDays = 3;
    static ForecastPage *create(lv_obj_t *parent, const DataPageContext &ctx);
    void refresh() override;

private:
    explicit ForecastPage(const DataPageContext &ctx) : ctx_(ctx) {}
    static void on_delete(lv_event_t *e);

    struct Row {
        lv_obj_t *name, *icon, *temps, *detail;
    };
    DataPageContext ctx_;
    std::array<Row, kDays> rows_{};
    lv_obj_t *none_ = nullptr;
};

class IndoorPage : public Page {
public:
    static constexpr int kSamples = 180;          // the trend: one sample …
    static constexpr uint32_t kSampleMs = 10'000; // … every 10 s, half an hour
    static IndoorPage *create(lv_obj_t *parent, const DataPageContext &ctx);
    void refresh() override;
    // sample adds the current indoor temperature to the trend (its own timer does this
    // every kSampleMs, also while the page isn't shown).
    void sample();

private:
    explicit IndoorPage(const DataPageContext &ctx) : ctx_(ctx) {}
    static void on_sample(lv_timer_t *t);
    static void on_delete(lv_event_t *e);

    DataPageContext ctx_;
    lv_timer_t *sampler_ = nullptr;
    lv_obj_t *temp_ = nullptr, *hum_ = nullptr, *line_ = nullptr, *range_ = nullptr, *noise_ = nullptr,
             *none_ = nullptr;
    std::deque<double> hist_;
    std::array<lv_point_precise_t, kSamples> points_{};
};

class HostDataPage : public Page {
public:
    static constexpr int kRows = 8;
    static HostDataPage *create(lv_obj_t *parent, const DataPageContext &ctx);
    void refresh() override;

private:
    explicit HostDataPage(const DataPageContext &ctx) : ctx_(ctx) {}
    static void on_delete(lv_event_t *e);

    DataPageContext ctx_;
    std::array<std::array<lv_obj_t *, 2>, kRows> rows_{}; // key, value
    lv_obj_t *none_ = nullptr, *hint_ = nullptr;
};

// format_value shows a DataStore value: whole numbers without decimals, others with up to
// two, true/false as yes/no, text cut to max characters with "…".
std::string format_value(const core::Value &v, size_t max = 16);

} // namespace ui
