#ifndef AUDIO_STREAM_LILV_H
#define AUDIO_STREAM_LILV_H

#include "lilv_instance.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

class AudioStreamLilv : public AudioStream {
    GDCLASS(AudioStreamLilv, AudioStream)

private:
    friend class AudioStreamPlaybackLilv;
    String lilv_name;
    bool active;

    LilvInstance *get_lilv_instance();
    void lilv_layout_changed();
    void lilv_ready(String lilv_name);

public:
    virtual String get_stream_name() const;
    virtual float get_length() const;

    int process_sample(AudioFrame *p_buffer, float p_rate, int p_frames);

    AudioStreamLilv();
    ~AudioStreamLilv();

    virtual Ref<AudioStreamPlayback> _instantiate_playback() const override;
    void set_lilv_name(const String &name);
    const String &get_lilv_name() const;

    void set_active(bool active);
    bool is_active();

    bool _set(const StringName &p_name, const Variant &p_value);
    bool _get(const StringName &p_name, Variant &r_ret) const;
    void _get_property_list(List<PropertyInfo> *p_list) const;

protected:
    static void _bind_methods();
};
} // namespace godot

#endif
