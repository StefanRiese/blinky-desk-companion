// Page: a screen whose content follows the DataStore, the settings or the clock. The App
// calls refresh when the page comes on screen and then once a second while it stays there,
// so pages that aren't shown cost nothing.
#pragma once

namespace ui {

class Page {
public:
    virtual ~Page() = default;
    // refresh updates the page now (tests and screenshots call it directly too).
    virtual void refresh() = 0;
};

} // namespace ui
