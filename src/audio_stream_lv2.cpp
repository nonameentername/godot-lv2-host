#include "audio_stream_lv2.h"
#include "audio_stream_player_lv2.h"
#include "lv2_server.h"
#include "godot_cpp/classes/audio_stream.hpp"

namespace godot {

AudioStreamLv2::AudioStreamLv2() {
    Lv2Server::get_singleton()->connect("lv2_layout_changed", Callable(this, "lv2_layout_changed"));
    Lv2Server::get_singleton()->connect("lv2_ready", Callable(this, "lv2_ready"));
    active = false;
}

AudioStreamLv2::~AudioStreamLv2() {
}

Ref<AudioStreamPlayback> AudioStreamLv2::_instantiate_playback() const {
    Ref<AudioStreamPlaybackLv2> talking_tree;
    talking_tree.instantiate();
    talking_tree->base = Ref<AudioStreamLv2>(this);

    return talking_tree;
}

void AudioStreamLv2::set_active(bool p_active) {
    active = p_active;

    Lv2Instance *lv2_instance = get_lv2_instance();
    if (lv2_instance != NULL) {
        lv2_instance->set_active(active);
    }
}

bool AudioStreamLv2::is_active() {
    Lv2Instance *lv2_instance = get_lv2_instance();
    if (lv2_instance != NULL) {
        return lv2_instance->is_active();
    }
    return false;
}

void AudioStreamLv2::set_lv2_name(const String &name) {
    lv2_name = name;
}

const String &AudioStreamLv2::get_lv2_name() const {
    static const String default_name = "Main";
    if (lv2_name.length() == 0) {
        return default_name;
    }

    for (int i = 0; i < Lv2Server::get_singleton()->get_lv2_count(); i++) {
        if (Lv2Server::get_singleton()->get_lv2_name(i) == lv2_name) {
            return lv2_name;
        }
    }

    return default_name;
}

String AudioStreamLv2::get_stream_name() const {
    return "Lv2";
}

float AudioStreamLv2::get_length() const {
    return 0;
}

bool AudioStreamLv2::_set(const StringName &p_name, const Variant &p_value) {
    if ((String)p_name == "lv2_name") {
        set_lv2_name(p_value);
        return true;
    }
    return false;
}

bool AudioStreamLv2::_get(const StringName &p_name, Variant &r_ret) const {
    if ((String)p_name == "lv2_name") {
        r_ret = get_lv2_name();
        return true;
    }
    return false;
}

void AudioStreamLv2::lv2_layout_changed() {
    notify_property_list_changed();
}

void AudioStreamLv2::lv2_ready(String p_lv2_name) {
    if (get_lv2_name() == p_lv2_name) {
        Lv2Instance *lv2_instance = get_lv2_instance();
        if (lv2_instance != NULL) {
            lv2_instance->set_active(active);
        }
    }
}

Lv2Instance *AudioStreamLv2::get_lv2_instance() {
    Lv2Server *lv2_server = (Lv2Server *)Engine::get_singleton()->get_singleton("Lv2Server");
    if (lv2_server != NULL) {
        Lv2Instance *lv2_instance = lv2_server->get_lv2(get_lv2_name());
        return lv2_instance;
    }
    return NULL;
}

int AudioStreamLv2::process_sample(AudioFrame *p_buffer, float p_rate, int p_frames) {
    Lv2Instance *lv2_instance = get_lv2_instance();
    if (lv2_instance != NULL) {
        return lv2_instance->process_sample(p_buffer, p_rate, p_frames);
    }

    for (int frame = 0; frame < p_frames; frame += 1) {
        p_buffer[frame].left = 0;
        p_buffer[frame].right = 0;
    }

    return p_frames;
}

void AudioStreamLv2::_get_property_list(List<PropertyInfo> *p_list) const {
    String options = Lv2Server::get_singleton()->get_lv2_name_options();
    p_list->push_back(PropertyInfo(Variant::STRING_NAME, "lv2_name", PROPERTY_HINT_ENUM, options));
}

void AudioStreamLv2::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_stream_name"), &AudioStreamLv2::get_stream_name);
    ClassDB::bind_method(D_METHOD("set_lv2_name", "name"), &AudioStreamLv2::set_lv2_name);
    ClassDB::bind_method(D_METHOD("get_lv2_name"), &AudioStreamLv2::get_lv2_name);
    ClassDB::bind_method(D_METHOD("lv2_layout_changed"), &AudioStreamLv2::lv2_layout_changed);
    ClassDB::bind_method(D_METHOD("lv2_ready", "lv2_name"), &AudioStreamLv2::lv2_ready);
}

}
