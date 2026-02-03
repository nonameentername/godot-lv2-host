@tool
extends VBoxContainer
class_name EditorLv2Instances

var editor_interface: EditorInterface
var undo_redo: EditorUndoRedoManager
var edited_path: String
var save_timer: Timer
var container: HBoxContainer
var editor_packed_scene: PackedScene
var file_label: Label
var file_dialog: EditorFileDialog
var new_layout: bool
var renaming_instance: bool
var drop_end: EditorLv2Drop


func _init():
	renaming_instance = false

	file_dialog = EditorFileDialog.new()

	var extensions = ResourceLoader.get_recognized_extensions_for_type("Lv2Layout")
	for extension in extensions:
		file_dialog.add_filter("*.%s" % extension, "Lv2 Layout")

	add_child(file_dialog)
	file_dialog.connect("file_selected", _file_dialog_callback)

	Lv2Server.connect("layout_changed", _update)


func _ready():
	var layout = ProjectSettings.get_setting_with_override("audio/lv2-host/default_lv2_layout")
	if layout:
		edited_path = layout
	else:
		edited_path = "res://default_lv2_layout.tres"
	editor_packed_scene = preload("res://addons/lv2-host/editor_lv2_host_instance.tscn")

	save_timer = $SaveTimer
	container = $Lv2Scroll/Lv2HBox
	file_label = $TopBoxContainer/FileLabel

	_update()


func _process(_delta):
	pass


func _notification(what):
	match what:
		NOTIFICATION_ENTER_TREE:
			_update_theme()

		NOTIFICATION_THEME_CHANGED:
			_update_theme()

		NOTIFICATION_DRAG_END:
			if drop_end:
				container.remove_child(drop_end)
				drop_end.queue_free()
				drop_end = null

		NOTIFICATION_PROCESS:
			var edited = Lv2Server.get_edited()
			Lv2Server.set_edited(false)

			if edited:
				save_timer.start()


func _update_theme():
	var stylebox: StyleBoxEmpty = get_theme_stylebox("panel", "Tree")
	$Lv2Scroll.add_theme_stylebox_override("panel", stylebox)

	for control in $Lv2Scroll/Lv2HBox.get_children():
		control.editor_interface = editor_interface


func _add_instance():
	undo_redo.create_action("Add Lv2 Plugin")
	undo_redo.add_do_method(Lv2Server, "set_instance_count", Lv2Server.get_instance_count() + 1)
	undo_redo.add_undo_method(Lv2Server, "set_instance_count", Lv2Server.get_instance_count())
	undo_redo.add_do_method(self, "_update")
	undo_redo.add_undo_method(self, "_update")
	undo_redo.commit_action()


func _update():
	if renaming_instance:
		return

	for i in range(container.get_child_count(), 0, -1):
		i = i - 1
		var editor_instance: Control = container.get_child(i)
		if editor_instance:
			container.remove_child(editor_instance)
			editor_instance.queue_free()

	if drop_end:
		container.remove_child(drop_end)
		drop_end.queue_free()
		drop_end = null

	for i in range(0, Lv2Server.get_instance_count()):
		var editor_instance: EditorLv2Instance = editor_packed_scene.instantiate()
		editor_instance.editor_interface = editor_interface
		var is_main: bool = i == 0
		editor_instance.editor_instances = self
		editor_instance.is_main = is_main
		editor_instance.undo_redo = undo_redo
		container.add_child(editor_instance)

		editor_instance.connect(
			"delete_request", _delete_instance.bind(editor_instance), CONNECT_DEFERRED
		)
		editor_instance.connect("duplicate_request", _duplicate_instance, CONNECT_DEFERRED)
		editor_instance.connect(
			"vol_reset_request", _reset_volume.bind(editor_instance), CONNECT_DEFERRED
		)
		editor_instance.connect("drop_end_request", _request_drop_end, CONNECT_DEFERRED)
		editor_instance.connect("dropped", _drop_at_index, CONNECT_DEFERRED)


func _update_instance(index):
	if index >= container.get_child_count():
		return

	container.get_child(index).update_instance()


func _delete_instance(editor_instance: EditorLv2Instance):
	var index: int = editor_instance.get_index()
	if index == 0:
		push_warning("Main Lv2 can't be deleted!")
		return

	undo_redo.create_action("Delete Lv2 Plugin")
	undo_redo.add_do_method(Lv2Server, "remove_instance", index)
	undo_redo.add_undo_method(Lv2Server, "add_instance", index)
	undo_redo.add_undo_method(
		Lv2Server, "set_instance_name", index, Lv2Server.get_instance_name(index)
	)
	undo_redo.add_undo_method(Lv2Server, "set_volume_db", index, Lv2Server.get_volume_db(index))
	undo_redo.add_undo_method(Lv2Server, "set_solo", index, Lv2Server.is_solo(index))
	undo_redo.add_undo_method(Lv2Server, "set_mute", index, Lv2Server.is_mute(index))
	undo_redo.add_undo_method(Lv2Server, "set_bypass", index, Lv2Server.is_bypassing(index))

	undo_redo.add_do_method(self, "_update")
	undo_redo.add_undo_method(self, "_update")
	undo_redo.commit_action()


