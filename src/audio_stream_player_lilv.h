#ifndef AUDIO_STREAM_PLAYER_LILV_H
#define AUDIO_STREAM_PLAYER_LILV_H

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/godot.hpp>

#include "audio_stream_lilv.h"

namespace godot {

class AudioStreamPlaybackLilv : public AudioStreamPlayback {
    GDCLASS(AudioStreamPlaybackLilv, AudioStreamPlayback)
    friend class AudioStreamLilv;

private:
    Ref<AudioStreamLilv> base;
    bool active;

public:
    static void _bind_methods();

    virtual void _start(double p_from_pos = 0.0) override;
    virtual void _stop() override;
    virtual bool _is_playing() const override;
    virtual int _get_loop_count() const override;
    virtual double _get_playback_position() const override;
    virtual void _seek(double p_time) override;
    virtual int _mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;
    virtual void _tag_used_streams() override;
    virtual float _get_length() const;
    AudioStreamPlaybackLilv();
    ~AudioStreamPlaybackLilv();
};
} // namespace godot

#endif
