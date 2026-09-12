#include "src/sound/sound.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace citsy {
namespace {

constexpr double kChromaticHz[12] = {
    261.7, 277.2, 293.7, 311.2, 329.7, 349.3,
    370.0, 392.0, 415.3, 440.0, 466.2, 493.9,
};

constexpr double kMaxVolume = 15.0;
constexpr double kNoteVolume = 5.0;
constexpr double kSfxPeak = 10.0;

Pitch chromatic_from(const Pitch& pitch, const TuneKey* key) {
    Pitch out = pitch;
    if (!pitch.solfa || !key) return out;
    const int deg = pitch.note % 7;
    int mapped = key->notes[static_cast<std::size_t>(deg)];
    if (mapped < 0) mapped = pitch.note;
    out.note = mapped;
    out.solfa = false;
    if (pitch.note >= 7) out.octave = std::min(3, out.octave + 1);
    return out;
}

bool pitch_playable(const Pitch& pitch, const TuneKey* key) {
    if (pitch.beats <= 0) return false;
    if (!key) return true;
    if (!pitch.solfa) return true;
    const int deg = pitch.note % 7;
    if (std::find(key->scale.begin(), key->scale.end(), deg) == key->scale.end())
        return false;
    return key->notes[static_cast<std::size_t>(deg)] >= 0;
}

int tempo_ms(Tempo t) {
    switch (t) {
        case Tempo::Slow:      return 250;
        case Tempo::Medium:    return 188;
        case Tempo::Fast:      return 125;
        case Tempo::ExtraFast: return 94;
    }
    return 188;
}

PulseWave pulse_named(std::string_view n) {
    if (n == "P8") return PulseWave::Eighth;
    if (n == "P4") return PulseWave::Quarter;
    return PulseWave::Half;
}

} // namespace

PulseWave pulse_from_name(std::string_view name) { return pulse_named(name); }
int tempo_sixteenth_ms(Tempo t) { return tempo_ms(t); }

double pitch_frequency_hz(const Pitch& pitch, const TuneKey* key) {
    Pitch p = chromatic_from(pitch, key);
    int note = std::clamp(p.note, 0, 11);
    int oct = std::clamp(p.octave, 0, 3);
    const int dist = oct - 2;  // Octave[4] == 2 is middle C
    return kChromaticHz[note] * std::pow(2.0, dist);
}

void SoundPlayer::play_blip(const Blip& blip, const Game& /*game*/) {
    blip_ = blip;
    blip_pulse_ = blip.instrument;
    blip_timer_ = 0;
    blip_duration_ = static_cast<double>(
        blip.envelope.attack + blip.envelope.decay +
        blip.envelope.length + blip.envelope.release);
    blip_freqs_.clear();
    blip_pitch_index_ = -1;
    auto push = [&](const Pitch& p) {
        if (p.beats > 0) blip_freqs_.push_back(pitch_frequency_hz(p, nullptr));
    };
    push(blip.pitch_a);
    push(blip.pitch_b);
    push(blip.pitch_c);
    blip_active_ = true;
    music_paused_for_blip_ = true;

    ch1_.active = !blip_freqs_.empty();
    ch1_.frequency_hz = blip_freqs_.empty() ? 0 : static_cast<int>(blip_freqs_[0]);
    ch1_.volume = 0;
    ch1_.pulse = blip_pulse_;
    ch1_.duration_ms = static_cast<int>(blip_duration_);
}

void SoundPlayer::play_tune(const Tune& tune) {
    cur_tune_ = &tune;
    beat16_timer_ = 0;
    beat16_index_ = -1;
    bar_index_ = 0;
    beat16_ms_ = tempo_ms(tune.tempo);
    tune_paused_ = false;
    arpeggio_.clear();
}

void SoundPlayer::stop_tune() {
    cur_tune_ = nullptr;
    ch2_ = {};
    if (!blip_active_) ch1_ = {};
}

void SoundPlayer::pause_tune() {
    tune_paused_ = true;
    if (!blip_active_) ch1_ = {};
    ch2_ = {};
}
void SoundPlayer::resume_tune() { tune_paused_ = false; }

void SoundPlayer::update(double dt_ms, const Game& game) {
    update_blip(dt_ms);
    if (!tune_paused_ && !music_paused_for_blip_) {
        update_tune(dt_ms, game);
    }
    if (!blip_active_ && !cur_tune_) {
        ch1_ = {};
        ch2_ = {};
    }
}

