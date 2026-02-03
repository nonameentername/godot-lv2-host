#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/os.hpp"
#include "godot_cpp/core/error_macros.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "lilv/lilv.h"
#include "lv2_layout.h"
#include "lv2_server.h"
#include "lv2_server_node.h"
#include "version_generated.gen.h"
#include <iostream>

namespace godot {

Lv2Server *Lv2Server::singleton = NULL;

Lv2Server::Lv2Server() {
    world = lilv_world_new();
    initialized = false;
    layout_loaded = false;
    edited = false;
    singleton = this;
    exit_thread = false;
    call_deferred("initialize");
}

Lv2Server::~Lv2Server() {
    finish();

    for (int i = 0; i < instances.size(); i++) {
        if (instances[i]) {
            instances[i]->stop();
            memdelete(instances[i]);
        }
    }

    instances.clear();
    instance_map.clear();

    if (lv2_host != NULL) {
        delete lv2_host;
        lv2_host = NULL;
    }

    if (world) {
        lilv_world_free(world);
        world = nullptr;
    }
    singleton = NULL;
}

LilvWorld *Lv2Server::get_lilv_world() {
    return world;
}

bool Lv2Server::get_solo_mode() {
    return solo_mode;
}

void Lv2Server::set_edited(bool p_edited) {
    edited = p_edited;
}

bool Lv2Server::get_edited() {
    return edited;
}

Lv2Server *Lv2Server::get_singleton() {
    return singleton;
}

void Lv2Server::add_property(String name, String default_value, GDExtensionVariantType extension_type,
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

void Lv2Server::initialize() {
    int mix_rate = AudioServer::get_singleton()->get_mix_rate();
    // TODO: update block size
    lv2_host = new Lv2Host(world, mix_rate, 512);

    add_property("audio/lv2-host/default_lv2_layout", "res://default_lv2_layout.tres", GDEXTENSION_VARIANT_TYPE_STRING,
                 PROPERTY_HINT_FILE);
    add_property("audio/lv2-host/lv2_path", "", GDEXTENSION_VARIANT_TYPE_STRING, PROPERTY_HINT_DIR);
    add_property("audio/lv2-host/hide_lv2_logs", "true", GDEXTENSION_VARIANT_TYPE_BOOL, PROPERTY_HINT_NONE);

    String lv2_path = ProjectSettings::get_singleton()->get_setting("audio/lv2-host/lv2_path");

    if (lv2_path.length() > 0 && lv2_path.is_absolute_path()) {
        lv2_path = ProjectSettings::get_singleton()->globalize_path(lv2_path);

        LilvNode *lv2_node_path = lilv_new_string(world, lv2_path.ascii());
        lilv_world_set_option(world, LILV_OPTION_LV2_PATH, lv2_node_path);
        lilv_node_free(lv2_node_path);
    }

    if (!lv2_host->load_world()) {
        // TODO: log to godot
        std::cerr << "Failed to create/load lv2 world\n";
    }

    if (!load_default_layout()) {
        set_instance_count(1);
    }

    set_edited(false);

    if (!initialized) {
        Node *server_node = memnew(Lv2ServerNode);
        SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
        tree->get_root()->add_child(server_node);
        server_node->set_process(true);
    }

    start();
    initialized = true;
}

void Lv2Server::process() {
}

void Lv2Server::thread_func() {
    int msdelay = 1000;
    while (!exit_thread) {
        if (!initialized) {
            continue;
        }

        lock();

        bool use_solo = false;
        for (int i = 0; i < instances.size(); i++) {
            if (instances[i]->solo == true) {
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

void Lv2Server::set_instance_count(int p_count) {
    ERR_FAIL_COND(p_count < 1);
    ERR_FAIL_INDEX(p_count, 256);

    edited = true;

    int cb = instances.size();

    if (p_count < instances.size()) {
        for (int i = p_count; i < instances.size(); i++) {
            instance_map.erase(instances[i]->instance_name);
            memdelete(instances[i]);
        }
    }

    instances.resize(p_count);

    for (int i = cb; i < instances.size(); i++) {
        String attempt = "New Instance";
        int attempts = 1;
        while (true) {
            bool name_free = true;
            for (int j = 0; j < i; j++) {
                if (instances[j]->instance_name == attempt) {
                    name_free = false;
                    break;
                }
            }

            if (!name_free) {
                attempts++;
                attempt = "New Instance " + itos(attempts);
            } else {
                break;
            }
        }

        if (i == 0) {
            attempt = "Main";
        }

        instances.write[i] = memnew(Lv2Instance);
        instances[i]->instance_name = attempt;
        instances[i]->solo = false;
        instances[i]->mute = false;
        instances[i]->bypass = false;
        instances[i]->volume_db = 0;
        instances[i]->uri = "";

        instance_map[attempt] = instances[i];

        if (!instances[i]->is_connected("lv2_ready", Callable(this, "on_ready"))) {
            instances[i]->connect("lv2_ready", Callable(this, "on_ready"), CONNECT_DEFERRED);
        }
    }

    emit_signal("layout_changed");
}

int Lv2Server::get_instance_count() const {
    if (!initialized) {
        return 0;
    }
    return instances.size();
}

void Lv2Server::remove_instance(int p_index) {
    ERR_FAIL_INDEX(p_index, instances.size());
    ERR_FAIL_COND(p_index == 0);

    edited = true;

    instances[p_index]->stop();
    instance_map.erase(instances[p_index]->instance_name);
    memdelete(instances[p_index]);
    instances.remove_at(p_index);

    emit_signal("layout_changed");
}

void Lv2Server::add_instance(int p_at_pos) {
    edited = true;

    if (p_at_pos >= instances.size()) {
        p_at_pos = -1;
    } else if (p_at_pos == 0) {
        if (instances.size() > 1) {
            p_at_pos = 1;
        } else {
            p_at_pos = -1;
        }
    }

    String attempt = "New Instance";
    int attempts = 1;
    while (true) {
        bool name_free = true;
        for (int j = 0; j < instances.size(); j++) {
            if (instances[j]->instance_name == attempt) {
                name_free = false;
                break;
            }
        }

        if (!name_free) {
            attempts++;
            attempt = "New Instance " + itos(attempts);
        } else {
            break;
        }
    }

    Lv2Instance *instance = memnew(Lv2Instance);
    instance->instance_name = attempt;
    instance->solo = false;
    instance->mute = false;
    instance->bypass = false;
    instance->volume_db = 0;
    instance->uri = "";

    if (!instance->is_connected("lv2_ready", Callable(this, "on_ready"))) {
        instance->connect("lv2_ready", Callable(this, "on_ready"), CONNECT_DEFERRED);
    }

    instance_map[attempt] = instance;

    if (p_at_pos == -1) {
        instances.push_back(instance);
    } else {
        instances.insert(p_at_pos, instance);
    }

    emit_signal("layout_changed");
}

void Lv2Server::move_instance(int p_index, int p_to_pos) {
    ERR_FAIL_COND(p_index < 1 || p_index >= instances.size());
    ERR_FAIL_COND(p_to_pos != -1 && (p_to_pos < 1 || p_to_pos > instances.size()));

    edited = true;

    if (p_index == p_to_pos) {
        return;
    }

    Lv2Instance *instance = instances[p_index];
    instances.remove_at(p_index);

    if (p_to_pos == -1) {
        instances.push_back(instance);
    } else if (p_to_pos < p_index) {
        instances.insert(p_to_pos, instance);
    } else {
        instances.insert(p_to_pos - 1, instance);
    }

    emit_signal("layout_changed");
}

void Lv2Server::set_instance_name(int p_index, const String &p_name) {
    ERR_FAIL_INDEX(p_index, instances.size());
    if (p_index == 0 && p_name != "Main") {
        return; // lv2 0 is always main
    }

    edited = true;

    if (instances[p_index]->instance_name == p_name) {
        return;
    }

    String attempt = p_name;
    int attempts = 1;

    while (true) {
        bool name_free = true;
        for (int i = 0; i < instances.size(); i++) {
            if (instances[i]->instance_name == attempt) {
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
    instance_map.erase(instances[p_index]->instance_name);
    instances[p_index]->instance_name = attempt;
    instance_map[attempt] = instances[p_index];

    emit_signal("layout_changed");
}

String Lv2Server::get_instance_name(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), String());
    return instances[p_index]->instance_name;
}

int Lv2Server::get_instance_index(const StringName &p_index) const {
    for (int i = 0; i < instances.size(); ++i) {
        if (instances[i]->instance_name == p_index) {
            return i;
        }
    }
    return -1;
}

String Lv2Server::get_name_options() const {
    String options;
    for (int i = 0; i < get_instance_count(); i++) {
        if (i > 0) {
            options += ",";
        }
        String name = get_instance_name(i);
        options += name;
    }
    return options;
}

int Lv2Server::get_channel_count(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), 0);
    return instances[p_index]->get_output_channel_count();
}

void Lv2Server::set_volume_db(int p_index, float p_volume_db) {
    ERR_FAIL_INDEX(p_index, instances.size());

    edited = true;

    instances[p_index]->volume_db = p_volume_db;
}

float Lv2Server::get_volume_db(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), 0);
    return instances[p_index]->volume_db;
}

void Lv2Server::set_uri(int p_index, String p_uri) {
    ERR_FAIL_INDEX(p_index, instances.size());

    edited = true;

    instances[p_index]->set_uri(p_uri);
}

String Lv2Server::get_uri(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), "");
    return instances[p_index]->uri;
}

void Lv2Server::set_solo(int p_index, bool p_enable) {
    ERR_FAIL_INDEX(p_index, instances.size());

    edited = true;

    instances[p_index]->solo = p_enable;
}

bool Lv2Server::is_solo(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), false);

    return instances[p_index]->solo;
}

