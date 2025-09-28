#include "lilv_server_node.h"
#include "lilv_server.h"

using namespace godot;

LilvServerNode::LilvServerNode() {
}

LilvServerNode::~LilvServerNode() {
}

void LilvServerNode::_process() {
    LilvServer::get_singleton()->process();
}

void LilvServerNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("process"), &LilvServerNode::_process);
}
