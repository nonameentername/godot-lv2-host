#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "lv2_layout.h"
#include "lv2_server.h"
#include "lv2_server_node.h"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/os.hpp"
#include "godot_cpp/core/error_macros.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "version_generated.gen.h"

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
    add_property("audio/lv2/default_lv2_layout", "res://default_lv2_layout.tres",
                 GDEXTENSION_VARIANT_TYPE_STRING, PROPERTY_HINT_FILE);
    add_property("audio/lv2/use_resource_files", "true", GDEXTENSION_VARIANT_TYPE_BOOL, PROPERTY_HINT_NONE);
    add_property("audio/lv2/hide_lv2_logs", "true", GDEXTENSION_VARIANT_TYPE_BOOL, PROPERTY_HINT_NONE);

    if (!load_default_lv2_layout()) {
        set_lv2_count(1);
    }

    set_edited(false);

    if (!initialized) {
        Node *lv2_server_node = memnew(Lv2ServerNode);
        SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
        tree->get_root()->add_child(lv2_server_node);
        lv2_server_node->set_process(true);
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
        for (int i = 0; i < lv2_instances.size(); i++) {
            if (lv2_instances[i]->solo == true) {
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

void Lv2Server::set_lv2_count(int p_count) {
    ERR_FAIL_COND(p_count < 1);
    ERR_FAIL_INDEX(p_count, 256);

    edited = true;

    int cb = lv2_instances.size();

    if (p_count < lv2_instances.size()) {
        for (int i = p_count; i < lv2_instances.size(); i++) {
            lv2_map.erase(lv2_instances[i]->lv2_name);
            memdelete(lv2_instances[i]);
        }
    }

    lv2_instances.resize(p_count);

    for (int i = cb; i < lv2_instances.size(); i++) {
        String attempt = "New Lv2";
        int attempts = 1;
        while (true) {
            bool name_free = true;
            for (int j = 0; j < i; j++) {
                if (lv2_instances[j]->lv2_name == attempt) {
                    name_free = false;
                    break;
                }
            }

            if (!name_free) {
                attempts++;
                attempt = "New Lv2 " + itos(attempts);
            } else {
                break;
            }
        }

        if (i == 0) {
            attempt = "Main";
        }

        lv2_instances.write[i] = memnew(Lv2Instance);
        lv2_instances[i]->lv2_name = attempt;
        lv2_instances[i]->solo = false;
        lv2_instances[i]->mute = false;
        lv2_instances[i]->bypass = false;
        lv2_instances[i]->volume_db = 0;
        lv2_instances[i]->tab = 0;

        lv2_map[attempt] = lv2_instances[i];

        if (!lv2_instances[i]->is_connected("lv2_ready", Callable(this, "on_lv2_ready"))) {
            lv2_instances[i]->connect("lv2_ready", Callable(this, "on_lv2_ready"), CONNECT_DEFERRED);
        }

    }

    emit_signal("lv2_layout_changed");
}

int Lv2Server::get_lv2_count() const {
    if (!initialized) {
        return 0;
    }
    return lv2_instances.size();
}

void Lv2Server::remove_lv2(int p_index) {
    ERR_FAIL_INDEX(p_index, lv2_instances.size());
    ERR_FAIL_COND(p_index == 0);

    edited = true;

    lv2_instances[p_index]->stop();
    lv2_map.erase(lv2_instances[p_index]->lv2_name);
    memdelete(lv2_instances[p_index]);
    lv2_instances.remove_at(p_index);

    emit_signal("lv2_layout_changed");
}

void Lv2Server::add_lv2(int p_at_pos) {
    edited = true;

    if (p_at_pos >= lv2_instances.size()) {
        p_at_pos = -1;
    } else if (p_at_pos == 0) {
        if (lv2_instances.size() > 1) {
            p_at_pos = 1;
        } else {
            p_at_pos = -1;
        }
    }

    String attempt = "New Lv2";
    int attempts = 1;
    while (true) {
        bool name_free = true;
        for (int j = 0; j < lv2_instances.size(); j++) {
            if (lv2_instances[j]->lv2_name == attempt) {
                name_free = false;
                break;
            }
        }

        if (!name_free) {
            attempts++;
            attempt = "New Lv2 " + itos(attempts);
        } else {
            break;
        }
    }

    Lv2Instance *lv2_instance = memnew(Lv2Instance);
    lv2_instance->lv2_name = attempt;
    lv2_instance->solo = false;
    lv2_instance->mute = false;
    lv2_instance->bypass = false;
    lv2_instance->volume_db = 0;
    lv2_instance->tab = 0;

    if (!lv2_instance->is_connected("lv2_ready", Callable(this, "on_lv2_ready"))) {
        lv2_instance->connect("lv2_ready", Callable(this, "on_lv2_ready"), CONNECT_DEFERRED);
    }

    lv2_map[attempt] = lv2_instance;

    if (p_at_pos == -1) {
        lv2_instances.push_back(lv2_instance);
    } else {
        lv2_instances.insert(p_at_pos, lv2_instance);
    }

    emit_signal("lv2_layout_changed");
}

void Lv2Server::move_lv2(int p_lv2, int p_to_pos) {
    ERR_FAIL_COND(p_lv2 < 1 || p_lv2 >= lv2_instances.size());
    ERR_FAIL_COND(p_to_pos != -1 && (p_to_pos < 1 || p_to_pos > lv2_instances.size()));

    edited = true;

    if (p_lv2 == p_to_pos) {
        return;
    }

    Lv2Instance *lv2_instance = lv2_instances[p_lv2];
    lv2_instances.remove_at(p_lv2);

    if (p_to_pos == -1) {
        lv2_instances.push_back(lv2_instance);
    } else if (p_to_pos < p_lv2) {
        lv2_instances.insert(p_to_pos, lv2_instance);
    } else {
        lv2_instances.insert(p_to_pos - 1, lv2_instance);
    }

    emit_signal("lv2_layout_changed");
}

void Lv2Server::set_lv2_name(int p_lv2, const String &p_name) {
    ERR_FAIL_INDEX(p_lv2, lv2_instances.size());
    if (p_lv2 == 0 && p_name != "Main") {
        return; // lv2 0 is always main
    }

    edited = true;

    if (lv2_instances[p_lv2]->lv2_name == p_name) {
        return;
    }

    String attempt = p_name;
    int attempts = 1;

    while (true) {
        bool name_free = true;
        for (int i = 0; i < lv2_instances.size(); i++) {
            if (lv2_instances[i]->lv2_name == attempt) {
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
    lv2_map.erase(lv2_instances[p_lv2]->lv2_name);
    lv2_instances[p_lv2]->lv2_name = attempt;
    lv2_map[attempt] = lv2_instances[p_lv2];

    emit_signal("lv2_layout_changed");
}

String Lv2Server::get_lv2_name(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), String());
    return lv2_instances[p_lv2]->lv2_name;
}

int Lv2Server::get_lv2_index(const StringName &p_lv2_name) const {
    for (int i = 0; i < lv2_instances.size(); ++i) {
        if (lv2_instances[i]->lv2_name == p_lv2_name) {
            return i;
        }
    }
    return -1;
}

String Lv2Server::get_lv2_name_options() const {
    String options;
    for (int i = 0; i < get_lv2_count(); i++) {
        if (i > 0) {
            options += ",";
        }
        String name = get_lv2_name(i);
        options += name;
    }
    return options;
}

int Lv2Server::get_lv2_channel_count(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), 0);
    return lv2_instances[p_lv2]->get_output_channel_count();
}

void Lv2Server::set_lv2_volume_db(int p_lv2, float p_volume_db) {
    ERR_FAIL_INDEX(p_lv2, lv2_instances.size());

    edited = true;

    lv2_instances[p_lv2]->volume_db = p_volume_db;
}

float Lv2Server::get_lv2_volume_db(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), 0);
    return lv2_instances[p_lv2]->volume_db;
}

