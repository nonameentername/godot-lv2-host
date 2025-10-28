#ifndef AUDIO_STREAM_LV2_H
#define AUDIO_STREAM_LV2_H

#include "lv2_instance.h"

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

class AudioStreamLv2 : public AudioStream {
    GDCLASS(AudioStreamLv2, AudioStream)

private:
    friend class AudioStreamPlaybackLv2;
    String lv2_name;
    bool active;

    Lv2Instance *get_instance();
    void on_layout_changed();
    void on_lv2_ready(String lv2_name);

public:
    virtual String get_stream_name() const;
    virtual float get_length() const;

    int process_sample(AudioFrame *p_buffer, float p_rate, int p_frames);

    AudioStreamLv2();
    ~AudioStreamLv2();

    virtual Ref<AudioStreamPlayback> _instantiate_playback() const override;
    void set_instance_name(const String &name);
    const String &get_instance_name() const;

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
