#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "lilv_layout.h"
#include "lilv_server.h"
#include "lilv_server_node.h"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/os.hpp"
#include "godot_cpp/core/error_macros.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "version_generated.gen.h"

namespace godot {

LilvServer *LilvServer::singleton = NULL;

LilvServer::LilvServer() {
    world = lilv_world_new();
    initialized = false;
    layout_loaded = false;
    edited = false;
    singleton = this;
    exit_thread = false;
    call_deferred("initialize");
}

LilvServer::~LilvServer() {
    if (world) {
        lilv_world_free(world);
        world = nullptr;
    }
    singleton = NULL;
}

LilvWorld *LilvServer::get_lilv_world() {
    return world;
}

bool LilvServer::get_solo_mode() {
    return solo_mode;
}

void LilvServer::set_edited(bool p_edited) {
    edited = p_edited;
}

bool LilvServer::get_edited() {
    return edited;
}

LilvServer *LilvServer::get_singleton() {
    return singleton;
}

void LilvServer::add_property(String name, String default_value, GDExtensionVariantType extension_type,
                                PropertyHint hint) {
    if (godot::Engine::get_singleton()->is_editor_hint() && !ProjectSettings::get_singleton()->has_setting(name)) {
        ProjectSettings::get_singleton()->set_setting(name, default_value);
        Dictionary property_info;
        property_info["name"] = name;
        property_info["type"] = extension_type;
        property_info["hint"] = hint;
        property_info["hint_string"] = "";
        ProjectSettings::get_singleton()->add_property_info(property_info);
        ProjectSettings::get_singleton()->set_initial_value(name, default_value);
        Error error = ProjectSettings::get_singleton()->save();
        ERR_FAIL_COND_MSG(error != OK, "Could not save project settings");
    }
}

void LilvServer::initialize() {
    add_property("audio/lilv/default_lilv_layout", "res://default_lilv_layout.tres",
                 GDEXTENSION_VARIANT_TYPE_STRING, PROPERTY_HINT_FILE);
    add_property("audio/lilv/use_resource_files", "true", GDEXTENSION_VARIANT_TYPE_BOOL, PROPERTY_HINT_NONE);
    add_property("audio/lilv/hide_lilv_logs", "true", GDEXTENSION_VARIANT_TYPE_BOOL, PROPERTY_HINT_NONE);

    if (!load_default_lilv_layout()) {
        set_lilv_count(1);
    }

    set_edited(false);

    if (!initialized) {
        Node *lilv_server_node = memnew(LilvServerNode);
        SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
        tree->get_root()->add_child(lilv_server_node);
        lilv_server_node->set_process(true);
    }

    start();
    initialized = true;
}

void LilvServer::process() {
}

void LilvServer::thread_func() {
    int msdelay = 1000;
    while (!exit_thread) {
        if (!initialized) {
            continue;
        }

        lock();

        bool use_solo = false;
        for (int i = 0; i < lilv_instances.size(); i++) {
            if (lilv_instances[i]->solo == true) {
                use_solo = true;
            }
        }

        if (use_solo != solo_mode) {
            solo_mode = use_solo;
        }

        unlock();
        OS::get_singleton()->delay_usec(msdelay * 1000);
    }
}

void LilvServer::set_lilv_count(int p_count) {
    ERR_FAIL_COND(p_count < 1);
    ERR_FAIL_INDEX(p_count, 256);

    edited = true;

    int cb = lilv_instances.size();

    if (p_count < lilv_instances.size()) {
        for (int i = p_count; i < lilv_instances.size(); i++) {
            lilv_map.erase(lilv_instances[i]->lilv_name);
            memdelete(lilv_instances[i]);
        }
    }

    lilv_instances.resize(p_count);

    for (int i = cb; i < lilv_instances.size(); i++) {
        String attempt = "New Lilv";
        int attempts = 1;
        while (true) {
            bool name_free = true;
            for (int j = 0; j < i; j++) {
                if (lilv_instances[j]->lilv_name == attempt) {
                    name_free = false;
                    break;
                }
            }

            if (!name_free) {
                attempts++;
                attempt = "New Lilv " + itos(attempts);
            } else {
                break;
            }
        }

        if (i == 0) {
            attempt = "Main";
        }

        lilv_instances.write[i] = memnew(LilvInstance);
        lilv_instances[i]->lilv_name = attempt;
        lilv_instances[i]->solo = false;
        lilv_instances[i]->mute = false;
        lilv_instances[i]->bypass = false;
        lilv_instances[i]->volume_db = 0;
        lilv_instances[i]->tab = 0;

        lilv_map[attempt] = lilv_instances[i];

        if (!lilv_instances[i]->is_connected("lilv_ready", Callable(this, "on_lilv_ready"))) {
            lilv_instances[i]->connect("lilv_ready", Callable(this, "on_lilv_ready"), CONNECT_DEFERRED);
        }

    }

    emit_signal("lilv_layout_changed");
}

int LilvServer::get_lilv_count() const {
    if (!initialized) {
        return 0;
    }
    return lilv_instances.size();
}

void LilvServer::remove_lilv(int p_index) {
    ERR_FAIL_INDEX(p_index, lilv_instances.size());
    ERR_FAIL_COND(p_index == 0);

    edited = true;

    lilv_instances[p_index]->stop();
    lilv_map.erase(lilv_instances[p_index]->lilv_name);
    memdelete(lilv_instances[p_index]);
    lilv_instances.remove_at(p_index);

    emit_signal("lilv_layout_changed");
}

void LilvServer::add_lilv(int p_at_pos) {
    edited = true;

    if (p_at_pos >= lilv_instances.size()) {
        p_at_pos = -1;
    } else if (p_at_pos == 0) {
        if (lilv_instances.size() > 1) {
            p_at_pos = 1;
        } else {
            p_at_pos = -1;
        }
    }

    String attempt = "New Lilv";
    int attempts = 1;
    while (true) {
        bool name_free = true;
        for (int j = 0; j < lilv_instances.size(); j++) {
            if (lilv_instances[j]->lilv_name == attempt) {
                name_free = false;
                break;
            }
        }

        if (!name_free) {
            attempts++;
            attempt = "New Lilv " + itos(attempts);
        } else {
            break;
        }
    }

    LilvInstance *lilv_instance = memnew(LilvInstance);
    lilv_instance->lilv_name = attempt;
    lilv_instance->solo = false;
    lilv_instance->mute = false;
    lilv_instance->bypass = false;
    lilv_instance->volume_db = 0;
    lilv_instance->tab = 0;

    if (!lilv_instance->is_connected("lilv_ready", Callable(this, "on_lilv_ready"))) {
        lilv_instance->connect("lilv_ready", Callable(this, "on_lilv_ready"), CONNECT_DEFERRED);
    }

    lilv_map[attempt] = lilv_instance;

    if (p_at_pos == -1) {
        lilv_instances.push_back(lilv_instance);
    } else {
        lilv_instances.insert(p_at_pos, lilv_instance);
    }

    emit_signal("lilv_layout_changed");
}

void LilvServer::move_lilv(int p_lilv, int p_to_pos) {
    ERR_FAIL_COND(p_lilv < 1 || p_lilv >= lilv_instances.size());
    ERR_FAIL_COND(p_to_pos != -1 && (p_to_pos < 1 || p_to_pos > lilv_instances.size()));

    edited = true;

    if (p_lilv == p_to_pos) {
        return;
    }

    LilvInstance *lilv_instance = lilv_instances[p_lilv];
    lilv_instances.remove_at(p_lilv);

    if (p_to_pos == -1) {
        lilv_instances.push_back(lilv_instance);
    } else if (p_to_pos < p_lilv) {
        lilv_instances.insert(p_to_pos, lilv_instance);
    } else {
        lilv_instances.insert(p_to_pos - 1, lilv_instance);
    }

    emit_signal("lilv_layout_changed");
}

void LilvServer::set_lilv_name(int p_lilv, const String &p_name) {
    ERR_FAIL_INDEX(p_lilv, lilv_instances.size());
    if (p_lilv == 0 && p_name != "Main") {
        return; // lilv 0 is always main
    }

    edited = true;

    if (lilv_instances[p_lilv]->lilv_name == p_name) {
        return;
    }

    String attempt = p_name;
    int attempts = 1;

    while (true) {
        bool name_free = true;
        for (int i = 0; i < lilv_instances.size(); i++) {
            if (lilv_instances[i]->lilv_name == attempt) {
                name_free = false;
                break;
            }
        }

        if (name_free) {
            break;
        }

        attempts++;
        attempt = p_name + String(" ") + itos(attempts);
    }
    lilv_map.erase(lilv_instances[p_lilv]->lilv_name);
    lilv_instances[p_lilv]->lilv_name = attempt;
    lilv_map[attempt] = lilv_instances[p_lilv];

    emit_signal("lilv_layout_changed");
}

String LilvServer::get_lilv_name(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), String());
    return lilv_instances[p_lilv]->lilv_name;
}

