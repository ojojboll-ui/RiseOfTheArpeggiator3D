// Sparks: a streak along the spark's velocity, turned to face the camera,
// added on top of everything. The instance's basis is identity, its origin
// the spark; INSTANCE_CUSTOM is the velocity in metres a second and the
// width. The quad runs from VERTEX.x 0 (head) to 1 (tail), y -0.5..0.5.
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled;

#include "res://shaders/look.gdshaderinc"

uniform float energy = 3.0;

varying vec4 col;

void vertex() {
	col = COLOR;
	vec3 v = INSTANCE_CUSTOM.xyz;
	float speed = length(v);
	vec3 dir = speed > 1e-4 ? v / speed : vec3(1.0, 0.0, 0.0);
	float width = INSTANCE_CUSTOM.w;
	float len = max(min(speed * 0.03, 0.4), width);
	vec3 to_cam = normalize(INV_VIEW_MATRIX[3].xyz - MODEL_MATRIX[3].xyz);
	vec3 side = cross(dir, to_cam);
	side = length(side) > 1e-4 ? normalize(side) : vec3(0.0, 0.0, 1.0);
	VERTEX = -dir * len * VERTEX.x + side * width * 2.2 * VERTEX.y;   // wider than the 2D line: seen from afar
}

void fragment() {
	// Across the streak, from the UV the quad carries: brightest down the middle.
	float across = 1.0 - abs(UV.y * 2.0 - 1.0);
	ALBEDO = look_tint(col.rgb) * energy;
	ALPHA = col.a * smoothstep(0.0, 0.6, across);
}
