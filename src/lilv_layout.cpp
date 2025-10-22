#include "lilv_layout.h"

using namespace godot;

bool LilvLayout::_set(const StringName &p_name, const Variant &p_value) {
    String s = p_name;
    if (s.begins_with("lilv/")) {
        int index = s.get_slice("/", 1).to_int();
        if (lilvs.size() <= index) {
            lilvs.resize(index + 1);
        }

        Lilv &lilv = lilvs.write[index];

        String what = s.get_slice("/", 2);

        if (what == "name") {
            lilv.name = p_value;
        } else if (what == "solo") {
            lilv.solo = p_value;
        } else if (what == "mute") {
            lilv.mute = p_value;
        } else if (what == "bypass") {
            lilv.bypass = p_value;
        } else if (what == "volume_db") {
            lilv.volume_db = p_value;
        } else {
            return false;
        }

        return true;
    }

    return false;
}

bool LilvLayout::_get(const StringName &p_name, Variant &r_ret) const {
    String s = p_name;
    if (s.begins_with("lilv/")) {
        int index = s.get_slice("/", 1).to_int();
        if (index < 0 || index >= lilvs.size()) {
            return false;
        }

        const Lilv &lilv = lilvs[index];

        String what = s.get_slice("/", 2);

        if (what == "name") {
            r_ret = lilv.name;
        } else if (what == "solo") {
            r_ret = lilv.solo;
        } else if (what == "mute") {
            r_ret = lilv.mute;
        } else if (what == "bypass") {
            r_ret = lilv.bypass;
        } else if (what == "volume_db") {
            r_ret = lilv.volume_db;
        } else {
            return false;
        }

        return true;
    }

    return false;
}

void LilvLayout::_get_property_list(List<PropertyInfo> *p_list) const {
    for (int i = 0; i < lilvs.size(); i++) {
        p_list->push_back(PropertyInfo(Variant::STRING, "lilv/" + itos(i) + "/name", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::BOOL, "lilv/" + itos(i) + "/solo", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::BOOL, "lilv/" + itos(i) + "/mute", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::BOOL, "lilv/" + itos(i) + "/bypass", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::FLOAT, "lilv/" + itos(i) + "/volume_db", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::INT, "lilv/" + itos(i) + "/tab", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
        p_list->push_back(PropertyInfo(Variant::OBJECT, "lilv/" + itos(i) + "/script", PROPERTY_HINT_NONE, "",
                                       PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL));
    }
}

LilvLayout::LilvLayout() {
    lilvs.resize(1);
    lilvs.write[0].name = "Main";
}

void LilvLayout::_bind_methods() {
}
