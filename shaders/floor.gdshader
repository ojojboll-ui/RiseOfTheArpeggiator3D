// The floor of a level: its hills, its waves, its zones.
//
// Heights are the level's hills (a coarse grid cut into triangles the same way
// Level::terrain cuts them, so the game and the picture agree) plus the
// world's wave sheet (Grid). Shading is flat per facet, so the hills read as
// lowpoly. On it the Geometry Wars grid in each zone's colour — every fourth
// line brighter, pumping with the kick, flaring where the floor is disturbed.
// Outside the rooms and passages there is no floor at all: the rooms float in
// the dark.
shader_type spatial;
render_mode cull_back, diffuse_lambert, specular_schlick_ggx;

// With few layers the floor is its grid alone, a vector landscape; its
// surface, colour and light come in with the band (look.gdshaderinc).
#include "res://shaders/look.gdshaderinc"

uniform sampler2D waves : filter_linear, repeat_disable;
uniform vec2 wave_dims = vec2(121.0, 111.0);   // points across and down
uniform float wave_cell = 0.4;                 // metres between them
uniform sampler2D hills : filter_nearest, repeat_disable;
uniform vec2 hill_dims = vec2(61.0, 56.0);
uniform float hill_cell = 0.8;
uniform sampler2D info : filter_linear, repeat_disable;   // rgb zone colour, a open floor
uniform vec2 info_dims = vec2(240.0, 220.0);
uniform float info_cell = 0.2;

uniform float kick = 0.0;
uniform float line_energy = 1.6;
uniform float line_px = 1.3;
uniform float grid_cell = 0.4;
uniform sampler2D paint : filter_linear, repeat_disable;   // rgb light shots left, at the wave grid
uniform float beat = 0.0;
uniform float beat_phase = 0.0;   // 0 on the beat, 1 at the next
uniform vec4 strum_rings[4];      // x, z where a strum ring started, its age in seconds (negative: none)
uniform vec4 strum_colors[4] : source_color;
uniform vec3 ship = vec3(0.0);

varying vec3 world;
varying float wave;

float hill_at(vec2 p) {
	vec2 f = clamp(p / hill_cell, vec2(0.0), hill_dims - 1.001);
	ivec2 c = ivec2(floor(f));
	vec2 t = f - vec2(c);
	float h00 = texelFetch(hills, c, 0).r;
	float h10 = texelFetch(hills, c + ivec2(1, 0), 0).r;
	float h01 = texelFetch(hills, c + ivec2(0, 1), 0).r;
	float h11 = texelFetch(hills, c + ivec2(1, 1), 0).r;
	if (t.x >= t.y)
		return h00 + t.x * (h10 - h00) + t.y * (h11 - h10);
	return h00 + t.y * (h01 - h00) + t.x * (h11 - h01);
}

float wave_at(vec2 p) {
	return texture(waves, (p / wave_cell + 0.5) / wave_dims).r;
}

void vertex() {
	world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	wave = wave_at(world.xz);
	float h = hill_at(world.xz) + wave;
	VERTEX.y += h;
	world.y += h;
}

void fragment() {
	vec4 zone = texture(info, world.xz / (info_cell * info_dims));
	if (zone.a < 0.5)
		discard;

	// Flat per facet: the normal from how the surface moves across the pixel.
	vec3 n = normalize(cross(dFdx(VERTEX), dFdy(VERTEX)));
	if (dot(n, -VERTEX) < 0.0)
		n = -n;
	NORMAL = n;

	vec2 cell = world.xz / grid_cell;
	vec2 g = abs(fract(cell - 0.5) - 0.5) / max(fwidth(cell), vec2(1e-5));
	float minor = 1.0 - clamp(min(g.x, g.y) / line_px, 0.0, 1.0);
	vec2 cm = cell / 4.0;
	vec2 gm = abs(fract(cm - 0.5) - 0.5) / max(fwidth(cm), vec2(1e-5));
	float major = 1.0 - clamp(min(gm.x, gm.y) / (line_px * 1.6), 0.0, 1.0);

	float stir = clamp(abs(wave) * 7.0, 0.0, 1.0);
	float base = 0.10 + 0.16 * kick;
	float line = max(minor * base * 0.55, major * base * 1.6) + max(minor, major) * stir * 0.6;

	float l = look_at(world);
	float band = sweep_band(world);
	if (old_look()) {
		float fill = look_fill(l);
		if (line < 0.015 && band < 0.1 && bayer4(FRAGCOORD.xy) >= fill)
			discard;

		// Lit enough that the facets of the hills catch the light differently.
		ALBEDO = mix(vec3(0.012, 0.014, 0.03), zone.rgb * 0.16, 0.55) * look_lit(l);
		ROUGHNESS = 0.6;
		METALLIC = 0.2;
		// Brighter lines while they are all there is; the sweep's wavefront a
		// bright ring running over the floor, brightest on the lines.
		vec3 lc = look_tint(zone.rgb, l);
		EMISSION = lc * line * line_energy * (1.0 + 0.4 * (1.0 - fill))
		         + look_tint(zone.rgb, look_to) * (band * (0.08 + 2.2 * max(minor, major)) + sweep_core(world) * 3.0);
	} else {
		// Dark ground the ship's light shows; the floor's own light comes late.
		ALBEDO = mix(vec3(0.04, 0.045, 0.06), zone.rgb * 0.15, 0.3);
		ROUGHNESS = 0.6;
		METALLIC = 0.2;
		vec3 grid_c = mix(vec3(0.55, 0.6, 0.7), zone.rgb, stage(l, 5.0));
		float lines = line * (0.45 * stage(l, 4.0) + 0.55 * stage(l, 5.0));
		vec3 e = grid_c * lines * line_energy;
		e += zone.rgb * 0.012 * stage(l, 5.0);
		// The dance floor: big cells light at random, a new set each beat,
		// each glowing on and fading out over the next few beats.
		float dance = 0.0;
		for (int k = 0; k < 4; k++) {
			float b = beat - float(k);
			float on = step(0.8, look_hash(floor(cm) + vec2(b * 3.1, b * 1.7)));
			dance += on * exp(-(float(k) + beat_phase) * 0.9);
		}
		e += zone.rgb * dance * (0.14 + 0.12 * kick) * stage(l, 6.0);
		// Light the shots leave behind.
		vec4 p = texture(paint, (world.xz / wave_cell + 0.5) / wave_dims);
		e += p.rgb * 0.55 * stage(l, 7.0);
		// The strum: a glowing ring runs out from the ship on every chord.
		for (int k = 0; k < 4; k++) {
			float age = strum_rings[k].z;
			if (age < 0.0 || age > 3.0) continue;
			float d = distance(world.xz, strum_rings[k].xy) - age * 8.0;
			float ring = exp(-d * d / 0.18) * exp(-age * 1.1);
			e += strum_colors[k].rgb * ring * (0.5 + 1.2 * max(minor, major)) * stage(l, 8.0);
		}
		EMISSION = e * look_power
		         + zone.rgb * (band * (0.06 + 1.6 * max(minor, major)) + sweep_core(world) * 2.0);
	}
}
