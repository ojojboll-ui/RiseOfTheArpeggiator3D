extends Node3D
## Rise of the Arpeggiator in 3D: the scene, the camera, the screens.
##
## The rules and the band run in the GDExtension (ArpGame); this script reads
## the controls, steps the game once a frame and puts what it reports on the
## level: its floor with hills, waves and zones, the walls, the towers and the
## gate, lowpoly enemies, the ship, bullets and sparks as MultiMeshes, and the
## HUD on top.
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
##   --counts          print counts and state once a second

const ENEMY_CAP := 256
const BULLET_CAP := 768
const SPARK_CAP := 8000
const PICKUP_CAP := 8
const RING_CAP := 32

const PITCH := deg_to_rad(57.0)     # how steeply the camera looks down
const DISTANCE := 12.5              # metres from what it looks at
const RIM_HEIGHT := 0.5             # the rooms' edges, metres
const WALL_HEIGHT := 0.8            # walls inside the rooms, above the highest ground under them
const TOWER_HEIGHT := 2.6

enum Screen { MENU, GAME, PAUSED }

var game: ArpGame
var player: AudioStreamPlayer
var camera: Camera3D
var env: Environment
var pad := Pad.new()
var hud: Control
var postfx: ColorRect
var postfx_mat: ShaderMaterial

# The level's scene, rebuilt for every level.
var level_root: Node3D
var floor_mat: ShaderMaterial
var wave_img: Image
var wave_tex: ImageTexture
var edge_mats: Array[ShaderMaterial] = []
var edge_colors: Array[Color] = []
var tower_nodes: Array[MeshInstance3D] = []
var tower_mats: Array[ShaderMaterial] = []
var gate_node: MeshInstance3D
var gate_mat: ShaderMaterial

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
var clear_time := 0.0
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
	_build_level()
	cam_target = game.start_position()

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
	sun.rotation = Vector3(deg_to_rad(-55.0), deg_to_rad(-35.0), 0.0)
	sun.light_energy = 1.1
	sun.light_color = Color(0.75, 0.8, 1.0)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 30.0
	add_child(sun)

	camera = Camera3D.new()
	camera.fov = 46.0
	camera.near = 0.2
	camera.far = 200.0
	add_child(camera)

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