void SoundPlayer::update_blip(double dt_ms) {
    if (!blip_active_) return;
    dt_ms = std::min(dt_ms, 32.0);
    blip_timer_ += dt_ms;

    double vol = 0;
    const auto& e = blip_.envelope;
    const double t = blip_timer_;
    if (t < e.attack) {
        vol = (e.attack > 0) ? kSfxPeak * (t / e.attack) : kSfxPeak;
    } else if (t < e.attack + e.decay) {
        const double u = (e.decay > 0) ? (t - e.attack) / e.decay : 1;
        vol = kSfxPeak + (e.sustain - kSfxPeak) * u;
    } else if (t < e.attack + e.decay + e.length) {
        vol = e.sustain;
    } else if (t < blip_duration_) {
        const double rel = e.release > 0
            ? (t - (e.attack + e.decay + e.length)) / e.release
            : 1;
        vol = e.sustain * (1.0 - rel);
    } else {
        vol = 0;
    }

    int idx = 0;
    if (blip_.beat.time > 0 && !blip_freqs_.empty()) {
        const double delta = std::max(0.0, t - blip_.beat.delay) / blip_.beat.time;
        if (blip_.do_repeat) {
            idx = static_cast<int>(delta) % static_cast<int>(blip_freqs_.size());
        } else {
            idx = std::min(static_cast<int>(delta),
                           static_cast<int>(blip_freqs_.size()) - 1);
        }
    }
    blip_pitch_index_ = idx;

    ch1_.active = vol > 0 && !blip_freqs_.empty();
    if (!blip_freqs_.empty()) {
        ch1_.frequency_hz = static_cast<int>(
            blip_freqs_[static_cast<std::size_t>(std::clamp(idx, 0,
                static_cast<int>(blip_freqs_.size()) - 1))]);
    }
    ch1_.volume = static_cast<float>(std::clamp(vol / kMaxVolume, 0.0, 1.0));
    ch1_.pulse = blip_pulse_;
    ch1_.duration_ms = static_cast<int>(std::max(0.0, blip_duration_ - blip_timer_));

    if (blip_timer_ >= blip_duration_) {
        blip_active_ = false;
        music_paused_for_blip_ = false;
        ch1_ = {};
    }
}

void SoundPlayer::emit_note(SoundChannel& ch, const Pitch& pitch, PulseWave wave,
                            const Tune* tune, int beat_ms) {
    const TuneKey* key = (tune && tune->key) ? &*tune->key : nullptr;
    if (!pitch_playable(pitch, key)) return;
    ch.active = true;
    ch.frequency_hz = static_cast<int>(pitch_frequency_hz(pitch, key));
    ch.volume = static_cast<float>(kNoteVolume / kMaxVolume);
    ch.pulse = wave;
    ch.duration_ms = std::max(1, pitch.beats) * beat_ms;
}

void SoundPlayer::update_tune(double dt_ms, const Game& game) {
    if (!cur_tune_) return;
    beat16_timer_ += dt_ms;
    if (beat16_timer_ < beat16_ms_) return;
    beat16_timer_ = 0;
    ++beat16_index_;

    if (beat16_index_ >= kBarLength) {
        beat16_index_ = 0;
        bar_index_ = (bar_index_ + 1) % static_cast<int>(cur_tune_->melody.size());
        arpeggio_.clear();
        if (cur_tune_->arpeggio != Arpeggio::Off && cur_tune_->key &&
            bar_index_ < static_cast<int>(cur_tune_->harmony.size())) {
            const Pitch root = cur_tune_->harmony[static_cast<std::size_t>(bar_index_)][0];
            static constexpr int kUp[]   = {0, 2, 4, 7};
            static constexpr int kDown[] = {7, 4, 2, 0};
            static constexpr int k5[]    = {0, 4};
            static constexpr int k8[]    = {0, 7};
            const int* pat = kUp;
            int n = 4;
            if (cur_tune_->arpeggio == Arpeggio::Down) { pat = kDown; n = 4; }
            else if (cur_tune_->arpeggio == Arpeggio::Int5) { pat = k5; n = 2; }
            else if (cur_tune_->arpeggio == Arpeggio::Int8) { pat = k8; n = 2; }
            for (int i = 0; i < n; ++i) {
                Pitch p = root;
                p.note = root.note + pat[i];
                p.beats = 1;
                arpeggio_.push_back(p);
            }
        }
    }

    if (cur_tune_->melody.empty()) return;
    const auto& bar = cur_tune_->melody[static_cast<std::size_t>(bar_index_)];
    const Pitch& a = bar[static_cast<std::size_t>(beat16_index_)];

    if (!blip_active_) ch1_ = {};
    ch2_ = {};

    if (a.beats > 0) {
        if (!a.blip_id.empty()) {
            auto it = game.blips.find(a.blip_id);
            if (it != game.blips.end()) {
                // Play as part of the melody without pausing the tune.
                const bool hold = music_paused_for_blip_;
                play_blip(it->second, game);
                music_paused_for_blip_ = hold;
            }
        } else if (!blip_active_) {
            emit_note(ch1_, a, cur_tune_->instrument_a, cur_tune_,
                      static_cast<int>(beat16_ms_));
        }
    }

    if (cur_tune_->arpeggio == Arpeggio::Off) {
        if (bar_index_ < static_cast<int>(cur_tune_->harmony.size())) {
            const Pitch& b =
                cur_tune_->harmony[static_cast<std::size_t>(bar_index_)]
                                  [static_cast<std::size_t>(beat16_index_)];
            if (b.beats > 0) {
                emit_note(ch2_, b, cur_tune_->instrument_b, cur_tune_,
                          static_cast<int>(beat16_ms_));
            }
        }
    } else if (!arpeggio_.empty()) {
        const Pitch& arp = arpeggio_[static_cast<std::size_t>(beat16_index_) % arpeggio_.size()];
        emit_note(ch2_, arp, cur_tune_->instrument_b, cur_tune_,
                  static_cast<int>(beat16_ms_));
    }
}

} // namespace citsy
