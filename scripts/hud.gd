extends Control
## Everything flat on the screen: the HUD in play, the menu, the pause and
## attract-mode captions. It is drawn above the 3D view and the slow-motion
## effects, so none of them touch it (as in the 2D game since 2026-10-13).
##
## Laid out on the 2D game's 1920 x 1080 canvas; the window stretches it.

enum Mode { MENU, GAME }

var mode := Mode.MENU
var data := {}             # ArpGame.hud() for the frame, with towers and gate
var rooms := []            # ArpGame.level_rooms(), for the map
var camera: Camera3D
var paused := false
var attract := false
var has_pad := false

# Menu state, set by main.gd.
var songs := []            # ArpGame.song_info() per song
var selected := 0
var difficulty := 1
var difficulty_names := PackedStringArray()
var best := [[0, 0, 0], [0, 0, 0], [0, 0, 0]]
var clock := 0.0

var font: Font
const SLOW_COLOR := Color(0.55, 0.75, 1.0)
const WHITE := Color(1, 1, 1)


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	var f := SystemFont.new()
	f.font_names = PackedStringArray(["Bahnschrift", "Segoe UI", "Arial"])
	f.font_weight = 600
	f.antialiasing = TextServer.FONT_ANTIALIASING_GRAY
	font = f


func _draw() -> void:
	if mode == Mode.MENU:
		_draw_menu()
	else:
		_draw_game()


#==============================================================================
# Drawing helpers

## Text with a soft neon halo; `align` 0 left, 0.5 centred, 1 right; `pos.y` is the top.
func text(s: String, pos: Vector2, px: float, color: Color, align := 0.0, glow := true) -> void:
	var size := int(px)
	var w := font.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x
	var at := Vector2(pos.x - w * align, pos.y + font.get_ascent(size))
	if glow:
		draw_string_outline(font, at, s, HORIZONTAL_ALIGNMENT_LEFT, -1, size, max(4, size / 5), Color(color, color.a * 0.28))
	draw_string(font, at, s, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color)


func text_width(s: String, px: float) -> float:
	return font.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, int(px)).x


func glow_line(a: Vector2, b: Vector2, color: Color, width: float) -> void:
	draw_line(a, b, Color(color, color.a * 0.25), width * 2.6, true)
	draw_line(a, b, color, width, true)


func glow_poly(points: PackedVector2Array, color: Color, width: float) -> void:
	var closed := points.duplicate()
	closed.push_back(points[0])
	draw_polyline(closed, Color(color, color.a * 0.25), width * 2.6, true)
	draw_polyline(closed, color, width, true)


func glow_circle(c: Vector2, r: float, color: Color, width: float) -> void:
	draw_arc(c, r, 0, TAU, 48, Color(color, color.a * 0.25), width * 2.6, true)
	draw_arc(c, r, 0, TAU, 48, color, width, true)


#==============================================================================
# In play

