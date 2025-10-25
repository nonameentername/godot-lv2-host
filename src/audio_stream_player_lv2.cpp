#include "audio_stream_player_lv2.h"
#include "audio_stream_lv2.h"

using namespace godot;

AudioStreamPlaybackLv2::AudioStreamPlaybackLv2() : active(false) {
}

AudioStreamPlaybackLv2::~AudioStreamPlaybackLv2() {
    _stop();
}

void AudioStreamPlaybackLv2::_stop() {
    active = false;
    base->set_active(active);
}

void AudioStreamPlaybackLv2::_start(double p_from_pos) {
    active = true;
    base->set_active(active);
}

void AudioStreamPlaybackLv2::_seek(double p_time) {
    if (p_time < 0) {
        p_time = 0;
    }
}

int AudioStreamPlaybackLv2::_mix(AudioFrame *p_buffer, float p_rate, int p_frames) {
    ERR_FAIL_COND_V(!active, 0);
    if (!active) {
        return 0;
    }

    return base->process_sample(p_buffer, p_rate, p_frames);
}

void AudioStreamPlaybackLv2::_tag_used_streams() {
}

int AudioStreamPlaybackLv2::_get_loop_count() const {
    return 10;
}

double AudioStreamPlaybackLv2::_get_playback_position() const {
    return 0;
}

float AudioStreamPlaybackLv2::_get_length() const {
    return 0.0;
}

bool AudioStreamPlaybackLv2::_is_playing() const {
    return active;
}

void AudioStreamPlaybackLv2::_bind_methods() {
}