void Lv2Server::set_mute(int p_index, bool p_enable) {
    ERR_FAIL_INDEX(p_index, instances.size());

    edited = true;

    instances[p_index]->mute = p_enable;
}

bool Lv2Server::is_mute(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), false);

    return instances[p_index]->mute;
}

void Lv2Server::set_bypass(int p_index, bool p_enable) {
    ERR_FAIL_INDEX(p_index, instances.size());

    edited = true;

    instances[p_index]->bypass = p_enable;
}

bool Lv2Server::is_bypassing(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), false);

    return instances[p_index]->bypass;
}

float Lv2Server::get_channel_peak_volume_db(int p_index, int p_channel) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), 0);
    ERR_FAIL_INDEX_V(p_channel, instances[p_index]->output_channels.size(), 0);

    return instances[p_index]->output_channels[p_channel].peak_volume;
}

bool Lv2Server::is_channel_active(int p_index, int p_channel) const {
    ERR_FAIL_INDEX_V(p_index, instances.size(), false);
    if (p_channel >= instances[p_index]->output_channels.size()) {
        return false;
    }

    ERR_FAIL_INDEX_V(p_channel, instances[p_index]->output_channels.size(), false);

    return instances[p_index]->output_channels[p_channel].active;
}

bool Lv2Server::load_default_layout() {
    if (layout_loaded) {
        return true;
    }

    String layout_path = ProjectSettings::get_singleton()->get_setting("audio/lv2-host/default_lv2_layout");

    if (layout_path.is_empty() || layout_path.get_file() == "<null>") {
        layout_path = "res://default_lv2_layout.tres";
    }

    if (ResourceLoader::get_singleton()->exists(layout_path)) {
        Ref<Lv2Layout> default_layout = ResourceLoader::get_singleton()->load(layout_path);
        if (default_layout.is_valid()) {
            set_layout(default_layout);
            emit_signal("layout_changed");
            return true;
        }
    }

    return false;
}