func _draw_game() -> void:
	if data.is_empty():
		return
	var W := size.x
	var H := size.y
	var accent: Color = data.accent
	var t: float = data.time

	# Score and multiplier.
	var score := str(data.score)
	text(score, Vector2(40, 22), 40, WHITE)
	text("x%d" % data.multiplier, Vector2(40 + text_width(score, 40) + 22, 34), 26, accent)

	# Slow-motion meter beside the score: dim when it cannot start yet,
	# pulsing while it runs.
	var x0 := 360.0
	var x1 := 650.0
	var y0 := 34.0
	var y1 := 60.0
	var meter: float = data.slow_meter
	var a := (0.8 + 0.2 * sin(t * 20.0)) if data.slow_running else (0.8 if meter > 0.15 else 0.35)
	glow_poly(PackedVector2Array([Vector2(x0, y0), Vector2(x1, y0), Vector2(x1, y1), Vector2(x0, y1)]), Color(SLOW_COLOR, 0.45), 2.0)
	if meter > 0.0:
		draw_rect(Rect2(x0 + 5, y0 + 5, (x1 - x0 - 10) * meter, y1 - y0 - 10), Color(SLOW_COLOR, a))
	text("SLOW MOTION", Vector2(x0, 10), 16, Color(SLOW_COLOR, max(a, 0.55)))

	# Song, difficulty, and the three towers in their colours: struck out
	# when down, the gate when they all are.
	text("%s  ·  %s" % [data.song, data.difficulty], Vector2(W * 0.5, 12), 30, accent, 0.5)
	for i in data.towers.size():
		var tw: Dictionary = data.towers[i]
		var c: Color = tw.color
		var x: float = W * 0.5 + (i - 1) * 54.0
		var pts := PackedVector2Array([Vector2(x - 11, 74), Vector2(x - 6, 52), Vector2(x + 6, 52), Vector2(x + 11, 74)])
		if tw.alive:
			glow_poly(pts, c, 2.5)
			draw_rect(Rect2(x - 9, 78, 18.0 * float(tw.health), 4), c)
		else:
			glow_poly(pts, Color(c, 0.3), 1.5)
			draw_line(Vector2(x - 14, 76), Vector2(x + 14, 50), Color(c, 0.7), 2.5, true)
	if data.gate_open:
		text("GATE OPEN", Vector2(W * 0.5, 88), 20, Color(accent, 0.6 + 0.4 * sin(clock * 6.0)), 0.5, false)

	_draw_map(Vector2(W - 270.0, 80.0), 230.0)

	# Lives and bombs.
	for i in data.lives:
		var c := Vector2(W - 60.0 - 44.0 * i, 40.0)
		glow_poly(PackedVector2Array([c + Vector2(0, -14), c + Vector2(11, 10), c + Vector2(0, 3), c + Vector2(-11, 10)]), WHITE, 2.5)
	for i in data.bombs:
		glow_circle(Vector2(W - 420.0 + 34.0 * i, 40.0), 11.0, data.bomb_color, 3.0)

	_draw_wheel(Vector2(160.0, H - 170.0))
	var zc: Color = data.zone_colors[data.section]
	text(data.section_name, Vector2(160.0, H - 300.0), 24, zc, 0.5)
	_draw_layers(Vector2(W - 300.0, H - 40.0))

	# Popups: score numbers where things died.
	if camera:
		for p in data.popups:
			if camera.is_position_behind(p[0]):
				continue
			var at := camera.unproject_position(p[0]) * _canvas_scale()
			text(p[1], at, float(p[4]) * 1.1, Color(p[3], float(p[2])), 0.5)

	# Banner: layers in and out, the song name, the end.
	var life: float = data.banner_life
	if life > 0.0:
		text(data.banner, Vector2(W * 0.5, H * 0.36), 68, Color(data.banner_color, clamp(life * 1.5, 0.0, 1.0)), 0.5)

	var phase: int = data.phase
	if phase == 2 or (phase == 3 and data.finished):
		text("SCORE %d" % data.score, Vector2(W * 0.5, H * 0.36 + 100.0), 42, WHITE, 0.5)
		text("PRESS A" if has_pad else "PRESS ENTER", Vector2(W * 0.5, H * 0.36 + 170.0), 26, Color(1, 1, 1, 0.5 + 0.5 * sin(clock * 4.0)), 0.5)
	elif phase == 3:
		text("SCORE %d" % data.score, Vector2(W * 0.5, H * 0.36 + 100.0), 42, WHITE, 0.5)
		text("NEXT  ·  " + String(songs[min(int(data.level) + 1, 2)].name), Vector2(W * 0.5, H * 0.36 + 170.0), 28, Color(songs[min(int(data.level) + 1, 2)].accent, 0.9), 0.5)

	if paused:
		draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.45))
		text("PAUSED", Vector2(W * 0.5, H * 0.40), 96, WHITE, 0.5)
		text("A  RESUME      B  MENU", Vector2(W * 0.5, H * 0.40 + 130.0), 28, Color(1, 1, 1, 0.7), 0.5)

	if attract:
		text("ATTRACT MODE  ·  PRESS A", Vector2(W * 0.5, H - 70.0), 24, Color(1, 1, 1, 0.5 + 0.5 * sin(clock * 3.0)), 0.5)