## The level's own scene: floor, edges, walls, towers, gate. Called whenever
## the game moves to another level (the menu shows the first level's verse).
func _build_level() -> void:
	if level_root:
		level_root.queue_free()
	level_root = Node3D.new()
	add_child(level_root)
	edge_mats.clear()
	edge_colors.clear()
	tower_nodes.clear()
	tower_mats.clear()

	var map := game.field_size()
	var rooms := game.level_rooms()
	var accent: Color = game.hud().accent

	# Floor: a fine plane the floor shader lifts with the hills and waves.
	var plane := PlaneMesh.new()
	plane.size = map
	plane.subdivide_width = int(map.x / 0.2)
	plane.subdivide_depth = int(map.y / 0.2)
	floor_mat = ShaderMaterial.new()
	floor_mat.shader = load("res://shaders/floor.gdshader")
	var wc := game.floor_columns()
	var wr := game.floor_rows()
	wave_img = Image.create_from_data(wc, wr, false, Image.FORMAT_RF, game.floor_bytes())
	wave_tex = ImageTexture.create_from_image(wave_img)
	floor_mat.set_shader_parameter("waves", wave_tex)
	floor_mat.set_shader_parameter("wave_dims", Vector2(wc, wr))
	floor_mat.set_shader_parameter("wave_cell", map.x / (wc - 1))
	var hc := game.hill_columns()
	var hr := game.hill_rows()
	var hills := Image.create_from_data(hc, hr, false, Image.FORMAT_RF, game.hill_bytes())
	floor_mat.set_shader_parameter("hills", ImageTexture.create_from_image(hills))
	floor_mat.set_shader_parameter("hill_dims", Vector2(hc, hr))
	floor_mat.set_shader_parameter("hill_cell", game.hill_cell())
	var ic := game.info_columns()
	var ir := game.info_rows()
	var info := Image.create_from_data(ic, ir, false, Image.FORMAT_RGBA8, game.floor_info_bytes())
	floor_mat.set_shader_parameter("info", ImageTexture.create_from_image(info))
	floor_mat.set_shader_parameter("info_dims", Vector2(ic, ir))
	floor_mat.set_shader_parameter("info_cell", map.x / ic)
	var floor_node := MeshInstance3D.new()
	floor_node.mesh = plane
	floor_node.material_override = floor_mat
	floor_node.position = Vector3(map.x * 0.5, 0.0, map.y * 0.5)
	floor_node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	floor_node.custom_aabb = AABB(Vector3(-map.x, -3, -map.y), Vector3(map.x * 2, 6, map.y * 2))
	level_root.add_child(floor_node)

	# Edges and walls, in the colour of the room they stand in (passages in
	# the level's accent): one mesh and material per colour.
	var groups := {}   # zone -> Array of faces
	var rim := game.level_rim()
	for i in range(0, rim.size(), 6):
		var a := Vector2(rim[i], rim[i + 1])
		var b := Vector2(rim[i + 2], rim[i + 3])
		var n := Vector2(rim[i + 4], rim[i + 5])
		var inside := (a + b) * 0.5 - n * 0.3
		var outline := PackedVector2Array([a, b, b + n * 0.12, a + n * 0.12])
		_group(groups, _zone_of(rooms, inside)).append(Meshes.prism_faces(outline, -0.6, RIM_HEIGHT))
	var walls := game.level_walls()
	for i in range(0, walls.size(), 5):
		var lo := Vector3(walls[i], -0.6, walls[i + 1])
		var hi := Vector3(walls[i + 2], walls[i + 4] + WALL_HEIGHT, walls[i + 3])
		var centre := Vector2((lo.x + hi.x) * 0.5, (lo.z + hi.z) * 0.5)
		_group(groups, _zone_of(rooms, centre)).append(Meshes.box(lo, hi))
	for zone in groups:
		var st := SurfaceTool.new()
		for faces in groups[zone]:
			st.append_from(Meshes.solid(faces), 0, Transform3D.IDENTITY)
		var mat := _neon_material(false)
		mat.set_shader_parameter("body_energy", 0.06)
		mat.set_shader_parameter("albedo_amount", 0.07)
		mat.set_shader_parameter("edge_energy", 3.2)
		var color: Color = accent if zone < 0 else _room_color(rooms, zone)
		mat.set_shader_parameter("base_color", color)
		var node := MeshInstance3D.new()
		node.mesh = st.commit()
		node.material_override = mat
		level_root.add_child(node)
		edge_mats.push_back(mat)
		edge_colors.push_back(color)

	# Towers.
	for t in game.tower_states():
		var mat := _neon_material(false)
		mat.set_shader_parameter("base_color", t.color)
		mat.set_shader_parameter("body_energy", 0.45)
		mat.set_shader_parameter("edge_energy", 5.0)
		var node := MeshInstance3D.new()
		node.mesh = Meshes.tower(t.radius, TOWER_HEIGHT)
		node.material_override = mat
		node.position = t.pos
		level_root.add_child(node)
		tower_nodes.push_back(node)
		tower_mats.push_back(mat)

	# The gate.
	gate_mat = _neon_material(false)
	gate_mat.set_shader_parameter("body_energy", 0.6)
	gate_mat.set_shader_parameter("edge_energy", 6.0)
	gate_node = MeshInstance3D.new()
	gate_node.mesh = Meshes.gate(1.0, 0.78, 0.18)
	gate_node.material_override = gate_mat
	level_root.add_child(gate_node)

	hud.rooms = rooms


func _group(groups: Dictionary, zone: int) -> Array:
	if not groups.has(zone):
		groups[zone] = []
	return groups[zone]


func _zone_of(rooms: Array, p: Vector2) -> int:
	for r in rooms:
		if r.zone >= 0 and (r.rect as Rect2).grow(0.05).has_point(p):
			return r.zone
	return -1


func _room_color(rooms: Array, zone: int) -> Color:
	for r in rooms:
		if r.zone == zone:
			return r.color
	return Color.WHITE


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
	# The instances move all over the level; never cull them by the mesh's own box.
	node.custom_aabb = AABB(Vector3(-10, -5, -10), Vector3(80, 15, 80))
	add_child(node)
	return mm


func _build_overlay() -> void:
	var fx_layer := CanvasLayer.new()
	fx_layer.layer = 1
	add_child(fx_layer)
	postfx = ColorRect.new()
	postfx.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
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
	start_level = level
	game.start(level, difficulty, autopilot)
	screen = Screen.GAME
	clear_time = 0.0
	_build_level()
	cam_target = game.ship_position()