void Lv2Server::set_lv2_tab(int p_lv2, float p_tab) {
    ERR_FAIL_INDEX(p_lv2, lv2_instances.size());

    edited = true;

    lv2_instances[p_lv2]->tab = p_tab;
}

int Lv2Server::get_lv2_tab(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), 0);
    return lv2_instances[p_lv2]->tab;
}

void Lv2Server::set_lv2_solo(int p_lv2, bool p_enable) {
    ERR_FAIL_INDEX(p_lv2, lv2_instances.size());

    edited = true;

    lv2_instances[p_lv2]->solo = p_enable;
}

bool Lv2Server::is_lv2_solo(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), false);

    return lv2_instances[p_lv2]->solo;
}

void Lv2Server::set_lv2_mute(int p_lv2, bool p_enable) {
    ERR_FAIL_INDEX(p_lv2, lv2_instances.size());

    edited = true;

    lv2_instances[p_lv2]->mute = p_enable;
}

bool Lv2Server::is_lv2_mute(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), false);

    return lv2_instances[p_lv2]->mute;
}

void Lv2Server::set_lv2_bypass(int p_lv2, bool p_enable) {
    ERR_FAIL_INDEX(p_lv2, lv2_instances.size());

    edited = true;

    lv2_instances[p_lv2]->bypass = p_enable;
}

bool Lv2Server::is_lv2_bypassing(int p_lv2) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), false);

    return lv2_instances[p_lv2]->bypass;
}