## A map of the level: rooms in their colours, towers, the gate, the ship.
func _draw_map(at: Vector2, width: float) -> void:
	var map: Vector2 = data.map_size
	if map.x <= 0.0:
		return
	var s := width / map.x
	draw_rect(Rect2(at - Vector2(8, 8), map * s + Vector2(16, 16)), Color(0, 0, 0, 0.45))
	for r in rooms:
		var rect: Rect2 = r.rect
		var box := Rect2(at + rect.position * s, rect.size * s)
		var c: Color = r.color
		var here: bool = r.zone == data.zone
		draw_rect(box, Color(c, 0.22 if here else 0.10))
		draw_rect(box, Color(c, 0.85 if here else 0.45), false, 2.0 if here else 1.2)
	for tw in data.towers:
		var p: Vector3 = tw.pos
		var c: Color = tw.color
		var q := at + Vector2(p.x, p.z) * s
		if tw.alive:
			draw_circle(q, 5.0, c)
		else:
			draw_arc(q, 5.0, 0, TAU, 12, Color(c, 0.5), 1.5, true)
	var g: Dictionary = data.gate
	var gp: Vector3 = g.pos
	var gq := at + Vector2(gp.x, gp.z) * s
	if g.open:
		draw_arc(gq, 7.0 + 2.0 * sin(clock * 6.0), 0, TAU, 16, data.accent, 2.5, true)
	var sp: Vector2 = data.ship_map
	draw_circle(at + sp * s, 4.0, WHITE)


func _canvas_scale() -> Vector2:
	# unproject_position gives viewport pixels; the HUD draws on the stretched canvas.
	var vp := get_viewport().get_visible_rect().size
	return size / vp if vp.x > 0.0 else Vector2.ONE


func _draw_wheel(centre: Vector2) -> void:
	var playing: int = data.chord
	var wanted: int = data.wanted
	var radius := 86.0
	var accent: Color = data.accent
	var flash: float = data.chord_flash
	glow_circle(centre, radius, Color(accent, 0.45), 2.5)
	var dirs := [Vector2(0, -1), Vector2(1, 0), Vector2(0, 1), Vector2(-1, 0)]
	for s in 4:
		var name: String = data.chord_names[s]
		var c: Color = data.chord_colors[s]
		var at: Vector2 = centre + dirs[s] * radius
		if s == playing:
			draw_circle(at, 34.0 + 16.0 * flash, Color(c, 0.10 + 0.25 * flash))
			text(name, at + Vector2(0, -21), 36, c, 0.5)
		else:
			var blink := (0.5 + 0.5 * sin(clock * 18.0)) if s == wanted else 0.0
			text(name, at + Vector2(0, -15), 26, Color(c, 0.55 + 0.45 * blink), 0.5)
	glow_line(centre, centre + dirs[wanted] * radius * 0.55, Color(data.chord_colors[wanted], 0.85), 3.0)


func _draw_layers(bottom_left: Vector2) -> void:
	var accent: Color = data.accent
	var names: PackedStringArray = data.layer_names
	var layer: int = data.layer
	var count: int = data.max_layer
	var lx := bottom_left.x
	var ly := bottom_left.y - 27.0 * count
	for i in count + 1:
		var on := i <= layer
		var y := ly + 27.0 * i
		draw_line(Vector2(lx, y), Vector2(lx + 16, y), Color(accent, 1.0 if on else 0.25), 6.0 if on else 3.0)
		text(names[i], Vector2(lx + 28, y - 11), 19, Color(1, 1, 1, 0.95 if on else 0.4), 0.0, false)
	if layer < count and int(data.phase) == 0:
		text("NEXT IN %d BARS" % data.bars_to_layer, Vector2(lx, ly - 38), 18, Color(accent, 0.8), 0.0, false)

	var rates := ["ARP 1/8", "ARP 1/16", "ARP 1/32"]
	var rate: int = data.arp_rate
	text(rates[rate], Vector2(lx, ly - 132), 28, data.power_color if rate > 0 else Color(1, 1, 1, 0.6))
	if data.arp_direction != "":
		text(data.arp_direction, Vector2(lx, ly - 96), 19, Color(1, 1, 1, 0.7), 0.0, false)
	for i in data.rate_bars:
		draw_line(Vector2(lx + 14.0 * i, ly - 62), Vector2(lx + 8 + 14.0 * i, ly - 62), data.power_color, 4.0)