void Lv2Server::set_layout(const Ref<Lv2Layout> &p_layout) {
    ERR_FAIL_COND(p_layout.is_null() || p_layout->instances.size() == 0);

    int prev_size = instances.size();
    for (int i = prev_size; i < instances.size(); i++) {
        instances[i]->stop();
        memdelete(instances[i]);
    }
    instances.resize(p_layout->instances.size());
    instance_map.clear();
    for (int i = 0; i < p_layout->instances.size(); i++) {
        Lv2Instance *instance;
        if (i >= prev_size) {
            instance = memnew(Lv2Instance);
        } else {
            instance = instances[i];
            instance->reset();
        }

        if (i == 0) {
            instance->instance_name = "Main";
        } else {
            instance->instance_name = p_layout->instances[i].name;
        }

        instance->solo = p_layout->instances[i].solo;
        instance->mute = p_layout->instances[i].mute;
        instance->bypass = p_layout->instances[i].bypass;
        instance->volume_db = p_layout->instances[i].volume_db;
        instance->uri = p_layout->instances[i].uri;
        instance_map[instance->instance_name] = instance;
        instances.write[i] = instance;

        instance->call_deferred("initialize");
        if (!instance->is_connected("lv2_ready", Callable(this, "on_ready"))) {
            instance->connect("lv2_ready", Callable(this, "on_ready"), CONNECT_DEFERRED);
        }
    }
    edited = false;
    layout_loaded = true;
}

