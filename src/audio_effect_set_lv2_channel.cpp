#include "audio_effect_set_lv2_channel.h"
#include "godot_cpp/classes/audio_server.hpp"
#include "lv2_server.h"

using namespace godot;

AudioEffectSetLv2Channel::AudioEffectSetLv2Channel() {
    Lv2Server::get_singleton()->connect("layout_changed", Callable(this, "layout_changed"));
}

AudioEffectSetLv2Channel::~AudioEffectSetLv2Channel() {
}

void AudioEffectSetLv2Channel::set_instance_name(const String &name) {
    instance_name = name;
}

const String &AudioEffectSetLv2Channel::get_instance_name() const {
    for (int i = 0; i < Lv2Server::get_singleton()->get_instance_count(); i++) {
        if (Lv2Server::get_singleton()->get_instance_name(i) == instance_name) {
            return instance_name;
        }
    }

    static const String default_name = "Main";
    return default_name;
}

void AudioEffectSetLv2Channel::set_channel_left(int p_channel_left) {
    channel_left = p_channel_left;
}

int AudioEffectSetLv2Channel::get_channel_left() {
    return channel_left;
}

void AudioEffectSetLv2Channel::set_channel_right(int p_channel_right) {
    channel_right = p_channel_right;
}

int AudioEffectSetLv2Channel::get_channel_right() {
    return channel_right;
}

void AudioEffectSetLv2Channel::set_forward_audio(bool p_forward_audio) {
    forward_audio = p_forward_audio;
}

bool AudioEffectSetLv2Channel::get_forward_audio() {
    return forward_audio;
}

bool AudioEffectSetLv2Channel::_set(const StringName &p_name, const Variant &p_value) {
    if ((String)p_name == "instance_name") {
        set_instance_name(p_value);
        return true;
    }
    return false;
}

bool AudioEffectSetLv2Channel::_get(const StringName &p_name, Variant &r_ret) const {
    if ((String)p_name == "instance_name") {
        r_ret = get_instance_name();
        return true;
    }
    return false;
}

void AudioEffectSetLv2Channel::layout_changed() {
    notify_property_list_changed();
}

void AudioEffectSetLv2Channel::_get_property_list(List<PropertyInfo> *p_list) const {
    String options = Lv2Server::get_singleton()->get_name_options();
    p_list->push_back(PropertyInfo(Variant::STRING_NAME, "instance_name", PROPERTY_HINT_ENUM, options));
}

void AudioEffectSetLv2Channel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_instance_name", "name"), &AudioEffectSetLv2Channel::set_instance_name);
    ClassDB::bind_method(D_METHOD("get_instance_name"), &AudioEffectSetLv2Channel::get_instance_name);

    ClassDB::bind_method(D_METHOD("set_channel_left", "channel"), &AudioEffectSetLv2Channel::set_channel_left);
    ClassDB::bind_method(D_METHOD("get_channel_left"), &AudioEffectSetLv2Channel::get_channel_left);
    ClassDB::add_property("AudioEffectSetLv2Channel", PropertyInfo(Variant::INT, "channel_left"), "set_channel_left",
                          "get_channel_left");
    ClassDB::bind_method(D_METHOD("set_channel_right", "channel"), &AudioEffectSetLv2Channel::set_channel_right);
    ClassDB::bind_method(D_METHOD("get_channel_right"), &AudioEffectSetLv2Channel::get_channel_right);
    ClassDB::add_property("AudioEffectSetLv2Channel", PropertyInfo(Variant::INT, "channel_right"), "set_channel_right",
                          "get_channel_right");
    ClassDB::bind_method(D_METHOD("set_forward_audio", "forward_audio"), &AudioEffectSetLv2Channel::set_forward_audio);
    ClassDB::bind_method(D_METHOD("get_forward_audio"), &AudioEffectSetLv2Channel::get_forward_audio);
    ClassDB::add_property("AudioEffectSetLv2Channel", PropertyInfo(Variant::BOOL, "forward_audio"), "set_forward_audio",
                          "get_forward_audio");

    ClassDB::bind_method(D_METHOD("layout_changed"), &AudioEffectSetLv2Channel::layout_changed);
}

Ref<AudioEffectInstance> AudioEffectSetLv2Channel::_instantiate() {
    Ref<AudioEffectSetLv2ChannelInstance> ins;
    ins.instantiate();
    ins->base = Ref<AudioEffectSetLv2Channel>(this);

    return ins;
}

void AudioEffectSetLv2ChannelInstance::_process(const void *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
    AudioFrame *src_frames = (AudioFrame *)p_src_frames;

    for (int i = 0; i < p_frame_count; i++) {
        if (base->forward_audio) {
            p_dst_frames[i] = src_frames[i];
        } else {
            p_dst_frames[i].left = 0;
            p_dst_frames[i].right = 0;
        }
        if (src_frames[i].left > 0 || src_frames[i].right > 0) {
            has_data = true;
        }
    }

    if (!has_data) {
        return;
    }

    Lv2Instance *instance = Lv2Server::get_singleton()->get_instance(base->get_instance_name());
    if (instance != NULL) {
        int p_rate = 1;
        instance->set_channel_sample(src_frames, p_rate, p_frame_count, base->channel_left, base->channel_right);
    }
}

bool AudioEffectSetLv2ChannelInstance::_process_silence() const {
    return true;
}

void AudioEffectSetLv2ChannelInstance::_bind_methods() {
}
