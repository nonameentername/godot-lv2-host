#include "lv2_layout.h"

using namespace godot;

bool Lv2Layout::_set(const StringName &p_name, const Variant &p_value) {
    String s = p_name;
    if (s.begins_with("lv2/")) {
        int index = s.get_slice("/", 1).to_int();
        if (instances.size() <= index) {
            instances.resize(index + 1);
        }

        Lv2 &lv2 = instances.write[index];

        String what = s.get_slice("/", 2);

        if (what == "name") {
            lv2.name = p_value;
        } else if (what == "solo") {
            lv2.solo = p_value;
        } else if (what == "mute") {
            lv2.mute = p_value;
        } else if (what == "bypass") {
            lv2.bypass = p_value;
        } else if (what == "volume_db") {
            lv2.volume_db = p_value;
        } else if (what == "uri") {
            lv2.uri = p_value;
        } else {
            return false;
        }

        return true;
    }

    return false;
}

bool Lv2Layout::_get(const StringName &p_name, Variant &r_ret) const {
    String s = p_name;
    if (s.begins_with("lv2/")) {
        int index = s.get_slice("/", 1).to_int();
        if (index < 0 || index >= instances.size()) {
            return false;
        }

        const Lv2 &lv2 = instances[index];

        String what = s.get_slice("/", 2);

        if (what == "name") {
            r_ret = lv2.name;
        } else if (what == "solo") {
            r_ret = lv2.solo;
        } else if (what == "mute") {
            r_ret = lv2.mute;
        } else if (what == "bypass") {
            r_ret = lv2.bypass;
        } else if (what == "volume_db") {
            r_ret = lv2.volume_db;
        } else if (what == "uri") {
            r_ret = lv2.uri;
        } else {
            return false;
        }

        return true;
    }

    return false;
}

void Lv2Layout::_get_property_list(List<PropertyInfo> *p_list) const {
    for (int i = 0; i < instances.size(); i++) {
        p_list->push_back(PropertyInfo(Variant::STRING, "lv2/" + itos(i) + "/name", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::BOOL, "lv2/" + itos(i) + "/solo", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::BOOL, "lv2/" + itos(i) + "/mute", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::BOOL, "lv2/" + itos(i) + "/bypass", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::FLOAT, "lv2/" + itos(i) + "/volume_db", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::STRING, "lv2/" + itos(i) + "/uri", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
    }
}

Lv2Layout::Lv2Layout() {
    instances.resize(1);
    instances.write[0].name = "Main";
}

void Lv2Layout::_bind_methods() {
}