void Lv2Server::on_ready(String instance_name) {
    emit_signal("lv2_ready", instance_name);
}

Ref<Lv2Layout> Lv2Server::generate_layout() const {
    Ref<Lv2Layout> state;
    state.instantiate();

    state->instances.resize(instances.size());

    for (int i = 0; i < instances.size(); i++) {
        state->instances.write[i].name = instances[i]->instance_name;
        state->instances.write[i].mute = instances[i]->mute;
        state->instances.write[i].solo = instances[i]->solo;
        state->instances.write[i].bypass = instances[i]->bypass;
        state->instances.write[i].volume_db = instances[i]->volume_db;
        state->instances.write[i].uri = instances[i]->uri;
    }

    return state;
}

Error Lv2Server::start() {
    thread_exited = false;
    thread.instantiate();
    mutex.instantiate();
    thread->start(callable_mp(this, &Lv2Server::thread_func), Thread::PRIORITY_NORMAL);
    return OK;
}

void Lv2Server::lock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->lock();
}

void Lv2Server::unlock() {
    if (thread.is_null() || mutex.is_null()) {
        return;
    }
    mutex->unlock();
}

void Lv2Server::finish() {
    exit_thread = true;
    if (thread.is_valid() && thread->is_started()) {
        thread->wait_to_finish();
    }
}

TypedArray<String> Lv2Server::get_plugins() {
    TypedArray<String> result;
    std::vector<LilvPluginInfo> plugins_info = lv2_host->get_plugins_info(false);

    for (int i = 0; i < plugins_info.size(); i++) {
        result.push_back(plugins_info[i].uri.c_str());
    }

    return result;
}

String Lv2Server::get_plugin_name(String p_uri) {
    // TODO: should this be cached?
    std::vector<LilvPluginInfo> plugins_info = lv2_host->get_plugins_info(true);

    for (int i = 0; i < plugins_info.size(); i++) {
        if (p_uri == plugins_info[i].uri.c_str()) {
            return plugins_info[i].name.c_str();
        }
    }

    return "";
}

Lv2Instance *Lv2Server::get_instance(const String &p_name) {
    if (instance_map.has(p_name)) {
        return instance_map.get(p_name);
    }

    return NULL;
}

Lv2Instance *Lv2Server::get_instance_by_index(int p_index) {
    return instances.get(p_index);
}

Lv2Instance *Lv2Server::get_instance_(const Variant &p_variant) {
    if (p_variant.get_type() == Variant::STRING) {
        String str = p_variant;
        return instance_map.get(str);
    }

    if (p_variant.get_type() == Variant::INT) {
        int index = p_variant.operator int();
        return instances.get(index);
    }

    return NULL;
}

String Lv2Server::get_version() {
    return GODOT_LV2_HOST_VERSION;
}

String Lv2Server::get_build() {
    return GODOT_LV2_HOST_BUILD;
}