int LilvServer::get_lilv_index(const StringName &p_lilv_name) const {
    for (int i = 0; i < lilv_instances.size(); ++i) {
        if (lilv_instances[i]->lilv_name == p_lilv_name) {
            return i;
        }
    }
    return -1;
}

String LilvServer::get_lilv_name_options() const {
    String options;
    for (int i = 0; i < get_lilv_count(); i++) {
        if (i > 0) {
            options += ",";
        }
        String name = get_lilv_name(i);
        options += name;
    }
    return options;
}

int LilvServer::get_lilv_channel_count(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), 0);
    return lilv_instances[p_lilv]->get_output_channel_count();
}

void LilvServer::set_lilv_volume_db(int p_lilv, float p_volume_db) {
    ERR_FAIL_INDEX(p_lilv, lilv_instances.size());

    edited = true;

    lilv_instances[p_lilv]->volume_db = p_volume_db;
}

float LilvServer::get_lilv_volume_db(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), 0);
    return lilv_instances[p_lilv]->volume_db;
}

void LilvServer::set_lilv_tab(int p_lilv, float p_tab) {
    ERR_FAIL_INDEX(p_lilv, lilv_instances.size());

    edited = true;

    lilv_instances[p_lilv]->tab = p_tab;
}

int LilvServer::get_lilv_tab(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), 0);
    return lilv_instances[p_lilv]->tab;
}

