// The floor: the world's wave sheet (Grid) as heights, a dark lacquered
// surface that catches the light on the waves, and the Geometry Wars grid
// drawn on it in neon — every fourth line brighter, all of it pumping with
// the kick and flaring where the floor is disturbed.
shader_type spatial;
render_mode cull_back, diffuse_lambert, specular_schlick_ggx;

uniform sampler2D heights : filter_linear, repeat_disable;
uniform vec2 dims = vec2(47.0, 25.0);     // grid points across and down
uniform vec2 field = vec2(18.6, 9.66);    // metres
uniform vec3 tint : source_color = vec3(0.22, 0.36, 1.0);
uniform float kick = 0.0;
uniform float line_energy = 1.6;
uniform float line_px = 1.3;

varying vec2 cell;
varying float h;

float height_at(vec2 c) {
	return texture(heights, (c + 0.5) / dims).r;
}

void vertex() {
	cell = (VERTEX.xz + field * 0.5) / field * (dims - 1.0);
	h = height_at(cell);
	VERTEX.y += h;
	vec2 spacing = field / (dims - 1.0);
	float hx = height_at(cell + vec2(0.3, 0.0)) - height_at(cell - vec2(0.3, 0.0));
	float hz = height_at(cell + vec2(0.0, 0.3)) - height_at(cell - vec2(0.0, 0.3));
	NORMAL = normalize(vec3(-hx / (spacing.x * 0.6), 1.0, -hz / (spacing.y * 0.6)));
}

void fragment() {
	vec2 g = abs(fract(cell - 0.5) - 0.5) / max(fwidth(cell), vec2(1e-5));
	float minor = 1.0 - clamp(min(g.x, g.y) / line_px, 0.0, 1.0);
	vec2 cm = cell / 4.0;
	vec2 gm = abs(fract(cm - 0.5) - 0.5) / max(fwidth(cm), vec2(1e-5));
	float major = 1.0 - clamp(min(gm.x, gm.y) / (line_px * 1.6), 0.0, 1.0);

	float stir = clamp(abs(h) * 7.0, 0.0, 1.0);
	float base = 0.10 + 0.16 * kick;
	float line = max(minor * base * 0.55, major * base * 1.6) + max(minor, major) * stir * 0.6;

	ALBEDO = vec3(0.010, 0.012, 0.026);
	ROUGHNESS = 0.3;
	METALLIC = 0.6;
	EMISSION = tint * line * line_energy;
}
