extends Node2D

@onready var dropdown: OptionButton = $OptionButton
var editor: Lv2Editor
var lv2: Lv2Instance
var bus = 0
var channel = 0


func _ready():
	editor = $lv2_editor
	print ("godot-lv2-host version: ", Lv2Server.get_version(), " build: ", Lv2Server.get_build())
	Lv2Server.lv2_layout_changed.connect(_on_lv2_layout_changed)
	Lv2Server.lv2_ready.connect(_on_lv2_ready)


func _on_lv2_layout_changed():
	pass


func _on_lv2_ready(_lv2_name: String):
	lv2 = Lv2Server.get_lv2("Main")
	var input_controls: Array[Lv2Control] = lv2.get_input_controls()
	for input_control in input_controls:
		if input_control.logarithmic:
			print (input_control.symbol, " - ", input_control.name)
			print ("    min = ", input_control.min)
			print ("    max = ", input_control.max)
			for key in input_control.get_choices():
				var value = input_control.get_choices()[key]
				print ("    ", key, " ", value)

	for preset in lv2.get_presets():
		dropdown.add_item(preset)

	editor.initialize(lv2)

func _process(_delta):
	pass


func _on_check_button_toggled(toggled_on: bool):
	if toggled_on:
		lv2.note_on(bus, channel, 60, 90)
	else:
		lv2.note_off(bus, channel, 60)


func _on_check_button_2_toggled(toggled_on: bool):
	if toggled_on:
		lv2.note_on(bus, channel, 64, 90)
	else:
		lv2.note_off(bus, channel, 64)


func _on_check_button_3_toggled(toggled_on: bool):
	if toggled_on:
		lv2.note_on(bus, channel, 67, 90)
	else:
		lv2.note_off(bus, channel, 67)


func _on_v_slider_value_changed(value: float):
	lv2.send_input_control_channel(14, value)


func _on_option_button_item_selected(index: int) -> void:
	var preset = dropdown.get_item_text(index)
	lv2.load_preset(preset)
	editor.update(lv2)
