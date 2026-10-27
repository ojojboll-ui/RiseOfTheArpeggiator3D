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

varying vec3 bary;
varying vec4 tint;
varying float pulse;

void vertex() {
	bary = vec3(UV, UV2.x);
	tint = instanced ? COLOR : base_color;
	pulse = instanced ? INSTANCE_CUSTOM.z : kick;
}

void fragment() {
	vec3 w = max(fwidth(bary), vec3(1e-5));
	vec3 a = smoothstep(vec3(0.0), w * edge_px, bary);
	float edge = 1.0 - min(min(a.x, a.y), a.z);
	float alpha = tint.a;
	float fill = solid ? 1.0 : look_fill();
	float lit = solid ? 1.0 : look_lit();
	vec3 c = solid ? tint.rgb : look_tint(tint.rgb);
	if (edge < 0.4 && bayer4(FRAGCOORD.xy) >= fill)
		discard;
	ALBEDO = c * albedo_amount * alpha * lit;
	ROUGHNESS = 0.4;
	METALLIC = 0.35;
	EMISSION = c * (edge * edge_energy * (1.0 + kick_glow * pulse) + body_energy * fill) * alpha;
}
