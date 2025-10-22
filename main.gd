extends Node2D


var lilv: LilvInstance
var bus = 0
var channel = 0


func _ready():
	print ("godot-lilv version: ", LilvServer.get_version(), " build: ", LilvServer.get_build())
	LilvServer.lilv_layout_changed.connect(_on_lilv_layout_changed)
	LilvServer.lilv_ready.connect(_on_lilv_ready)


func _on_lilv_layout_changed():
	pass

func _on_lilv_ready(_lilv_name: String):
	lilv = LilvServer.get_lilv("Main")
	lilv.send_control_channel(14, 0.2)

func _process(_delta):
	pass


func _on_check_button_toggled(toggled_on: bool):
	if toggled_on:
		lilv.note_on(bus, channel, 60, 90)
	else:
		lilv.note_off(bus, channel, 60)


func _on_check_button_2_toggled(toggled_on: bool):
	if toggled_on:
		lilv.note_on(bus, channel, 64, 90)
	else:
		lilv.note_off(bus, channel, 64)


func _on_check_button_3_toggled(toggled_on: bool):
	if toggled_on:
		lilv.note_on(bus, channel, 67, 90)
	else:
		lilv.note_off(bus, channel, 67)


func _on_v_slider_value_changed(value: float):
	lilv.send_control_channel(14, value * 0.2)
