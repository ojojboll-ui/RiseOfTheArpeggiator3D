extends Node3D
## Rise of the Arpeggiator in 3D: the scene, the camera, the screens.
##
## The rules and the band run in the GDExtension (ArpGame); this script reads
## the controls, steps the game once a frame and puts what it reports on the
## floor: lowpoly enemies, the ship, bullets and sparks as MultiMeshes, the
## floor's waves through a height texture, the HUD on top.
##
## Flags after `--`, for testing without a pad:
##   --demo            the autopilot plays (starts straight into a level)
##   --level N         1..3, which song
##   --difficulty D    easy, normal or hard
##   --seconds T       quit after T seconds
##   --shots DIR       save a PNG every --every seconds (default 10) into DIR
##   --wav PATH        record what the band played
##   --reckless        the autopilot flies unarmed, to test deaths
##   --slowtest        the autopilot holds slow motion 3 s in every 12
##   --counts          print spark and bullet counts and slow motion once a second

const ENEMY_CAP := 256
const BULLET_CAP := 768
const SPARK_CAP := 8000
const PICKUP_CAP := 8
const RING_CAP := 32

const PITCH := deg_to_rad(57.0)     # how steeply the camera looks down
const DISTANCE := 12.5              # metres from what it looks at
const FOLLOW := 0.45                # how far it follows the ship off centre

enum Screen { MENU, GAME, PAUSED }

var game: ArpGame
var player: AudioStreamPlayer
var camera: Camera3D
var env: Environment
var pad := Pad.new()
var hud: Control
var postfx: ColorRect
var postfx_mat: ShaderMaterial

var floor_mat: ShaderMaterial
var floor_img: Image
var floor_tex: ImageTexture
var frame_mat: ShaderMaterial
var ship: MeshInstance3D
var ship_mat: ShaderMaterial
var enemy_mm: Array[MultiMesh] = []
var bullet_mm: MultiMesh
var spark_mm: MultiMesh
var pickup_mm: MultiMesh
var ring_mm: MultiMesh

var screen := Screen.MENU
var selected := 0
var difficulty := 1
var idle := 0.0
var best := [[0, 0, 0], [0, 0, 0], [0, 0, 0]]
var clock := 0.0
var cam_target := Vector3.ZERO

# Test flags
var demo := false
var start_level := 0
var quit_after := -1.0
var shots_dir := ""
var shot_every := 10.0
var next_shot := 0.0
var shot_index := 0
var wav_path := ""

const BEST_PATH := "user://best.json"


func _ready() -> void:
	_parse_args()
	_load_best()

	game = ArpGame.new()
	add_child(game)
	game.set_pilot_flags(_flag("--reckless"), _flag("--slowtest"))

	player = AudioStreamPlayer.new()
	player.stream = game.get_stream()
	add_child(player)
	player.play()

	if wav_path != "":
		game.record(true, max(quit_after, 30.0) + 5.0)

	_build_scene()
	_build_overlay()

	if demo:
		_start(start_level, true)


func _parse_args() -> void:
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		var a: String = args[i]
		var value := args[i + 1] if i + 1 < args.size() else ""
		match a:
			"--demo": demo = true
			"--level": start_level = clamp(int(value) - 1, 0, 2); i += 1
			"--difficulty": difficulty = 0 if value == "easy" else (2 if value == "hard" else 1); i += 1
			"--seconds": quit_after = float(value); i += 1
			"--shots": shots_dir = value; i += 1
			"--every": shot_every = float(value); i += 1
			"--wav": wav_path = value; i += 1
		i += 1


func _flag(name: String) -> bool:
	return OS.get_cmdline_user_args().has(name)


#==============================================================================
# The scene

