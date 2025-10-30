extends SceneTree

var plugin: Lv2Instance


func _init():
	var main_scene = preload("res://main.tscn")
	var main = main_scene.instantiate()

	Lv2Server.layout_changed.connect(on_layout_changed)
	Lv2Server.lv2_ready.connect(on_ready)

	change_scene_to_packed(main_scene)


func on_layout_changed():
	print ('layout_changed')


func on_ready(_name):
	plugin = Lv2Server.get_instance("Main")

	plugin.note_on(0, 0, 64, 64)
	plugin.note_off(0, 0, 64)

	print ('it works!')
