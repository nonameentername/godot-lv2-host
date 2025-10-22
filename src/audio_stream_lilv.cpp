#include "audio_stream_lilv.h"
#include "audio_stream_player_lilv.h"
#include "lilv_server.h"
#include "godot_cpp/classes/audio_stream.hpp"

namespace godot {

AudioStreamLilv::AudioStreamLilv() {
    LilvServer::get_singleton()->connect("lilv_layout_changed", Callable(this, "lilv_layout_changed"));
    LilvServer::get_singleton()->connect("lilv_ready", Callable(this, "lilv_ready"));
    active = false;
}

AudioStreamLilv::~AudioStreamLilv() {
}

Ref<AudioStreamPlayback> AudioStreamLilv::_instantiate_playback() const {
    Ref<AudioStreamPlaybackLilv> talking_tree;
    talking_tree.instantiate();
    talking_tree->base = Ref<AudioStreamLilv>(this);

    return talking_tree;
}

void AudioStreamLilv::set_active(bool p_active) {
    active = p_active;

    LilvInstance *lilv_instance = get_lilv_instance();
    if (lilv_instance != NULL) {
        lilv_instance->set_active(active);
    }
}

bool AudioStreamLilv::is_active() {
    LilvInstance *lilv_instance = get_lilv_instance();
    if (lilv_instance != NULL) {
        return lilv_instance->is_active();
    }
    return false;
}

void AudioStreamLilv::set_lilv_name(const String &name) {
    lilv_name = name;
}

const String &AudioStreamLilv::get_lilv_name() const {
    static const String default_name = "Main";
    if (lilv_name.length() == 0) {
        return default_name;
    }

    for (int i = 0; i < LilvServer::get_singleton()->get_lilv_count(); i++) {
        if (LilvServer::get_singleton()->get_lilv_name(i) == lilv_name) {
            return lilv_name;
        }
    }

    return default_name;
}

String AudioStreamLilv::get_stream_name() const {
    return "Lilv";
}

float AudioStreamLilv::get_length() const {
    return 0;
}

bool AudioStreamLilv::_set(const StringName &p_name, const Variant &p_value) {
    if ((String)p_name == "lilv_name") {
        set_lilv_name(p_value);
        return true;
    }
    return false;
}

bool AudioStreamLilv::_get(const StringName &p_name, Variant &r_ret) const {
    if ((String)p_name == "lilv_name") {
        r_ret = get_lilv_name();
        return true;
    }
    return false;
}

void AudioStreamLilv::lilv_layout_changed() {
    notify_property_list_changed();
}

void AudioStreamLilv::lilv_ready(String p_lilv_name) {
    if (get_lilv_name() == p_lilv_name) {
        LilvInstance *lilv_instance = get_lilv_instance();
        if (lilv_instance != NULL) {
            lilv_instance->set_active(active);
        }
    }
}

LilvInstance *AudioStreamLilv::get_lilv_instance() {
    LilvServer *lilv_server = (LilvServer *)Engine::get_singleton()->get_singleton("LilvServer");
    if (lilv_server != NULL) {
        LilvInstance *lilv_instance = lilv_server->get_lilv(get_lilv_name());
        return lilv_instance;
    }
    return NULL;
}

int AudioStreamLilv::process_sample(AudioFrame *p_buffer, float p_rate, int p_frames) {
    LilvInstance *lilv_instance = get_lilv_instance();
    if (lilv_instance != NULL) {
        return lilv_instance->process_sample(p_buffer, p_rate, p_frames);
    }

    for (int frame = 0; frame < p_frames; frame += 1) {
        p_buffer[frame].left = 0;
        p_buffer[frame].right = 0;
    }

    return p_frames;
}

void AudioStreamLilv::_get_property_list(List<PropertyInfo> *p_list) const {
    String options = LilvServer::get_singleton()->get_lilv_name_options();
    p_list->push_back(PropertyInfo(Variant::STRING_NAME, "lilv_name", PROPERTY_HINT_ENUM, options));
}

void AudioStreamLilv::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_stream_name"), &AudioStreamLilv::get_stream_name);
    ClassDB::bind_method(D_METHOD("set_lilv_name", "name"), &AudioStreamLilv::set_lilv_name);
    ClassDB::bind_method(D_METHOD("get_lilv_name"), &AudioStreamLilv::get_lilv_name);
    ClassDB::bind_method(D_METHOD("lilv_layout_changed"), &AudioStreamLilv::lilv_layout_changed);
    ClassDB::bind_method(D_METHOD("lilv_ready", "lilv_name"), &AudioStreamLilv::lilv_ready);
}

}