func _build_scene() -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.004, 0.004, 0.012)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.25, 0.3, 0.5)
	env.ambient_light_energy = 0.12
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 0.55
	env.glow_strength = 1.0
	env.glow_bloom = 0.0
	env.glow_hdr_threshold = 1.0
	env.glow_blend_mode = Environment.GLOW_BLEND_MODE_SCREEN
	for level in 7:
		env.set_glow_level(level, 1.0 if level >= 1 and level <= 5 else 0.0)
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(deg_to_rad(-62.0), deg_to_rad(-35.0), 0.0)
	sun.light_energy = 0.9
	sun.light_color = Color(0.75, 0.8, 1.0)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 40.0
	add_child(sun)

	camera = Camera3D.new()
	camera.fov = 46.0
	camera.near = 0.2
	camera.far = 200.0
	add_child(camera)

	# The floor: a fine plane the floor shader lifts with the wave heights.
	var cols := game.floor_columns()
	var rows := game.floor_rows()
	var field := game.field_size()
	var plane := PlaneMesh.new()
	plane.size = field
	plane.subdivide_width = (cols - 1) * 4
	plane.subdivide_depth = (rows - 1) * 4
	floor_mat = ShaderMaterial.new()
	floor_mat.shader = load("res://shaders/floor.gdshader")
	floor_img = Image.create_from_data(cols, rows, false, Image.FORMAT_RF, game.floor_bytes())
	floor_tex = ImageTexture.create_from_image(floor_img)
	floor_mat.set_shader_parameter("heights", floor_tex)
	floor_mat.set_shader_parameter("dims", Vector2(cols, rows))
	floor_mat.set_shader_parameter("field", field)
	var floor_node := MeshInstance3D.new()
	floor_node.mesh = plane
	floor_node.material_override = floor_mat
	floor_node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(floor_node)

	# The frame round the field: four low neon walls.
	frame_mat = _neon_material(false)
	frame_mat.set_shader_parameter("body_energy", 0.25)
	frame_mat.set_shader_parameter("edge_energy", 3.0)
	var frame := MeshInstance3D.new()
	frame.mesh = Meshes.frame(field.x, field.y, 0.08, 0.12)
	frame.material_override = frame_mat
	add_child(frame)

	# The ship.
	ship_mat = _neon_material(false)
	ship_mat.set_shader_parameter("base_color", Color(1.0, 0.95, 0.8))
	ship_mat.set_shader_parameter("body_energy", 0.5)
	ship_mat.set_shader_parameter("edge_energy", 6.0)
	ship = MeshInstance3D.new()
	ship.mesh = Meshes.ship()
	ship.material_override = ship_mat
	add_child(ship)

	# Enemies, one MultiMesh per kind.
	var shapes := [Meshes.wanderer(), Meshes.seeker(), Meshes.dodger(), Meshes.splitter(), Meshes.shard(), Meshes.well()]
	for k in shapes.size():
		var mat := _neon_material(true)
		if k == 5:
			mat.set_shader_parameter("body_energy", 0.9)
		enemy_mm.push_back(_multimesh(shapes[k], mat, ENEMY_CAP))

	var bullet_mat := _neon_material(true)
	bullet_mat.set_shader_parameter("body_energy", 0.9)
	bullet_mat.set_shader_parameter("edge_energy", 1.6)
	bullet_mm = _multimesh(Meshes.bullet(), bullet_mat, BULLET_CAP, false)

	var pickup_mat := _neon_material(true)
	pickup_mat.set_shader_parameter("body_energy", 0.8)
	pickup_mm = _multimesh(Meshes.pickup(), pickup_mat, PICKUP_CAP)

	var spark_mat := ShaderMaterial.new()
	spark_mat.shader = load("res://shaders/spark.gdshader")
	spark_mm = _multimesh(Meshes.streak(), spark_mat, SPARK_CAP, false)

	var ring_mat := ShaderMaterial.new()
	ring_mat.shader = load("res://shaders/ring.gdshader")
	ring_mm = _multimesh(Meshes.ring(), ring_mat, RING_CAP, false)


func _neon_material(instanced: bool) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = load("res://shaders/neon_mesh.gdshader")
	m.set_shader_parameter("instanced", instanced)
	return m


func _multimesh(mesh: Mesh, mat: Material, cap: int, shadows := true) -> MultiMesh:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.use_custom_data = true
	mm.mesh = mesh
	mm.instance_count = cap
	mm.visible_instance_count = 0
	var node := MultiMeshInstance3D.new()
	node.multimesh = mm
	node.material_override = mat
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadows else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# The instances move all over the field; never cull them by the mesh's own box.
	node.custom_aabb = AABB(Vector3(-20, -5, -20), Vector3(40, 15, 40))
	add_child(node)
	return mm