func _duplicate_instance(which: int):
	var add_at_pos: int = which + 1
	undo_redo.create_action("Duplicate Lv2 Plugin")
	undo_redo.add_do_method(Lv2Server, "add_instance", add_at_pos)
	undo_redo.add_do_method(
		Lv2Server, "set_instance_name", add_at_pos, Lv2Server.get_instance_name(which) + " Copy"
	)
	undo_redo.add_do_method(Lv2Server, "set_volume_db", add_at_pos, Lv2Server.get_volume_db(which))
	undo_redo.add_do_method(Lv2Server, "set_solo", add_at_pos, Lv2Server.is_solo(which))
	undo_redo.add_do_method(Lv2Server, "set_mute", add_at_pos, Lv2Server.is_mute(which))
	undo_redo.add_do_method(Lv2Server, "set_bypass", add_at_pos, Lv2Server.is_bypassing(which))
	undo_redo.add_do_method(Lv2Server, "set_uri", add_at_pos, Lv2Server.get_uri(which))
	undo_redo.add_undo_method(Lv2Server, "remove_instance", add_at_pos)
	undo_redo.add_do_method(self, "_update")
	undo_redo.add_undo_method(self, "_update")
	undo_redo.commit_action()


func _reset_volume(editor_instance: EditorLv2Instance):
	var index: int = editor_instance.get_index()
	undo_redo.create_action("Reset Lv2 Volume")
	undo_redo.add_do_method(Lv2Server, "set_volume_db", index, 0)
	undo_redo.add_undo_method(Lv2Server, "set_volume_db", index, Lv2Server.get_volume_db(index))
	undo_redo.add_do_method(self, "_update")
	undo_redo.add_undo_method(self, "_update")
	undo_redo.commit_action()


func _request_drop_end():
	if not drop_end and container.get_child_count():
		drop_end = EditorLv2Drop.new()
		container.add_child(drop_end)
		drop_end.custom_minimum_size = container.get_child(0).size
		drop_end.connect("dropped", _drop_at_index, CONNECT_DEFERRED)


func _drop_at_index(instance, index):
	undo_redo.create_action("Move Lv2")

	undo_redo.add_do_method(Lv2Server, "move_instance", instance, index)
	var real_instance: int = instance if index > instance else instance + 1
	var real_index: int = index - 1 if index > instance else index
	undo_redo.add_undo_method(Lv2Server, "move_instance", real_index, real_instance)

	undo_redo.add_do_method(self, "_update")
	undo_redo.add_undo_method(self, "_update")
	undo_redo.commit_action()


func _server_save():
	var status = Lv2Server.generate_layout()
	ResourceSaver.save(status, edited_path)


func _save_as_layout():
	file_dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	file_dialog.title = "Save Lv2 Layout As..."
	file_dialog.current_path = edited_path
	file_dialog.popup_centered_clamped(Vector2(1050, 700) * scale, 0.8)
	new_layout = false


func _new_layout():
	file_dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	file_dialog.title = "Location for New Layout..."
	file_dialog.current_path = edited_path
	file_dialog.popup_centered_clamped(Vector2(1050, 700) * scale, 0.8)
	new_layout = false


func _select_layout():
	if editor_interface:
		editor_interface.get_file_system_dock().navigate_to_path(edited_path)


func _load_layout():
	file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	file_dialog.title = "Open Lv2 Layout"
	file_dialog.current_path = edited_path
	file_dialog.popup_centered_clamped(Vector2(1050, 700) * scale, 0.8)
	new_layout = false


func _load_default_layout():
	var layout_path = ProjectSettings.get_setting_with_override("audio/lv2-host/default_lv2_layout")
	var state = ResourceLoader.load(layout_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	if not state:
		push_warning("There is no '%s' file." % layout_path)
		return

	edited_path = layout_path
	file_label.text = "Layout: %s" % edited_path.get_file()
	Lv2Server.set_layout(state)
	_update()
	call_deferred("_select_layout")


func _file_dialog_callback(filename: String):
	if file_dialog.file_mode == EditorFileDialog.FILE_MODE_OPEN_FILE:
		var state = ResourceLoader.load(filename, "", ResourceLoader.CACHE_MODE_IGNORE)
		if not state:
			push_warning("Invalid file, not a lv2 layout.")
			return

		edited_path = filename
		file_label.text = "Layout: %s" % edited_path.get_file()
		Lv2Server.set_layout(state)
		_update()
		call_deferred("_select_layout")

	elif file_dialog.file_mode == EditorFileDialog.FILE_MODE_SAVE_FILE:
		if new_layout:
			var lv2_layout: Lv2Layout = Lv2Layout.new()
			Lv2Server.set_layout(lv2_layout)

		var error = ResourceSaver.save(Lv2Server.generate_layout(), filename)
		if error != OK:
			push_warning("Error saving file: %s" % filename)
			return

		edited_path = filename
		file_label.text = "Layout: %s" % edited_path.get_file()
		_update()
		call_deferred("_select_layout")


func _set_renaming(renaming: bool):
	renaming_instance = renaming


func resource_saved(resource: Resource):
	for editor_instance in container.get_children():
		if editor_instance is EditorLv2Instance:
			editor_instance.resource_saved(resource)