float Lv2Server::get_lv2_channel_peak_volume_db(int p_lv2, int p_channel) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), 0);
    ERR_FAIL_INDEX_V(p_channel, lv2_instances[p_lv2]->output_channels.size(), 0);

    return lv2_instances[p_lv2]->output_channels[p_channel].peak_volume;
}

bool Lv2Server::is_lv2_channel_active(int p_lv2, int p_channel) const {
    ERR_FAIL_INDEX_V(p_lv2, lv2_instances.size(), false);
    if (p_channel >= lv2_instances[p_lv2]->output_channels.size()) {
        return false;
    }

    ERR_FAIL_INDEX_V(p_channel, lv2_instances[p_lv2]->output_channels.size(), false);

    return lv2_instances[p_lv2]->output_channels[p_channel].active;
}

bool Lv2Server::load_default_lv2_layout() {
    if (layout_loaded) {
        return true;
    }

    String layout_path =
        ProjectSettings::get_singleton()->get_setting("audio/lv2/default_lv2_layout");

    if (layout_path.is_empty() || layout_path.get_file() == "<null>") {
        layout_path = "res://default_lv2_layout.tres";
    }

    if (ResourceLoader::get_singleton()->exists(layout_path)) {
        Ref<Lv2Layout> default_layout = ResourceLoader::get_singleton()->load(layout_path);
        if (default_layout.is_valid()) {
            set_lv2_layout(default_layout);
            emit_signal("lv2_layout_changed");
            return true;
        }
    }

    return false;
}

void Lv2Server::set_lv2_layout(const Ref<Lv2Layout> &p_lv2_layout) {
    ERR_FAIL_COND(p_lv2_layout.is_null() || p_lv2_layout->lv2s.size() == 0);

    int prev_size = lv2_instances.size();
    for (int i = prev_size; i < lv2_instances.size(); i++) {
        lv2_instances[i]->stop();
        memdelete(lv2_instances[i]);
    }
    lv2_instances.resize(p_lv2_layout->lv2s.size());
    lv2_map.clear();
    for (int i = 0; i < p_lv2_layout->lv2s.size(); i++) {
        Lv2Instance *lv2;
        if (i >= prev_size) {
            lv2 = memnew(Lv2Instance);
        } else {
            lv2 = lv2_instances[i];
            lv2->reset();
        }

        if (i == 0) {
            lv2->lv2_name = "Main";
        } else {
            lv2->lv2_name = p_lv2_layout->lv2s[i].name;
        }

        lv2->solo = p_lv2_layout->lv2s[i].solo;
        lv2->mute = p_lv2_layout->lv2s[i].mute;
        lv2->bypass = p_lv2_layout->lv2s[i].bypass;
        lv2->volume_db = p_lv2_layout->lv2s[i].volume_db;
        lv2_map[lv2->lv2_name] = lv2;
        lv2_instances.write[i] = lv2;

        lv2->call_deferred("initialize");
        if (!lv2->is_connected("lv2_ready", Callable(this, "on_lv2_ready"))) {
            lv2->connect("lv2_ready", Callable(this, "on_lv2_ready"), CONNECT_DEFERRED);
        }
    }
    edited = false;
    layout_loaded = true;
}

void Lv2Server::on_lv2_ready(String lv2_name) {
    emit_signal("lv2_ready", lv2_name);
}

