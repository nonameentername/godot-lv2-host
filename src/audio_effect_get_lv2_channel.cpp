#include "audio_effect_get_lv2_channel.h"
#include "lv2_server.h"
#include "godot_cpp/classes/audio_server.hpp"
#include <algorithm>

using namespace godot;

AudioEffectGetLv2Channel::AudioEffectGetLv2Channel() {
    Lv2Server::get_singleton()->connect("layout_changed", Callable(this, "layout_changed"));
}

AudioEffectGetLv2Channel::~AudioEffectGetLv2Channel() {
}

void AudioEffectGetLv2Channel::set_instance_name(const String &name) {
    instance_name = name;
}

const String &AudioEffectGetLv2Channel::get_instance_name() const {
    for (int i = 0; i < Lv2Server::get_singleton()->get_instance_count(); i++) {
        if (Lv2Server::get_singleton()->get_instance_name(i) == instance_name) {
            return instance_name;
        }
    }

    static const String default_name = "Main";
    return default_name;
}

void AudioEffectGetLv2Channel::set_channel_left(int p_channel_left) {
    channel_left = p_channel_left;
}

int AudioEffectGetLv2Channel::get_channel_left() {
    return channel_left;
}

void AudioEffectGetLv2Channel::set_channel_right(int p_channel_right) {
    channel_right = p_channel_right;
}

int AudioEffectGetLv2Channel::get_channel_right() {
    return channel_right;
}

void AudioEffectGetLv2Channel::set_mix(float p_mix) {
    mix = fmax(fmin(p_mix, 1), 0);
}

float AudioEffectGetLv2Channel::get_mix() {
    return mix;
}

bool AudioEffectGetLv2Channel::_set(const StringName &p_name, const Variant &p_value) {
    if ((String)p_name == "instance_name") {
        set_instance_name(p_value);
        return true;
    }
    return false;
}

bool AudioEffectGetLv2Channel::_get(const StringName &p_name, Variant &r_ret) const {
    if ((String)p_name == "instance_name") {
        r_ret = get_instance_name();
        return true;
    }
    return false;
}

void AudioEffectGetLv2Channel::layout_changed() {
    notify_property_list_changed();
}

void AudioEffectGetLv2Channel::_get_property_list(List<PropertyInfo> *p_list) const {
    String options = Lv2Server::get_singleton()->get_name_options();
    p_list->push_back(PropertyInfo(Variant::STRING_NAME, "instance_name", PROPERTY_HINT_ENUM, options));
}

void AudioEffectGetLv2Channel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_instance_name", "name"), &AudioEffectGetLv2Channel::set_instance_name);
    ClassDB::bind_method(D_METHOD("get_instance_name"), &AudioEffectGetLv2Channel::get_instance_name);

    ClassDB::bind_method(D_METHOD("set_channel_left", "channel"), &AudioEffectGetLv2Channel::set_channel_left);
    ClassDB::bind_method(D_METHOD("get_channel_left"), &AudioEffectGetLv2Channel::get_channel_left);
    ClassDB::add_property("AudioEffectGetLv2Channel", PropertyInfo(Variant::INT, "channel_left"), "set_channel_left",
                          "get_channel_left");
    ClassDB::bind_method(D_METHOD("set_channel_right", "channel"), &AudioEffectGetLv2Channel::set_channel_right);
    ClassDB::bind_method(D_METHOD("get_channel_right"), &AudioEffectGetLv2Channel::get_channel_right);
    ClassDB::add_property("AudioEffectGetLv2Channel", PropertyInfo(Variant::INT, "channel_right"),
                          "set_channel_right", "get_channel_right");
    ClassDB::bind_method(D_METHOD("set_mix", "mix"),
                         &AudioEffectGetLv2Channel::set_mix);
    ClassDB::bind_method(D_METHOD("get_mix"), &AudioEffectGetLv2Channel::get_mix);
    ClassDB::add_property("AudioEffectGetLv2Channel", PropertyInfo(Variant::FLOAT, "mix", PROPERTY_HINT_RANGE, "0,1,0.001"),
                          "set_mix", "get_mix");

    ClassDB::bind_method(D_METHOD("layout_changed"), &AudioEffectGetLv2Channel::layout_changed);
}

Ref<AudioEffectInstance> AudioEffectGetLv2Channel::_instantiate() {
    Ref<AudioEffectGetLv2ChannelInstance> ins;
    ins.instantiate();
    ins->base = Ref<AudioEffectGetLv2Channel>(this);

    return ins;
}

void AudioEffectGetLv2ChannelInstance::_process(const void *p_src_frames, AudioFrame *p_dst_frames,
                                                   int p_frame_count) {
    AudioFrame *src_frames = (AudioFrame *)p_src_frames;

    if (temp_buffer.size() != p_frame_count) {
        temp_buffer.resize(p_frame_count);
    }

    Lv2Instance *instance = Lv2Server::get_singleton()->get_instance(base->get_instance_name());
    if (instance != NULL) {
        int p_rate = 1;
        instance->get_channel_sample(temp_buffer.ptrw(), p_rate, p_frame_count, base->channel_left, base->channel_right);
    }

    for (int i = 0; i < p_frame_count; i++) {
        p_dst_frames[i].left = (1 - base->mix) * src_frames[i].left + (base->mix) * temp_buffer[i].left;
        p_dst_frames[i].right = (1 - base->mix) * src_frames[i].left + (base->mix) * temp_buffer[i].right;
    }
}

bool AudioEffectGetLv2ChannelInstance::_process_silence() const {
    return true;
}

void AudioEffectGetLv2ChannelInstance::_bind_methods() {
}
