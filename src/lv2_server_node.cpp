#include "lv2_server_node.h"
#include "lv2_server.h"

using namespace godot;

Lv2ServerNode::Lv2ServerNode() {
}

Lv2ServerNode::~Lv2ServerNode() {
}

void Lv2ServerNode::_process() {
    Lv2Server::get_singleton()->process();
}

void Lv2ServerNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("process"), &Lv2ServerNode::_process);
}
