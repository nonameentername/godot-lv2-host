#ifndef LILV_SERVER_H
#define LILV_SERVER_H

#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "lilv_instance.h"
#include "lilv_layout.h"
#include "godot_cpp/classes/mutex.hpp"
#include "godot_cpp/classes/thread.hpp"

namespace godot {

class LilvServer : public Object {
    GDCLASS(LilvServer, Object);

private:
    bool initialized;
    bool layout_loaded;
    bool edited;
    int sfont_id;

    LilvWorld *world;
    HashMap<String, LilvInstance *> lilv_map;

    bool thread_exited;
    mutable bool exit_thread;
    Ref<Thread> thread;
    Ref<Mutex> mutex;

    void on_lilv_ready(String lilv_name);
    void add_property(String name, String default_value, GDExtensionVariantType extension_type, PropertyHint hint);

protected:
    bool solo_mode;
    Vector<LilvInstance *> lilv_instances;
    static void _bind_methods();
    static LilvServer *singleton;

public:
    LilvServer();
    ~LilvServer();

    LilvWorld *get_lilv_world();

    bool get_solo_mode();

    void set_edited(bool p_edited);
    bool get_edited();

    static LilvServer *get_singleton();
    void initialize();
    void process();
    void thread_func();

    void set_lilv_count(int p_count);
    int get_lilv_count() const;

    void remove_lilv(int p_index);
    void add_lilv(int p_at_pos = -1);

    void move_lilv(int p_lilv, int p_to_pos);

    void set_lilv_name(int p_lilv, const String &p_name);
    String get_lilv_name(int p_lilv) const;
    int get_lilv_index(const StringName &p_lilv_name) const;

    String get_lilv_name_options() const;

    int get_lilv_channel_count(int p_lilv) const;

    void set_lilv_volume_db(int p_lilv, float p_volume_db);
    float get_lilv_volume_db(int p_lilv) const;

    void set_lilv_tab(int p_lilv, float p_tab);
    int get_lilv_tab(int p_lilv) const;

    void set_lilv_solo(int p_lilv, bool p_enable);
    bool is_lilv_solo(int p_lilv) const;

    void set_lilv_mute(int p_lilv, bool p_enable);
    bool is_lilv_mute(int p_lilv) const;

    void set_lilv_bypass(int p_lilv, bool p_enable);
    bool is_lilv_bypassing(int p_lilv) const;

    float get_lilv_channel_peak_volume_db(int p_lilv, int p_channel) const;

    bool is_lilv_channel_active(int p_lilv, int p_channel) const;

    bool load_default_lilv_layout();
    void set_lilv_layout(const Ref<LilvLayout> &p_lilv_layout);
    Ref<LilvLayout> generate_lilv_layout() const;

    Error start();
    void lock();
    void unlock();
    void finish();

    LilvInstance *get_lilv(const String &p_name);
    LilvInstance *get_lilv_by_index(int p_index);
    LilvInstance *get_lilv_(const Variant &p_name);

    String get_version();
    String get_build();
};
} // namespace godot

#endif
