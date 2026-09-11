#pragma once

// Two-channel square-wave parameter generator (blips + tunes).
// The host plays the SoundChannel values; this module never opens a device.

#include "src/model/game.hpp"

#include <citsy/types.hpp>

#include <string>
#include <vector>

namespace citsy {

class SoundPlayer {
public:
    void play_blip(const Blip& blip, const Game& game);
    void play_tune(const Tune& tune);
    void stop_tune();
    void pause_tune();
    void resume_tune();

    void update(double dt_ms, const Game& game);

    [[nodiscard]] SoundChannel channel1() const { return ch1_; }
    [[nodiscard]] SoundChannel channel2() const { return ch2_; }
    [[nodiscard]] bool blip_playing() const { return blip_active_; }
    [[nodiscard]] bool tune_playing() const { return cur_tune_ != nullptr; }
    [[nodiscard]] std::string tune_id() const {
        return cur_tune_ ? cur_tune_->id : std::string{};
    }

private:
    SoundChannel ch1_{};
    SoundChannel ch2_{};

    const Tune* cur_tune_ = nullptr;
    bool tune_paused_ = false;
    bool music_paused_for_blip_ = false;

    int bar_index_ = 0;
    int beat16_index_ = -1;
    double beat16_ms_ = 188.0;
    double beat16_timer_ = 0;

    bool blip_active_ = false;
    std::vector<double> blip_freqs_;
    int blip_pitch_index_ = -1;
    double blip_timer_ = 0;
    double blip_duration_ = 0;
    Blip blip_{};
    PulseWave blip_pulse_ = PulseWave::Half;

    std::vector<Pitch> arpeggio_;

    void update_blip(double dt_ms);
    void update_tune(double dt_ms, const Game& game);
    void emit_note(SoundChannel& ch, const Pitch& pitch, PulseWave wave,
                   const Tune* tune, int beat_ms);
};

[[nodiscard]] double pitch_frequency_hz(const Pitch& pitch, const TuneKey* key = nullptr);
[[nodiscard]] PulseWave pulse_from_name(std::string_view name);
[[nodiscard]] int tempo_sixteenth_ms(Tempo t);

} // namespace citsy
