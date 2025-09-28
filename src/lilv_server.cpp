#include "lilv_server.h"
#include "lilv_server_node.h"
#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/classes/audio_server.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/os.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/time.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "version_generated.gen.h"
#include <cstdlib>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <unistd.h>

using namespace godot;

LilvServer *LilvServer::singleton = NULL;

LilvServer::LilvServer() {
    world = lilv_world_new();

	LilvNode* lv2_path = lilv_new_string(world, "~/.lv2");
	lilv_world_set_option(world, LILV_OPTION_LV2_PATH, lv2_path);
  	lilv_node_free(lv2_path);

    lilv_world_load_all(world);

    const LilvPlugins* plugins = lilv_world_get_all_plugins(world);

	LILV_FOREACH (plugins, i, plugins) {
		const LilvPlugin* p = lilv_plugins_get(plugins, i);
		LilvNode* n = lilv_plugin_get_name(p);
		printf("%s\n", lilv_node_as_string(n));
		lilv_node_free(n);
	}
}

LilvServer::~LilvServer() {
}

LilvServer *LilvServer::get_singleton() {
    return singleton;
}

void LilvServer::initialize() {
    if (!initialized) {
        Node *lilv_server_node = memnew(LilvServerNode);
        SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
        tree->get_root()->add_child(lilv_server_node);
        lilv_server_node->set_process(true);
    }

    start();
    initialized = true;
}

void LilvServer::audio_thread_func() {
}

void LilvServer::process() {
}

void LilvServer::note_on(int p_channel, int p_note, int p_velocity) {
}

void LilvServer::note_off(int p_channel, int p_note, int p_velocity) {
}

void LilvServer::program_change(int p_channel, int p_program_number) {
}

void LilvServer::control_change(int p_channel, int p_controller, int p_value) {
}

void LilvServer::pitch_bend(int p_channel, int p_value) {
}

void LilvServer::channel_pressure(int p_channel, int p_pressure) {
}

void LilvServer::midi_poly_aftertouch(int p_channel, int p_note, int p_pressure) {
}

int LilvServer::process_sample(AudioFrame *p_buffer, float p_rate, int p_frames) {
    return p_frames;
}

Error LilvServer::start() {
    if (!godot::Engine::get_singleton()->is_editor_hint()) {
        audio_thread.instantiate();
        audio_thread->start(callable_mp(this, &LilvServer::audio_thread_func), Thread::PRIORITY_HIGH);
    }
    return OK;
}

void LilvServer::lock_audio() {
    if (audio_thread.is_null() || audio_mutex.is_null()) {
        return;
    }
    audio_mutex->lock();
}

void LilvServer::unlock_audio() {
    if (audio_thread.is_null() || audio_mutex.is_null()) {
        return;
    }
    audio_mutex->unlock();
}

void LilvServer::finish() {
    if (!godot::Engine::get_singleton()->is_editor_hint()) {
        exit_thread = true;
        audio_thread->wait_to_finish();
    }
}

godot::String LilvServer::get_version() {
    return GODOT_DISTRHO_VERSION;
}

godot::String LilvServer::get_build() {
    return GODOT_DISTRHO_BUILD;
}

void LilvServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &LilvServer::initialize);

    ClassDB::bind_method(D_METHOD("note_on", "channel", "note", "velocity"), &LilvServer::note_on);
    ClassDB::bind_method(D_METHOD("note_off", "channel", "note", "velocity"), &LilvServer::note_off);
    ClassDB::bind_method(D_METHOD("program_change", "channel", "program_number"), &LilvServer::program_change);
    ClassDB::bind_method(D_METHOD("control_change", "channel", "controller", "value"),
                         &LilvServer::control_change);
    ClassDB::bind_method(D_METHOD("midi_poly_aftertouch", "channel", "note", "pressure"),
                         &LilvServer::midi_poly_aftertouch);

    ClassDB::bind_method(D_METHOD("get_version"), &LilvServer::get_version);
    ClassDB::bind_method(D_METHOD("get_build"), &LilvServer::get_build);
}
