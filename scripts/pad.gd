class_name Pad
extends RefCounted
## Gamepad, keyboard and mouse, read once a frame, as the 2D game read them.
##
## Left stick or WASD flies; right stick, arrows or a held left mouse button
## aims and fires; RT, LB, RB, Space or the right mouse button bombs; LT or
## Shift is slow motion. Menus get one step per push of the stick or D-pad.

var device := -1

# Pressed this frame.
var up := false
var down := false
var left := false
var right := false
var confirm := false
var back := false
var pause := false
var bomb := false
var any := false

var _held := {}
var _trigger_held := false
var _stick_nav := false
var _mouse_bomb := false
var _mouse_down := false


func has_pad() -> bool:
	return device >= 0


## Call from the scene's _input, so key and button presses are never missed.
func feed(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		any = true
		match event.physical_keycode:
			KEY_W, KEY_UP: up = true
			KEY_S, KEY_DOWN: down = true
			KEY_A, KEY_LEFT: left = true
			KEY_D, KEY_RIGHT: right = true
			KEY_ENTER, KEY_KP_ENTER: confirm = true
			KEY_BACKSPACE: back = true
			KEY_ESCAPE, KEY_P: pause = true
			KEY_SPACE: bomb = true
	elif event is InputEventJoypadButton and event.pressed:
		any = true
		match event.button_index:
			JOY_BUTTON_A: confirm = true
			JOY_BUTTON_B, JOY_BUTTON_BACK: back = true
			JOY_BUTTON_START: pause = true
			JOY_BUTTON_DPAD_UP: up = true
			JOY_BUTTON_DPAD_DOWN: down = true
			JOY_BUTTON_DPAD_LEFT: left = true
			JOY_BUTTON_DPAD_RIGHT: right = true
			JOY_BUTTON_LEFT_SHOULDER, JOY_BUTTON_RIGHT_SHOULDER: bomb = true
	elif event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_mouse_down = event.pressed
		if event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			bomb = true


## Call at the end of the frame.
func end_frame() -> void:
	up = false
	down = false
	left = false
	right = false
	confirm = false
	back = false
	pause = false
	bomb = false
	any = false


## Call at the start of the frame: picks up a pad and the edges on its axes.
func poll() -> void:
	var pads := Input.get_connected_joypads()
	device = pads[0] if pads.size() > 0 else -1
	if device < 0:
		return

	var held := Input.get_joy_axis(device, JOY_AXIS_TRIGGER_RIGHT) > 0.6
	if held and not _trigger_held:
		bomb = true
		any = true
	_trigger_held = held

	var s := Vector2(Input.get_joy_axis(device, JOY_AXIS_LEFT_X), Input.get_joy_axis(device, JOY_AXIS_LEFT_Y))
	var pushed := s.length() > 0.6
	if pushed and not _stick_nav:
		any = true
		if abs(s.x) > abs(s.y):
			if s.x > 0.0: right = true
			else: left = true
		else:
			if s.y > 0.0: down = true
			else: up = true
	_stick_nav = pushed


static func deadzone(v: Vector2, inner: float) -> Vector2:
	# Radial, and rescaled so the travel past the deadzone is the whole range.
	var m := v.length()
	if m < inner:
		return Vector2.ZERO
	return v / m * min(1.0, (m - inner) / (1.0 - inner))


## The game's controls. `mouse_aim` is the direction from the ship to the
## mouse on the floor, in the field's plane (x right, y towards the camera).
func controls(mouse_aim: Vector2) -> Dictionary:
	var move := Vector2.ZERO
	var aim := Vector2.ZERO
	var fire := false
	var slow := Input.is_physical_key_pressed(KEY_SHIFT)

	if device >= 0:
		move = deadzone(Vector2(Input.get_joy_axis(device, JOY_AXIS_LEFT_X), Input.get_joy_axis(device, JOY_AXIS_LEFT_Y)), 0.18)
		aim = deadzone(Vector2(Input.get_joy_axis(device, JOY_AXIS_RIGHT_X), Input.get_joy_axis(device, JOY_AXIS_RIGHT_Y)), 0.25)
		slow = slow or Input.get_joy_axis(device, JOY_AXIS_TRIGGER_LEFT) > 0.4

	if move == Vector2.ZERO:
		var k := Vector2(
			float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A)),
			float(Input.is_physical_key_pressed(KEY_S)) - float(Input.is_physical_key_pressed(KEY_W)))
		move = k.normalized()

	if aim != Vector2.ZERO:
		fire = true
	else:
		var k := Vector2(
			float(Input.is_physical_key_pressed(KEY_RIGHT)) - float(Input.is_physical_key_pressed(KEY_LEFT)),
			float(Input.is_physical_key_pressed(KEY_DOWN)) - float(Input.is_physical_key_pressed(KEY_UP)))
		if k != Vector2.ZERO:
			aim = k.normalized()
			fire = true
		elif _mouse_down and mouse_aim != Vector2.ZERO:
			aim = mouse_aim.normalized()
			fire = true

	return { "move": move, "aim": aim, "fire": fire, "bomb": bomb, "slow": slow }


func rumble(weak: float, strong: float, seconds: float) -> void:
	if device >= 0:
		Input.start_joy_vibration(device, weak, strong, seconds)