func _back_to_menu() -> void:
	if not game.get_autopilot():
		# Best scores are kept by the song a run started on.
		var lvl := start_level
		if game.get_score() > best[difficulty][lvl]:
			best[difficulty][lvl] = game.get_score()
			_save_best()
	game.end_game()
	screen = Screen.MENU
	idle = 0.0
	_build_level()


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
	if phase == 2:   # game over
		if pad.confirm and not auto:
			_back_to_menu()
		elif auto and not demo:
			_back_to_menu()
		elif demo:
			_start(start_level, true)
	elif phase == 3:   # through the gate
		clear_time += delta
		var last: bool = game.hud().finished
		if last:
			if (pad.confirm and not auto) or (auto and clear_time > 6.0):
				if demo:
					_start(0, true)
				else:
					_back_to_menu()
		elif clear_time > 4.0 or (pad.confirm and not auto):
			game.next_level()
			clear_time = 0.0
			_build_level()


func _paused() -> void:
	if pad.pause or pad.confirm:
		screen = Screen.GAME
		game.resume_band()
	elif pad.back:
		_back_to_menu()


## The direction from the ship to the mouse on the floor, in the level's plane.
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
	d.towers = game.tower_states()
	d.gate = game.gate_state()

	# Floor waves.
	wave_img.set_data(game.floor_columns(), game.floor_rows(), false, Image.FORMAT_RF, game.floor_bytes())
	wave_tex.update(wave_img)
	floor_mat.set_shader_parameter("kick", d.kick)

	# Instances.
	for k in enemy_mm.size():
		_fill(enemy_mm[k], game.enemy_buffer(k, ENEMY_CAP))
	_fill(bullet_mm, game.bullet_buffer(BULLET_CAP))
	_fill(spark_mm, game.spark_buffer(SPARK_CAP))
	if _flag("--counts") and Engine.get_process_frames() % 60 == 0:
		print("t %.1f sparks %d zone %d section %s towers %d gate %s phase %d score %d" % [clock, spark_mm.visible_instance_count,
			d.zone, d.section_name, d.towers_left, d.gate_open, d.phase, d.score])
	_fill(pickup_mm, game.pickup_buffer(PICKUP_CAP))
	_fill(ring_mm, game.ring_buffer(RING_CAP))

	# The ship, in the colour of the chord being played round its outline.
	ship.visible = playing and game.ship_visible()
	if ship.visible:
		ship.transform = game.ship_transform()
		var chord: Color = d.chord_colors[d.chord]
		ship_mat.set_shader_parameter("base_color", Color(1.0, 0.95, 0.8).lerp(chord, 0.35))
		ship_mat.set_shader_parameter("kick", d.kick)

	# Edges and walls pump with the kick and turn blue in slow motion.
	var slow_a: float = d.slow_amount if playing else 0.0
	for i in edge_mats.size():
		edge_mats[i].set_shader_parameter("base_color", edge_colors[i].lerp(Color(0.55, 0.75, 1.0), slow_a))
		edge_mats[i].set_shader_parameter("kick", d.kick)

	# Towers: flash when hit, flicker when nearly down, sink when they fall.
	for i in tower_nodes.size():
		var t: Dictionary = d.towers[i]
		var node := tower_nodes[i]
		node.visible = float(t.fall) < 1.0
		node.position = t.pos - Vector3(0, float(t.fall) * (TOWER_HEIGHT + 1.0), 0)
		node.rotation.y += delta * (0.4 + 1.6 * (1.0 - float(t.health)))
		var c: Color = t.color
		var weak := float(t.health) < 0.25 and fmod(clock * 9.0, 1.0) < 0.3
		tower_mats[i].set_shader_parameter("base_color", c.lerp(Color.WHITE, float(t.flash) * 0.6) * (0.4 if weak else 1.0))
		tower_mats[i].set_shader_parameter("kick", d.kick)

	# The gate: small and dim where it will open; open, big, bright and turning.
	var g: Dictionary = d.gate
	var open: bool = g.open
	var grow: float = clamp(float(g.time) * 1.5, 0.0, 1.0) if open else 0.0
	gate_node.position = g.pos + Vector3(0, 0.4 + 0.9 * grow, 0)
	gate_node.scale = Vector3.ONE * lerp(0.35, 1.2, grow)
	gate_node.rotation.y += delta * (0.3 + 1.2 * grow)
	var gate_color: Color = Color(0.5, 0.5, 0.6).lerp(d.accent, grow)
	gate_mat.set_shader_parameter("base_color", Color(gate_color, 0.35 + 0.65 * grow))
	gate_mat.set_shader_parameter("kick", d.kick)

	# Camera: follows the ship; in slow motion it closes in from 100 to 150%
	# over the slow motion, logarithmically.
	var want := game.ship_position() if playing else game.start_position()
	want.y = 0.0
	var zoom := pow(1.5, float(d.slow_progress) * slow_a)
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
