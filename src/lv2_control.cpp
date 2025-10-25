#include "lv2_control.h"

using namespace godot;

Lv2Control::Lv2Control() {
}

Lv2Control::~Lv2Control() {
}

void Lv2Control::set_index(int p_index) {
    index = p_index;
}

int Lv2Control::get_index() {
    return index;
}

void Lv2Control::set_symbol(String p_symbol) {
    symbol = p_symbol;
}

String Lv2Control::get_symbol() {
    return symbol;
}

void Lv2Control::set_name(String p_name) {
    name = p_name;
}

String Lv2Control::get_name() {
    return name;
}

void Lv2Control::set_unit(String p_unit) {
    unit = p_unit;
}

String Lv2Control::get_unit() {
    return unit;
}

void Lv2Control::set_default(int p_default) {
    def = p_default;
}

int Lv2Control::get_default() {
    return def;
}

void Lv2Control::set_min(int p_min) {
    min = p_min;
}

int Lv2Control::get_min() {
    return min;
}

void Lv2Control::set_max(int p_max) {
    max = p_max;
}

int Lv2Control::get_max() {
    return max;
}

void Lv2Control::set_logarithmic(bool p_logarithmic) {
    logarithmic = p_logarithmic;
}

bool Lv2Control::get_logarithmic() {
    return logarithmic;
}

void Lv2Control::set_integer(bool p_integer) {
    integer = p_integer;
}

bool Lv2Control::get_integer() {
    return integer;
}

void Lv2Control::set_enumeration(bool p_enumeration) {
    enumeration = p_enumeration;
}

bool Lv2Control::get_enumeration() {
    return enumeration;
}

void Lv2Control::set_toggle(bool p_toggle) {
    toggle = p_toggle;
}

bool Lv2Control::get_toggle() {
    return toggle;
}

int Lv2Control::get_choice_count() {
    return choices.size();
}

void Lv2Control::set_choice(String p_label, float p_value) {
    choices[p_label] = p_value;
}

float Lv2Control::get_choice(String p_label) {
    if (choices.has(p_label)) {
        return choices[p_label];
    } else {
        return 0;
    }
}

Dictionary Lv2Control::get_choices() {
    return choices;
}

void Lv2Control::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_index", "index"), &Lv2Control::set_index);
    ClassDB::bind_method(D_METHOD("get_index"), &Lv2Control::get_index);

    ClassDB::bind_method(D_METHOD("set_symbol", "symbol"), &Lv2Control::set_symbol);
    ClassDB::bind_method(D_METHOD("get_symbol"), &Lv2Control::get_symbol);

    ClassDB::bind_method(D_METHOD("set_name", "name"), &Lv2Control::set_name);
    ClassDB::bind_method(D_METHOD("get_name"), &Lv2Control::get_name);

    ClassDB::bind_method(D_METHOD("set_unit", "unit"), &Lv2Control::set_unit);
    ClassDB::bind_method(D_METHOD("get_unit"), &Lv2Control::get_unit);

    ClassDB::bind_method(D_METHOD("set_default", "default"), &Lv2Control::set_default);
    ClassDB::bind_method(D_METHOD("get_default"), &Lv2Control::get_default);

    ClassDB::bind_method(D_METHOD("set_min", "min"), &Lv2Control::set_min);
    ClassDB::bind_method(D_METHOD("get_min"), &Lv2Control::get_min);

    ClassDB::bind_method(D_METHOD("set_max", "max"), &Lv2Control::set_max);
    ClassDB::bind_method(D_METHOD("get_max"), &Lv2Control::get_max);

    ClassDB::bind_method(D_METHOD("set_logarithmic", "logarithmic"), &Lv2Control::set_logarithmic);
    ClassDB::bind_method(D_METHOD("get_logarithmic"), &Lv2Control::get_logarithmic);

    ClassDB::bind_method(D_METHOD("set_integer", "integer"), &Lv2Control::set_integer);
    ClassDB::bind_method(D_METHOD("get_integer"), &Lv2Control::get_integer);

    ClassDB::bind_method(D_METHOD("set_enumeration", "enumeration"), &Lv2Control::set_enumeration);
    ClassDB::bind_method(D_METHOD("get_enumeration"), &Lv2Control::get_enumeration);

    ClassDB::bind_method(D_METHOD("set_toggle", "toggle"), &Lv2Control::set_toggle);
    ClassDB::bind_method(D_METHOD("get_toggle"), &Lv2Control::get_toggle);

    ClassDB::bind_method(D_METHOD("get_choices"), &Lv2Control::get_choices);

    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "index"), "set_index", "get_index");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "symbol"), "set_symbol", "get_symbol");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "name"), "set_name", "get_name");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "unit"), "set_unit", "get_unit");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "default"), "set_default", "get_default");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "min"), "set_min", "get_min");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "max"), "set_max", "get_max");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "logarithmic"), "set_logarithmic", "get_logarithmic");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "integer"), "set_integer", "get_integer");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "enumeration"), "set_enumeration", "get_enumeration");
    ClassDB::add_property("Lv2Control", PropertyInfo(Variant::STRING, "toggle"), "set_toggle", "get_toggle");
}