#==============================================================================
# Menu

func _draw_menu() -> void:
	if songs.is_empty():
		return
	var W := size.x
	var accent: Color = songs[selected].accent
	var pulse := 0.5 + 0.5 * sin(clock * 2.0)

	text("RISE OF THE", Vector2(W * 0.5, 60), 54, accent, 0.5)
	text("ARPEGGIATOR", Vector2(W * 0.5, 120), 130, accent, 0.5)
	text("ARPEGGIATOR", Vector2(W * 0.5, 120), 130, Color(1, 1, 1, 0.25 + 0.2 * pulse), 0.5, false)
	text("FOUR CHORDS  ·  ONE ARPEGGIO  ·  THREE TOWERS  ·  ONE GATE", Vector2(W * 0.5, 300), 26, Color(1, 1, 1, 0.75), 0.5)

	for i in 3:
		var s: Dictionary = songs[i]
		var on := i == selected
		var c: Color = Color(s.accent, 1.0 if on else 0.35)
		var cx := W * 0.5 + (i - 1) * 470.0
		var top := 390.0
		var bottom := 750.0
		var half := 205.0
		var box := PackedVector2Array([Vector2(cx - half, top), Vector2(cx + half, top), Vector2(cx + half, bottom), Vector2(cx - half, bottom)])
		if on:
			draw_rect(Rect2(cx - half, top, half * 2, bottom - top), Color(s.accent, 0.06 + 0.04 * pulse))
			glow_poly(box, c, 3.5)
		else:
			glow_poly(box, c, 2.0)
		text(str(i + 1), Vector2(cx, top + 24), 36, c, 0.5)
		text(s.name, Vector2(cx, top + 82), 50, WHITE if on else c, 0.5)
		text(s.tagline, Vector2(cx, top + 152), 22, c, 0.5)
		text("%d BPM" % s.bpm, Vector2(cx, top + 192), 20, Color(1, 1, 1, 0.8 if on else 0.35), 0.5, false)
		text("   ".join(s.chords), Vector2(cx, top + 232), 24, Color(1, 1, 1, 0.9 if on else 0.35), 0.5, false)
		text("BEST %d" % best[difficulty][i], Vector2(cx, top + 292), 20, c, 0.5, false)

	for d in 3:
		var sel := d == difficulty
		var x := W * 0.5 + (d - 1) * 230.0
		text(difficulty_names[d], Vector2(x, 778), 32 if sel else 26, accent if sel else Color(1, 1, 1, 0.4), 0.5)
		if sel:
			var w := text_width(difficulty_names[d], 32) * 0.5 + 16.0
			glow_line(Vector2(x - w, 826), Vector2(x + w, 826), accent, 3.0)

	var help := Color(1, 1, 1, 0.75)
	text("LEFT STICK  FLY + PICK CHORD     RIGHT STICK  SHOOT + PLAY ARPEGGIO", Vector2(W * 0.5, 856), 22, help, 0.5, false)
	text("RT LB RB  BOMB    LT  SLOW MOTION    START  PAUSE    UP DOWN  DIFFICULTY", Vector2(W * 0.5, 892), 22, help, 0.5, false)
	text("KEYBOARD  WASD + ARROWS OR MOUSE    SPACE BOMB    SHIFT SLOW MOTION", Vector2(W * 0.5, 928), 20, Color(1, 1, 1, 0.55), 0.5, false)
	text("A  PLAY" if has_pad else "ENTER  PLAY", Vector2(W * 0.5, 990), 36, Color(accent, 0.6 + 0.4 * pulse), 0.5)
