#include "audio_stream_lv2_channel.h"
#include "audio_stream_player_lv2_channel.h"
#include "lv2_instance.h"
#include "lv2_server.h"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/string_name.hpp"

using namespace godot;

AudioStreamLv2Channel::AudioStreamLv2Channel() {
    Lv2Server::get_singleton()->connect("layout_changed", Callable(this, "layout_changed"));
    channel_left = 0;
    channel_right = 1;
}

AudioStreamLv2Channel::~AudioStreamLv2Channel() {
}

void AudioStreamLv2Channel::set_instance_name(const String &name) {
    instance_name = name;
}

const String &AudioStreamLv2Channel::get_instance_name() const {
    static const String default_name = "Main";
    if (instance_name.length() == 0) {
        return default_name;
    }

    for (int i = 0; i < Lv2Server::get_singleton()->get_instance_count(); i++) {
        if (Lv2Server::get_singleton()->get_instance_name(i) == instance_name) {
            return instance_name;
        }
    }

    return default_name;
}

String AudioStreamLv2Channel::get_stream_name() const {
    return "Lv2Channel";
}

float AudioStreamLv2Channel::get_length() const {
    return 0;
}

Ref<AudioStreamPlayback> AudioStreamLv2Channel::_instantiate_playback() const {
    Ref<AudioStreamPlaybackLv2Channel> talking_tree;
    talking_tree.instantiate();
    talking_tree->base = Ref<AudioStreamLv2Channel>(this);
    return talking_tree;
}

int AudioStreamLv2Channel::process_sample(AudioFrame *p_buffer, float p_rate, int p_frames) {
    Lv2Instance *instance = Lv2Server::get_singleton()->get_instance(get_instance_name());
    if (instance != NULL && instance->is_active()) {
        return instance->get_channel_sample(p_buffer, p_rate, p_frames, channel_left, channel_right);
    }

    for (int frame = 0; frame < p_frames; frame += 1) {
        p_buffer[frame].left = 0;
        p_buffer[frame].right = 0;
    }

    return p_frames;
}

void AudioStreamLv2Channel::set_channel_left(int p_channel_left) {
    channel_left = p_channel_left;
}

int AudioStreamLv2Channel::get_channel_left() {
    return channel_left;
}

void AudioStreamLv2Channel::set_channel_right(int p_channel_right) {
    channel_right = p_channel_right;
}

int AudioStreamLv2Channel::get_channel_right() {
    return channel_right;
}

bool AudioStreamLv2Channel::_set(const StringName &p_name, const Variant &p_value) {
    if ((String)p_name == "instance_name") {
        set_instance_name(p_value);
        return true;
    }
    return false;
}

bool AudioStreamLv2Channel::_get(const StringName &p_name, Variant &r_ret) const {
    if ((String)p_name == "instance_name") {
        r_ret = get_instance_name();
        return true;
    }
    return false;
}

void AudioStreamLv2Channel::layout_changed() {
    notify_property_list_changed();
}

void AudioStreamLv2Channel::_get_property_list(List<PropertyInfo> *p_list) const {
    String options = Lv2Server::get_singleton()->get_name_options();
    p_list->push_back(PropertyInfo(Variant::STRING_NAME, "instance_name", PROPERTY_HINT_ENUM, options));
}

void AudioStreamLv2Channel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_stream_name"), &AudioStreamLv2Channel::get_stream_name);
    ClassDB::bind_method(D_METHOD("set_instance_name", "name"), &AudioStreamLv2Channel::set_instance_name);
    ClassDB::bind_method(D_METHOD("get_instance_name"), &AudioStreamLv2Channel::get_instance_name);

    ClassDB::bind_method(D_METHOD("set_channel_left", "channel"), &AudioStreamLv2Channel::set_channel_left);
    ClassDB::bind_method(D_METHOD("get_channel_left"), &AudioStreamLv2Channel::get_channel_left);
    ClassDB::add_property("AudioStreamLv2Channel", PropertyInfo(Variant::INT, "channel_left"), "set_channel_left",
                          "get_channel_left");

    ClassDB::bind_method(D_METHOD("set_channel_right", "channel"), &AudioStreamLv2Channel::set_channel_right);
    ClassDB::bind_method(D_METHOD("get_channel_right"), &AudioStreamLv2Channel::get_channel_right);
    ClassDB::add_property("AudioStreamLv2Channel", PropertyInfo(Variant::INT, "channel_right"), "set_channel_right",
                          "get_channel_right");

    ClassDB::bind_method(D_METHOD("layout_changed"), &AudioStreamLv2Channel::layout_changed);
}
