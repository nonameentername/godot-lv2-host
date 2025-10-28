#ifndef LV2_CONTROL_H
#define LV2_CONTROL_H

#include "godot_cpp/variant/dictionary.hpp"
#include <godot_cpp/classes/audio_frame.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

class Lv2Control : public RefCounted {
    GDCLASS(Lv2Control, RefCounted);

private:
    int index;
    String symbol;
    String name;
    String unit;
    int def;
    int min;
    int max;
    bool logarithmic;
    bool integer;
    bool enumeration;
    bool toggle;
    Dictionary choices;

protected:
    static void _bind_methods();

public:
    Lv2Control();
    ~Lv2Control();

    void set_index(int p_index);
    int get_index();

    void set_symbol(String p_symbol);
    String get_symbol();

    void set_name(String p_name);
    String get_name();

    void set_unit(String p_unit);
    String get_unit();

    void set_default(int p_default);
    int get_default();

    void set_min(int p_min);
    int get_min();

    void set_max(int p_max);
    int get_max();

    void set_logarithmic(bool p_logarithmic);
    bool get_logarithmic();

    void set_integer(bool p_integer);
    bool get_integer();

    void set_enumeration(bool p_enumeration);
    bool get_enumeration();

    void set_toggle(bool p_toggle);
    bool get_toggle();

    int get_choice_count();
    void set_choice(String p_label, float p_value);
    float get_choice(String p_label);
    Dictionary get_choices();
};

} // namespace godot
#endif