void LilvServer::set_lilv_solo(int p_lilv, bool p_enable) {
    ERR_FAIL_INDEX(p_lilv, lilv_instances.size());

    edited = true;

    lilv_instances[p_lilv]->solo = p_enable;
}

bool LilvServer::is_lilv_solo(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), false);

    return lilv_instances[p_lilv]->solo;
}

void LilvServer::set_lilv_mute(int p_lilv, bool p_enable) {
    ERR_FAIL_INDEX(p_lilv, lilv_instances.size());

    edited = true;

    lilv_instances[p_lilv]->mute = p_enable;
}

bool LilvServer::is_lilv_mute(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), false);

    return lilv_instances[p_lilv]->mute;
}

void LilvServer::set_lilv_bypass(int p_lilv, bool p_enable) {
    ERR_FAIL_INDEX(p_lilv, lilv_instances.size());

    edited = true;

    lilv_instances[p_lilv]->bypass = p_enable;
}

bool LilvServer::is_lilv_bypassing(int p_lilv) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), false);

    return lilv_instances[p_lilv]->bypass;
}

float LilvServer::get_lilv_channel_peak_volume_db(int p_lilv, int p_channel) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), 0);
    ERR_FAIL_INDEX_V(p_channel, lilv_instances[p_lilv]->output_channels.size(), 0);

    return lilv_instances[p_lilv]->output_channels[p_channel].peak_volume;
}

bool LilvServer::is_lilv_channel_active(int p_lilv, int p_channel) const {
    ERR_FAIL_INDEX_V(p_lilv, lilv_instances.size(), false);
    if (p_channel >= lilv_instances[p_lilv]->output_channels.size()) {
        return false;
    }

    ERR_FAIL_INDEX_V(p_channel, lilv_instances[p_lilv]->output_channels.size(), false);

    return lilv_instances[p_lilv]->output_channels[p_channel].active;
}

bool LilvServer::load_default_lilv_layout() {
    if (layout_loaded) {
        return true;
    }

    String layout_path =
        ProjectSettings::get_singleton()->get_setting("audio/lilv/default_lilv_layout");

    if (layout_path.is_empty() || layout_path.get_file() == "<null>") {
        layout_path = "res://default_lilv_layout.tres";
    }

    if (ResourceLoader::get_singleton()->exists(layout_path)) {
        Ref<LilvLayout> default_layout = ResourceLoader::get_singleton()->load(layout_path);
        if (default_layout.is_valid()) {
            set_lilv_layout(default_layout);
            emit_signal("lilv_layout_changed");
            return true;
        }
    }

    return false;
}

