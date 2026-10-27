// Speaker and microphone of the ws146 board (ESP-IDF only).
// - Speaker (PCM5101 over I²S, 24 kHz): plays a tune (alarm, countdown) until stopped.
// - Microphone (I²S, 16 kHz): measures the level ten times a second and passes it through a
//   core::SoundLevel: indoor.noise into the DataStore (30 s average), and the reactions
//   (loud noise, voices) to a hook. Only levels are used; no audio is kept or sent. Off while
//   sound.enabled is false, and deaf while the speaker plays.
#pragma once

#include <functional>

#include "app_core/data_store.h"
#include "app_core/settings.h"
#include "app_core/sound.h"

namespace audio {

// start sets up both I²S ports and starts the speaker and microphone tasks. on_sound runs
// in the microphone task.
void start(core::DataStore &store, const core::Settings &settings,
           std::function<void(core::SoundLevel::Event)> on_sound);

// play starts a tune (from its beginning), or stops with core::Tune::None. Thread-safe.
void play(core::Tune tune);

} // namespace audio