func _build_overlay() -> void:
	var fx_layer := CanvasLayer.new()
	fx_layer.layer = 1
	add_child(fx_layer)
	postfx = ColorRect.new()
	postfx.set_anchors_preset(Control.PRESET_FULL_RECT)
	postfx.mouse_filter = Control.MOUSE_FILTER_IGNORE
	postfx_mat = ShaderMaterial.new()
	postfx_mat.shader = load("res://shaders/postfx.gdshader")
	postfx.material = postfx_mat
	postfx.visible = false
	fx_layer.add_child(postfx)

	var hud_layer := CanvasLayer.new()
	hud_layer.layer = 2
	add_child(hud_layer)
	hud = Control.new()
	hud.set_script(load("res://scripts/hud.gd"))
	hud_layer.add_child(hud)
	hud.camera = camera
	for i in 3:
		hud.songs.push_back(game.song_info(i))
	hud.difficulty_names = game.difficulty_names()
	hud.best = best


#==============================================================================
# The loop

func _input(event: InputEvent) -> void:
	pad.feed(event)


func _process(delta: float) -> void:
	clock += delta
	pad.poll()

	match screen:
		Screen.MENU: _menu(delta)
		Screen.GAME: _play(delta)
		Screen.PAUSED: _paused()

	_draw_world(delta)
	pad.end_frame()

	if shots_dir != "" and clock >= next_shot:
		next_shot += shot_every
		_screenshot()
	if quit_after > 0.0 and clock >= quit_after:
		_quit()


func _menu(delta: float) -> void:
	idle += delta
	var before := selected
	if pad.left: selected = (selected + 2) % 3
	if pad.right: selected = (selected + 1) % 3
	if pad.up and difficulty > 0:
		difficulty -= 1
		idle = 0.0
	if pad.down and difficulty < 2:
		difficulty += 1
		idle = 0.0
	if selected != before:
		idle = 0.0
		game.blip(int(game.song_info(selected).key_note))
	if pad.confirm:
		_start(selected, false)
	elif idle > 30.0:
		_start(selected, true)   # attract mode
	else:
		game.ambient_step(delta)


func _start(level: int, autopilot: bool) -> void:
	game.start(level, difficulty, autopilot)
	screen = Screen.GAME


func _back_to_menu() -> void:
	if not game.get_autopilot():
		var lvl := game.get_level()
		if game.get_score() > best[difficulty][lvl]:
			best[difficulty][lvl] = game.get_score()
			_save_best()
	game.end_game()
	screen = Screen.MENU
	idle = 0.0


func _play(delta: float) -> void:
	var auto := game.get_autopilot()

	# In attract mode any key hands the game back to the menu.
	if auto and not demo and (pad.confirm or pad.back or pad.pause or pad.left or pad.right):
		_back_to_menu()
		return
	if pad.pause and not auto:
		screen = Screen.PAUSED
		game.pause_band()
		return

	var c := pad.controls(_mouse_aim())
	game.step(delta, c.move, c.aim, c.fire, c.bomb, c.slow)
	if game.take_died():
		pad.rumble(1.0, 0.6, 0.5)
	if game.take_bombed():
		pad.rumble(0.8, 0.8, 0.3)

	var phase := game.get_phase()
	if phase == 2 or phase == 3:   # game over, cleared
		if pad.confirm and not auto:
			_back_to_menu()
		elif auto and not demo:
			_back_to_menu()
		elif demo and phase == 2:
			_start(game.get_level(), true)


func _paused() -> void:
	if pad.pause or pad.confirm:
		screen = Screen.GAME
		game.resume_band()
	elif pad.back:
		_back_to_menu()


## The direction from the ship to the mouse on the floor, in the field's plane.
func _mouse_aim() -> Vector2:
	var mouse := get_viewport().get_mouse_position()
	var origin := camera.project_ray_origin(mouse)
	var dir := camera.project_ray_normal(mouse)
	var ship_at := game.ship_position()
	if abs(dir.y) < 1e-4:
		return Vector2.ZERO
	var t := (ship_at.y - origin.y) / dir.y
	if t <= 0.0:
		return Vector2.ZERO
	var hit := origin + dir * t
	return Vector2(hit.x - ship_at.x, hit.z - ship_at.z)


#==============================================================================
# Drawing