void LilvServer::set_lilv_layout(const Ref<LilvLayout> &p_lilv_layout) {
    ERR_FAIL_COND(p_lilv_layout.is_null() || p_lilv_layout->lilvs.size() == 0);

    int prev_size = lilv_instances.size();
    for (int i = prev_size; i < lilv_instances.size(); i++) {
        lilv_instances[i]->stop();
        memdelete(lilv_instances[i]);
    }
    lilv_instances.resize(p_lilv_layout->lilvs.size());
    lilv_map.clear();
    for (int i = 0; i < p_lilv_layout->lilvs.size(); i++) {
        LilvInstance *lilv;
        if (i >= prev_size) {
            lilv = memnew(LilvInstance);
        } else {
            lilv = lilv_instances[i];
            lilv->reset();
        }

        if (i == 0) {
            lilv->lilv_name = "Main";
        } else {
            lilv->lilv_name = p_lilv_layout->lilvs[i].name;
        }

        lilv->solo = p_lilv_layout->lilvs[i].solo;
        lilv->mute = p_lilv_layout->lilvs[i].mute;
        lilv->bypass = p_lilv_layout->lilvs[i].bypass;
        lilv->volume_db = p_lilv_layout->lilvs[i].volume_db;
        lilv_map[lilv->lilv_name] = lilv;
        lilv_instances.write[i] = lilv;

        lilv->call_deferred("initialize");
        if (!lilv->is_connected("lilv_ready", Callable(this, "on_lilv_ready"))) {
            lilv->connect("lilv_ready", Callable(this, "on_lilv_ready"), CONNECT_DEFERRED);
        }
    }
    edited = false;
    layout_loaded = true;
}

void LilvServer::on_lilv_ready(String lilv_name) {
    emit_signal("lilv_ready", lilv_name);
}

Ref<LilvLayout> LilvServer::generate_lilv_layout() const {
    Ref<LilvLayout> state;
    state.instantiate();

    state->lilvs.resize(lilv_instances.size());

    for (int i = 0; i < lilv_instances.size(); i++) {
        state->lilvs.write[i].name = lilv_instances[i]->lilv_name;
        state->lilvs.write[i].mute = lilv_instances[i]->mute;
        state->lilvs.write[i].solo = lilv_instances[i]->solo;
        state->lilvs.write[i].bypass = lilv_instances[i]->bypass;
        state->lilvs.write[i].volume_db = lilv_instances[i]->volume_db;
    }

    return state;
}

Error LilvServer::start() {
    thread_exited = false;
    thread.instantiate();
    mutex.instantiate();
    thread->start(callable_mp(this, &LilvServer::thread_func), Thread::PRIORITY_NORMAL);
    return OK;
}

void LilvServer::lock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->lock();
}

void LilvServer::unlock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->unlock();
}

void LilvServer::finish() {
    exit_thread = true;
    thread->wait_to_finish();
}

LilvInstance *LilvServer::get_lilv(const String &p_name) {
    if (lilv_map.has(p_name)) {
        return lilv_map.get(p_name);
    }

    return NULL;
}

LilvInstance *LilvServer::get_lilv_by_index(int p_index) {
    return lilv_instances.get(p_index);
}

LilvInstance *LilvServer::get_lilv_(const Variant &p_variant) {
    if (p_variant.get_type() == Variant::STRING) {
        String str = p_variant;
        return lilv_map.get(str);
    }

    if (p_variant.get_type() == Variant::INT) {
        int index = p_variant.operator int();
        return lilv_instances.get(index);
    }

    return NULL;
}

String LilvServer::get_version() {
    return GODOT_LILV_VERSION;
}

String LilvServer::get_build() {
    return GODOT_LILV_BUILD;
}

void LilvServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &LilvServer::initialize);

    ClassDB::bind_method(D_METHOD("get_version"), &LilvServer::get_version);
    ClassDB::bind_method(D_METHOD("get_build"), &LilvServer::get_build);

    ClassDB::bind_method(D_METHOD("set_edited", "edited"), &LilvServer::set_edited);
    ClassDB::bind_method(D_METHOD("get_edited"), &LilvServer::get_edited);

    ClassDB::bind_method(D_METHOD("set_lilv_count", "amount"), &LilvServer::set_lilv_count);
    ClassDB::bind_method(D_METHOD("get_lilv_count"), &LilvServer::get_lilv_count);

    ClassDB::bind_method(D_METHOD("remove_lilv", "index"), &LilvServer::remove_lilv);
    ClassDB::bind_method(D_METHOD("add_lilv", "at_position"), &LilvServer::add_lilv, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("move_lilv", "index", "to_index"), &LilvServer::move_lilv);

    ClassDB::bind_method(D_METHOD("set_lilv_name", "lilv_idx", "name"), &LilvServer::set_lilv_name);
    ClassDB::bind_method(D_METHOD("get_lilv_name", "lilv_idx"), &LilvServer::get_lilv_name);
    ClassDB::bind_method(D_METHOD("get_lilv_index", "lilv_name"), &LilvServer::get_lilv_index);

    ClassDB::bind_method(D_METHOD("get_lilv_name_options"), &LilvServer::get_lilv_name_options);

    ClassDB::bind_method(D_METHOD("get_lilv_channel_count", "lilv_idx"), &LilvServer::get_lilv_channel_count);

    ClassDB::bind_method(D_METHOD("set_lilv_volume_db", "lilv_idx", "volume_db"),
                         &LilvServer::set_lilv_volume_db);
    ClassDB::bind_method(D_METHOD("get_lilv_volume_db", "lilv_idx"), &LilvServer::get_lilv_volume_db);

    ClassDB::bind_method(D_METHOD("set_lilv_tab", "lilv_idx", "tab"), &LilvServer::set_lilv_tab);
    ClassDB::bind_method(D_METHOD("get_lilv_tab", "lilv_idx"), &LilvServer::get_lilv_tab);

    ClassDB::bind_method(D_METHOD("set_lilv_solo", "lilv_idx", "enable"), &LilvServer::set_lilv_solo);
    ClassDB::bind_method(D_METHOD("is_lilv_solo", "lilv_idx"), &LilvServer::is_lilv_solo);

    ClassDB::bind_method(D_METHOD("set_lilv_mute", "lilv_idx", "enable"), &LilvServer::set_lilv_mute);
    ClassDB::bind_method(D_METHOD("is_lilv_mute", "lilv_idx"), &LilvServer::is_lilv_mute);

    ClassDB::bind_method(D_METHOD("set_lilv_bypass", "lilv_idx", "enable"), &LilvServer::set_lilv_bypass);
    ClassDB::bind_method(D_METHOD("is_lilv_bypassing", "lilv_idx"), &LilvServer::is_lilv_bypassing);

    ClassDB::bind_method(D_METHOD("get_lilv_channel_peak_volume_db", "lilv_idx", "channel"),
                         &LilvServer::get_lilv_channel_peak_volume_db);

    ClassDB::bind_method(D_METHOD("is_lilv_channel_active", "lilv_idx", "channel"),
                         &LilvServer::is_lilv_channel_active);

    ClassDB::bind_method(D_METHOD("lock"), &LilvServer::lock);
    ClassDB::bind_method(D_METHOD("unlock"), &LilvServer::unlock);

    ClassDB::bind_method(D_METHOD("set_lilv_layout", "lilv_layout"), &LilvServer::set_lilv_layout);
    ClassDB::bind_method(D_METHOD("generate_lilv_layout"), &LilvServer::generate_lilv_layout);

    ClassDB::bind_method(D_METHOD("get_lilv", "lilv_name"), &LilvServer::get_lilv);

    ClassDB::bind_method(D_METHOD("on_lilv_ready", "lilv_name"), &LilvServer::on_lilv_ready);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "lilv_count"), "set_lilv_count", "get_lilv_count");

    ADD_SIGNAL(MethodInfo("lilv_layout_changed"));
    ADD_SIGNAL(MethodInfo("lilv_ready", PropertyInfo(Variant::STRING, "lilv_name")));
}

}
