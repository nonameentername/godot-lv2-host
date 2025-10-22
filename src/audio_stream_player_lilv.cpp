#include "audio_stream_player_lilv.h"
#include "audio_stream_lilv.h"

using namespace godot;

AudioStreamPlaybackLilv::AudioStreamPlaybackLilv() : active(false) {
}

AudioStreamPlaybackLilv::~AudioStreamPlaybackLilv() {
    _stop();
}

void AudioStreamPlaybackLilv::_stop() {
    active = false;
    base->set_active(active);
}

void AudioStreamPlaybackLilv::_start(double p_from_pos) {
    active = true;
    base->set_active(active);
}

void AudioStreamPlaybackLilv::_seek(double p_time) {
    if (p_time < 0) {
        p_time = 0;
    }
}

int AudioStreamPlaybackLilv::_mix(AudioFrame *p_buffer, float p_rate, int p_frames) {
    ERR_FAIL_COND_V(!active, 0);
    if (!active) {
        return 0;
    }

    return base->process_sample(p_buffer, p_rate, p_frames);
}

void AudioStreamPlaybackLilv::_tag_used_streams() {
}

int AudioStreamPlaybackLilv::_get_loop_count() const {
    return 10;
}

double AudioStreamPlaybackLilv::_get_playback_position() const {
    return 0;
}

float AudioStreamPlaybackLilv::_get_length() const {
    return 0.0;
}

bool AudioStreamPlaybackLilv::_is_playing() const {
    return active;
}

void AudioStreamPlaybackLilv::_bind_methods() {
}