func _draw_world(delta: float) -> void:
	var d := game.hud()
	var playing := screen != Screen.MENU and game.is_running()

	# Floor waves.
	floor_img.set_data(game.floor_columns(), game.floor_rows(), false, Image.FORMAT_RF, game.floor_bytes())
	floor_tex.update(floor_img)
	var tint: Color = hud.songs[selected].tint if not playing else d.tint
	floor_mat.set_shader_parameter("tint", tint)
	floor_mat.set_shader_parameter("kick", d.kick)

	# Instances.
	for k in enemy_mm.size():
		_fill(enemy_mm[k], game.enemy_buffer(k, ENEMY_CAP))
	_fill(bullet_mm, game.bullet_buffer(BULLET_CAP))
	_fill(spark_mm, game.spark_buffer(SPARK_CAP))
	if _flag("--counts") and Engine.get_process_frames() % 60 == 0:
		print("sparks %d bullets %d slow %.2f meter %.2f t %.1f" % [spark_mm.visible_instance_count, bullet_mm.visible_instance_count, d.slow_amount, d.slow_meter, clock])
	_fill(pickup_mm, game.pickup_buffer(PICKUP_CAP))
	_fill(ring_mm, game.ring_buffer(RING_CAP))

	# The ship, in the colour of the chord being played round its outline.
	ship.visible = playing and game.ship_visible()
	if ship.visible:
		ship.transform = game.ship_transform()
		var chord: Color = d.chord_colors[d.chord]
		ship_mat.set_shader_parameter("base_color", Color(1.0, 0.95, 0.8).lerp(chord, 0.35))
		ship_mat.set_shader_parameter("kick", d.kick)

	# The frame pumps with the kick and turns blue in slow motion.
	var slow_a: float = d.slow_amount if playing else 0.0
	var accent: Color = d.accent if playing else hud.songs[selected].accent
	frame_mat.set_shader_parameter("base_color", Color(accent.lerp(Color(0.55, 0.75, 1.0), slow_a), 0.55 + 0.35 * float(d.kick) + 0.3 * slow_a))
	frame_mat.set_shader_parameter("kick", d.kick)

	# Camera: follows the ship part of the way; in slow motion all the way,
	# closing in from 100 to 150% over the slow motion, logarithmically.
	var ship_at := game.ship_position() if playing else Vector3.ZERO
	ship_at.y = 0.0
	var zoom := pow(1.5, float(d.slow_progress) * slow_a)
	var want := (ship_at * FOLLOW).lerp(ship_at, slow_a)
	cam_target = cam_target.lerp(want, 1.0 - exp(-delta * 5.0))
	var back := Vector3(0.0, sin(PITCH), cos(PITCH))
	var shake: Vector2 = d.shake if playing else Vector2.ZERO
	camera.position = cam_target + back * (DISTANCE / zoom) + Vector3(shake.x, 0.0, shake.y)
	camera.look_at(cam_target + Vector3(shake.x, 0.0, shake.y) * 0.5, Vector3.UP)

	# Flash on deaths and bombs; slow motion's blur and aberration round the ship.
	env.tonemap_exposure = 1.0 + float(d.flash) if playing else 1.0
	postfx.visible = slow_a > 0.002
	if postfx.visible:
		var vp := get_viewport().get_visible_rect().size
		postfx_mat.set_shader_parameter("centre", camera.unproject_position(game.ship_position()) / vp)
		postfx_mat.set_shader_parameter("amount", slow_a)

	hud.mode = 0 if screen == Screen.MENU else 1
	hud.data = d
	hud.paused = screen == Screen.PAUSED
	hud.attract = playing and game.get_autopilot() and not demo
	hud.has_pad = pad.has_pad()
	hud.selected = selected
	hud.difficulty = difficulty
	hud.clock = clock
	hud.queue_redraw()


func _fill(mm: MultiMesh, buffer: PackedFloat32Array) -> void:
	mm.buffer = buffer
	mm.visible_instance_count = game.last_count()


#==============================================================================

func _screenshot() -> void:
	var img := get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(shots_dir)
	img.save_png("%s/shot-%02d.png" % [shots_dir, shot_index])
	shot_index += 1


func _quit() -> void:
	if wav_path != "":
		game.save_wav(wav_path)
	get_tree().quit()


func _load_best() -> void:
	if not FileAccess.file_exists(BEST_PATH):
		return
	var f := FileAccess.open(BEST_PATH, FileAccess.READ)
	var v = JSON.parse_string(f.get_as_text())
	if v is Array and v.size() == 3:
		for d in 3:
			for s in 3:
				best[d][s] = int(v[d][s])


func _save_best() -> void:
	var f := FileAccess.open(BEST_PATH, FileAccess.WRITE)
	f.store_string(JSON.stringify(best))
