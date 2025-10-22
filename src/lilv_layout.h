#ifndef LILV_LAYOUT_H
#define LILV_LAYOUT_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

class LilvLayout : public Resource {
    GDCLASS(LilvLayout, Resource);
    friend class LilvServer;

    struct Lilv {
        String name;
        bool solo = false;
        bool mute = false;
        bool bypass = false;
        float volume_db = 0.0f;

        Lilv() {
        }
    };

    Vector<Lilv> lilvs;

protected:
    static void _bind_methods();

    bool _set(const StringName &p_name, const Variant &p_value);
    bool _get(const StringName &p_name, Variant &r_ret) const;
    void _get_property_list(List<PropertyInfo> *p_list) const;

public:
    LilvLayout();
};

} // namespace godot

#endif
