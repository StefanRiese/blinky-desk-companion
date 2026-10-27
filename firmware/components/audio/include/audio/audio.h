// Speaker and microphone, through the board's audio functions (board.h; ESP-IDF only).
// - Speaker: plays a tune (alarm, countdown) until stopped.
// - Microphone: measures the level ten times a second and passes it through a
//   core::SoundLevel: indoor.noise into the DataStore (30 s average), and the reactions
//   (loud noise, voices) to a hook. Only levels are used; no audio is kept or sent. Off while
//   sound.enabled is false, and deaf while the speaker plays.
#pragma once

#include <functional>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "app_core/sound.h"

namespace audio {

// start sets up the board's audio and starts the speaker and microphone tasks; false if the
// board has none (or it failed). on_sound runs in the microphone task.
bool start(core::DataStore &store, const core::Settings &settings,
           std::function<void(core::SoundLevel::Event)> on_sound);

// play starts a tune (from its beginning), or stops with core::Tune::None. Thread-safe.
void play(core::Tune tune);

} // namespace audio
