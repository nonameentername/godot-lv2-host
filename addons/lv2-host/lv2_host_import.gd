@tool
extends EditorPlugin

var lv2_plugin: EditorLv2Instances


func _enter_tree():
	if DisplayServer.get_name() != "headless":
		lv2_plugin = preload("res://addons/lv2-host/editor_lv2_host_instances.tscn").instantiate()
		lv2_plugin.editor_interface = get_editor_interface()
		lv2_plugin.undo_redo = get_undo_redo()

		add_control_to_bottom_panel(lv2_plugin, "Lv2 Plugins")


func _exit_tree():
	if DisplayServer.get_name() != "headless":
		remove_control_from_bottom_panel(lv2_plugin)

	if lv2_plugin:
		lv2_plugin.queue_free()


func _notification(what):
	pass


func _has_main_screen():
	return false


func _make_visible(visible):
	pass


func _get_plugin_name():
	return "Lv2"


func _get_plugin_icon():
	# Must return some kind of Texture for the icon.
	return EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")