Ref<Lv2Layout> Lv2Server::generate_lv2_layout() const {
    Ref<Lv2Layout> state;
    state.instantiate();

    state->lv2s.resize(lv2_instances.size());

    for (int i = 0; i < lv2_instances.size(); i++) {
        state->lv2s.write[i].name = lv2_instances[i]->lv2_name;
        state->lv2s.write[i].mute = lv2_instances[i]->mute;
        state->lv2s.write[i].solo = lv2_instances[i]->solo;
        state->lv2s.write[i].bypass = lv2_instances[i]->bypass;
        state->lv2s.write[i].volume_db = lv2_instances[i]->volume_db;
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
    thread->wait_to_finish();
}

Lv2Instance *Lv2Server::get_lv2(const String &p_name) {
    if (lv2_map.has(p_name)) {
        return lv2_map.get(p_name);
    }

    return NULL;
}

Lv2Instance *Lv2Server::get_lv2_by_index(int p_index) {
    return lv2_instances.get(p_index);
}

Lv2Instance *Lv2Server::get_lv2_(const Variant &p_variant) {
    if (p_variant.get_type() == Variant::STRING) {
        String str = p_variant;
        return lv2_map.get(str);
    }

    if (p_variant.get_type() == Variant::INT) {
        int index = p_variant.operator int();
        return lv2_instances.get(index);
    }

    return NULL;
}

String Lv2Server::get_version() {
    return GODOT_LV2_VERSION;
}

String Lv2Server::get_build() {
    return GODOT_LV2_BUILD;
}

void Lv2Server::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &Lv2Server::initialize);

    ClassDB::bind_method(D_METHOD("get_version"), &Lv2Server::get_version);
    ClassDB::bind_method(D_METHOD("get_build"), &Lv2Server::get_build);

    ClassDB::bind_method(D_METHOD("set_edited", "edited"), &Lv2Server::set_edited);
    ClassDB::bind_method(D_METHOD("get_edited"), &Lv2Server::get_edited);

    ClassDB::bind_method(D_METHOD("set_lv2_count", "amount"), &Lv2Server::set_lv2_count);
    ClassDB::bind_method(D_METHOD("get_lv2_count"), &Lv2Server::get_lv2_count);

    ClassDB::bind_method(D_METHOD("remove_lv2", "index"), &Lv2Server::remove_lv2);
    ClassDB::bind_method(D_METHOD("add_lv2", "at_position"), &Lv2Server::add_lv2, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("move_lv2", "index", "to_index"), &Lv2Server::move_lv2);

    ClassDB::bind_method(D_METHOD("set_lv2_name", "lv2_idx", "name"), &Lv2Server::set_lv2_name);
    ClassDB::bind_method(D_METHOD("get_lv2_name", "lv2_idx"), &Lv2Server::get_lv2_name);
    ClassDB::bind_method(D_METHOD("get_lv2_index", "lv2_name"), &Lv2Server::get_lv2_index);

    ClassDB::bind_method(D_METHOD("get_lv2_name_options"), &Lv2Server::get_lv2_name_options);

    ClassDB::bind_method(D_METHOD("get_lv2_channel_count", "lv2_idx"), &Lv2Server::get_lv2_channel_count);

    ClassDB::bind_method(D_METHOD("set_lv2_volume_db", "lv2_idx", "volume_db"),
                         &Lv2Server::set_lv2_volume_db);
    ClassDB::bind_method(D_METHOD("get_lv2_volume_db", "lv2_idx"), &Lv2Server::get_lv2_volume_db);

    ClassDB::bind_method(D_METHOD("set_lv2_tab", "lv2_idx", "tab"), &Lv2Server::set_lv2_tab);
    ClassDB::bind_method(D_METHOD("get_lv2_tab", "lv2_idx"), &Lv2Server::get_lv2_tab);

    ClassDB::bind_method(D_METHOD("set_lv2_solo", "lv2_idx", "enable"), &Lv2Server::set_lv2_solo);
    ClassDB::bind_method(D_METHOD("is_lv2_solo", "lv2_idx"), &Lv2Server::is_lv2_solo);

    ClassDB::bind_method(D_METHOD("set_lv2_mute", "lv2_idx", "enable"), &Lv2Server::set_lv2_mute);
    ClassDB::bind_method(D_METHOD("is_lv2_mute", "lv2_idx"), &Lv2Server::is_lv2_mute);

    ClassDB::bind_method(D_METHOD("set_lv2_bypass", "lv2_idx", "enable"), &Lv2Server::set_lv2_bypass);
    ClassDB::bind_method(D_METHOD("is_lv2_bypassing", "lv2_idx"), &Lv2Server::is_lv2_bypassing);

    ClassDB::bind_method(D_METHOD("get_lv2_channel_peak_volume_db", "lv2_idx", "channel"),
                         &Lv2Server::get_lv2_channel_peak_volume_db);

    ClassDB::bind_method(D_METHOD("is_lv2_channel_active", "lv2_idx", "channel"),
                         &Lv2Server::is_lv2_channel_active);

    ClassDB::bind_method(D_METHOD("lock"), &Lv2Server::lock);
    ClassDB::bind_method(D_METHOD("unlock"), &Lv2Server::unlock);

    ClassDB::bind_method(D_METHOD("set_lv2_layout", "lv2_layout"), &Lv2Server::set_lv2_layout);
    ClassDB::bind_method(D_METHOD("generate_lv2_layout"), &Lv2Server::generate_lv2_layout);

    ClassDB::bind_method(D_METHOD("get_lv2", "lv2_name"), &Lv2Server::get_lv2);

    ClassDB::bind_method(D_METHOD("on_lv2_ready", "lv2_name"), &Lv2Server::on_lv2_ready);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "lv2_count"), "set_lv2_count", "get_lv2_count");

    ADD_SIGNAL(MethodInfo("lv2_layout_changed"));
    ADD_SIGNAL(MethodInfo("lv2_ready", PropertyInfo(Variant::STRING, "lv2_name")));
}

}
