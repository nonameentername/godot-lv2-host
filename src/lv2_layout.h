#ifndef LV2_LAYOUT_H
#define LV2_LAYOUT_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

class Lv2Layout : public Resource {
    GDCLASS(Lv2Layout, Resource);
    friend class Lv2Server;

    struct Lv2 {
        String name;
        bool solo = false;
        bool mute = false;
        bool bypass = false;
        float volume_db = 0.0f;
        String uri;

        Lv2() {
        }
    };

    Vector<Lv2> lv2s;

protected:
    static void _bind_methods();

    bool _set(const StringName &p_name, const Variant &p_value);
    bool _get(const StringName &p_name, Variant &r_ret) const;
    void _get_property_list(List<PropertyInfo> *p_list) const;

public:
    Lv2Layout();
};

} // namespace godot

#endif