void Lv2Server::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &Lv2Server::initialize);

    ClassDB::bind_method(D_METHOD("get_version"), &Lv2Server::get_version);
    ClassDB::bind_method(D_METHOD("get_build"), &Lv2Server::get_build);

    ClassDB::bind_method(D_METHOD("set_edited", "edited"), &Lv2Server::set_edited);
    ClassDB::bind_method(D_METHOD("get_edited"), &Lv2Server::get_edited);

    ClassDB::bind_method(D_METHOD("set_instance_count", "amount"), &Lv2Server::set_instance_count);
    ClassDB::bind_method(D_METHOD("get_instance_count"), &Lv2Server::get_instance_count);

    ClassDB::bind_method(D_METHOD("remove_instance", "index"), &Lv2Server::remove_instance);
    ClassDB::bind_method(D_METHOD("add_instance", "at_position"), &Lv2Server::add_instance, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("move_instance", "index", "to_index"), &Lv2Server::move_instance);

    ClassDB::bind_method(D_METHOD("set_instance_name", "index", "name"), &Lv2Server::set_instance_name);
    ClassDB::bind_method(D_METHOD("get_instance_name", "index"), &Lv2Server::get_instance_name);
    ClassDB::bind_method(D_METHOD("get_instance_index", "name"), &Lv2Server::get_instance_index);

    ClassDB::bind_method(D_METHOD("get_name_options"), &Lv2Server::get_name_options);

    ClassDB::bind_method(D_METHOD("get_channel_count", "index"), &Lv2Server::get_channel_count);

    ClassDB::bind_method(D_METHOD("set_volume_db", "index", "volume_db"), &Lv2Server::set_volume_db);
    ClassDB::bind_method(D_METHOD("get_volume_db", "index"), &Lv2Server::get_volume_db);

    ClassDB::bind_method(D_METHOD("set_uri", "index", "uri"), &Lv2Server::set_uri);
    ClassDB::bind_method(D_METHOD("get_uri", "index"), &Lv2Server::get_uri);

    ClassDB::bind_method(D_METHOD("set_solo", "index", "enable"), &Lv2Server::set_solo);
    ClassDB::bind_method(D_METHOD("is_solo", "index"), &Lv2Server::is_solo);

    ClassDB::bind_method(D_METHOD("set_mute", "index", "enable"), &Lv2Server::set_mute);
    ClassDB::bind_method(D_METHOD("is_mute", "index"), &Lv2Server::is_mute);

    ClassDB::bind_method(D_METHOD("set_bypass", "index", "enable"), &Lv2Server::set_bypass);
    ClassDB::bind_method(D_METHOD("is_bypassing", "index"), &Lv2Server::is_bypassing);

    ClassDB::bind_method(D_METHOD("get_channel_peak_volume_db", "index", "channel"),
                         &Lv2Server::get_channel_peak_volume_db);

    ClassDB::bind_method(D_METHOD("is_channel_active", "index", "channel"), &Lv2Server::is_channel_active);

    ClassDB::bind_method(D_METHOD("lock"), &Lv2Server::lock);
    ClassDB::bind_method(D_METHOD("unlock"), &Lv2Server::unlock);

    ClassDB::bind_method(D_METHOD("set_layout", "layout"), &Lv2Server::set_layout);
    ClassDB::bind_method(D_METHOD("generate_layout"), &Lv2Server::generate_layout);

    ClassDB::bind_method(D_METHOD("get_instance", "name"), &Lv2Server::get_instance);

    ClassDB::bind_method(D_METHOD("on_ready", "name"), &Lv2Server::on_ready);

    ClassDB::bind_method(D_METHOD("get_plugins"), &Lv2Server::get_plugins);

    ClassDB::bind_method(D_METHOD("get_plugin_name", "uri"), &Lv2Server::get_plugin_name);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "instance_count"), "set_instance_count", "get_instance_count");

    ADD_SIGNAL(MethodInfo("layout_changed"));
    ADD_SIGNAL(MethodInfo("lv2_ready", PropertyInfo(Variant::STRING, "name")));
}

} // namespace godot
