#ifndef LV2_SERVER_H
#define LV2_SERVER_H

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

#include "lv2_instance.h"
#include "lv2_layout.h"
#include "godot_cpp/classes/mutex.hpp"
#include "godot_cpp/classes/thread.hpp"

namespace godot {

class Lv2Server : public Object {
    GDCLASS(Lv2Server, Object);

private:
    bool initialized;
    bool layout_loaded;
    bool edited;
    int sfont_id;

    LilvWorld *world;
    HashMap<String, Lv2Instance *> lv2_map;

    bool thread_exited;
    mutable bool exit_thread;
    Ref<Thread> thread;
    Ref<Mutex> mutex;

    void on_lv2_ready(String lv2_name);
    void add_property(String name, String default_value, GDExtensionVariantType extension_type, PropertyHint hint);

protected:
    bool solo_mode;
    Vector<Lv2Instance *> lv2_instances;
    static void _bind_methods();
    static Lv2Server *singleton;

public:
    Lv2Server();
    ~Lv2Server();

    LilvWorld *get_lilv_world();

    bool get_solo_mode();

    void set_edited(bool p_edited);
    bool get_edited();

    static Lv2Server *get_singleton();
    void initialize();
    void process();
    void thread_func();

    void set_lv2_count(int p_count);
    int get_lv2_count() const;

    void remove_lv2(int p_index);
    void add_lv2(int p_at_pos = -1);

    void move_lv2(int p_lv2, int p_to_pos);

    void set_lv2_name(int p_lv2, const String &p_name);
    String get_lv2_name(int p_lv2) const;
    int get_lv2_index(const StringName &p_lv2_name) const;

    String get_lv2_name_options() const;

    int get_lv2_channel_count(int p_lv2) const;

    void set_lv2_volume_db(int p_lv2, float p_volume_db);
    float get_lv2_volume_db(int p_lv2) const;

    void set_lv2_tab(int p_lv2, float p_tab);
    int get_lv2_tab(int p_lv2) const;

    void set_lv2_solo(int p_lv2, bool p_enable);
    bool is_lv2_solo(int p_lv2) const;

    void set_lv2_mute(int p_lv2, bool p_enable);
    bool is_lv2_mute(int p_lv2) const;

    void set_lv2_bypass(int p_lv2, bool p_enable);
    bool is_lv2_bypassing(int p_lv2) const;

    float get_lv2_channel_peak_volume_db(int p_lv2, int p_channel) const;

    bool is_lv2_channel_active(int p_lv2, int p_channel) const;

    bool load_default_lv2_layout();
    void set_lv2_layout(const Ref<Lv2Layout> &p_lv2_layout);
    Ref<Lv2Layout> generate_lv2_layout() const;

    Error start();
    void lock();
    void unlock();
    void finish();

    Lv2Instance *get_lv2(const String &p_name);
    Lv2Instance *get_lv2_by_index(int p_index);
    Lv2Instance *get_lv2_(const Variant &p_name);

    String get_version();
    String get_build();
};
} // namespace godot

#endif
