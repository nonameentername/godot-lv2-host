#include "audio_stream_player_lv2_channel.h"
#include "audio_stream_lv2_channel.h"

using namespace godot;

AudioStreamPlaybackLv2Channel::AudioStreamPlaybackLv2Channel() : active(false) {
}

AudioStreamPlaybackLv2Channel::~AudioStreamPlaybackLv2Channel() {
}

void AudioStreamPlaybackLv2Channel::_stop() {
    active = false;
}

void AudioStreamPlaybackLv2Channel::_start(double p_from_pos) {
    active = true;
}

void AudioStreamPlaybackLv2Channel::_seek(double p_time) {
    if (p_time < 0) {
        p_time = 0;
    }
}

int AudioStreamPlaybackLv2Channel::_mix(AudioFrame *p_buffer, float p_rate, int p_frames) {
    ERR_FAIL_COND_V(!active, 0);
    if (!active) {
        return 0;
    }

    return base->process_sample(p_buffer, p_rate, p_frames);
}

int AudioStreamPlaybackLv2Channel::_get_loop_count() const {
    return 10;
}

double AudioStreamPlaybackLv2Channel::_get_playback_position() const {
    return 0;
}

float AudioStreamPlaybackLv2Channel::_get_length() const {
    return 0.0;
}

bool AudioStreamPlaybackLv2Channel::_is_playing() const {
    return active;
}

void AudioStreamPlaybackLv2Channel::_bind_methods() {
}
