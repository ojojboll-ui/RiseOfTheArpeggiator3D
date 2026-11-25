// Lowpoly bodies with neon edges: every face is lit flat, and its outline
// glows a constant number of pixels wide wherever it is on screen, like the
// 2D game's lines. The outline comes from barycentric coordinates the mesh
// builder puts in UV and UV2.x (scripts/meshes.gd), with the diagonals that
// split a polygon into triangles pushed out of reach so only real edges glow.
//
// Instanced (MultiMesh) the colour is the instance's, alpha its spawn flicker
// and INSTANCE_CUSTOM.z the kick; a single mesh uses base_color and kick.
//
// How much of a body there is follows the band (look.gdshaderinc): with few
// layers only the outline is drawn, in one pale colour. `solid` keeps a body
// whole and in its own colour at every layer (the bullets: their colour is
// the chord).
shader_type spatial;
render_mode cull_back, diffuse_lambert, specular_schlick_ggx;

#include "res://shaders/look.gdshaderinc"

uniform bool solid = false;
uniform bool instanced = true;
uniform vec4 base_color : source_color = vec4(1.0);
uniform float kick = 0.0;
uniform float edge_px = 2.4;
uniform float edge_energy = 5.0;
uniform float body_energy = 0.35;
uniform float albedo_amount = 0.25;
uniform float kick_glow = 0.5;

uniform int role = 0;   // the new look: 0 a thing that moves, 1 a wall or rim, 2 a tower, door or gate

varying vec3 bary;
varying vec4 tint;
varying float pulse;
varying vec3 wpos;

void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	bary = vec3(UV, UV2.x);
	tint = instanced ? COLOR : base_color;
	pulse = instanced ? INSTANCE_CUSTOM.z : kick;
}

void fragment() {
	vec3 w = max(fwidth(bary), vec3(1e-5));
	vec3 a = smoothstep(vec3(0.0), w * edge_px, bary);
	float edge = 1.0 - min(min(a.x, a.y), a.z);
	float alpha = tint.a;
	float l = look_at(wpos);
	float band = sweep_band(wpos);
	if (old_look()) {
		float fill = solid ? 1.0 : look_fill(l);
		float lit = solid ? 1.0 : look_lit(l);
		vec3 c = solid ? tint.rgb : look_tint(tint.rgb, l);
		if (edge < 0.4 && band < 0.2 && bayer4(FRAGCOORD.xy) >= fill)
			discard;
		ALBEDO = c * albedo_amount * alpha * lit;
		ROUGHNESS = 0.4;
		METALLIC = 0.35;
		EMISSION = c * (edge * edge_energy * (1.0 + kick_glow * pulse) + body_energy * fill) * alpha
		         + mix(c, vec3(1.0), 0.3) * band * (0.15 + 3.0 * edge);   // the wavefront lights what it passes
	} else {
		vec3 c = tint.rgb;
		ALBEDO = c * max(albedo_amount, 0.18) * alpha;   // the ship's light shows every surface
		ROUGHNESS = 0.4;
		METALLIC = 0.35;
		float e;
		vec3 bulbs = vec3(0.0);
		if (role == 1) {
			// Walls: a night line, then neon edges, then whole and pulsing.
			float seed = floor(wpos.x * 0.6) * 7.0 + floor(wpos.z * 0.6);
			float edges = mix(0.05, 1.0, tube(l, 2.0, wpos, seed));
			float body = tube(l, 3.0, wpos, seed + 3.0) * (0.25 + 0.6 * pulse);
			e = (edge * edge_energy * edges * (1.0 + kick_glow * pulse) + body) * look_power;
		} else {
			// Things that move glow from the start, brighter with the bass.
			float boost = 1.0 + 0.7 * stage(l, 1.0);
			e = (edge * edge_energy * (1.0 + kick_glow * pulse) + body_energy) * (role == 0 ? boost : 1.0);
			if (role == 2) {
				// The tivoli: strings of bulbs chasing round towers, doors and gate.
				float row = wpos.y * 3.0;
				float bulb = smoothstep(0.75, 0.95, 1.0 - abs(fract(row) - 0.5) * 2.0)
				           * step(0.5, fract(atan(wpos.z - MODEL_MATRIX[3].z, wpos.x - MODEL_MATRIX[3].x) * 2.5 + TIME * 1.5 + floor(row) * 0.5));
				bulbs = hue(floor(row) * 0.17 + TIME * 0.2) * bulb * 3.0 * stage(l, 8.0) * look_power * alpha;
			}
		}
		EMISSION = bulbs + c * e * alpha + mix(c, vec3(1.0), 0.3) * band * (0.1 + 2.0 * edge);
	}
}
