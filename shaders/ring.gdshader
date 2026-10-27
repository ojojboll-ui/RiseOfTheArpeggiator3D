// Shock rings lying on the floor. The mesh is a unit annulus whose UV.x is 0
// on the inner edge and 1 on the outer; the radius and thickness come from
// INSTANCE_CUSTOM.xy, so a growing ring keeps its line width.
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled;

#include "res://shaders/look.gdshaderinc"

uniform float energy = 3.0;

varying vec4 col;
varying float here;

void vertex() {
	col = COLOR;
	here = look_at(MODEL_MATRIX[3].xyz);
	vec2 d = normalize(VERTEX.xz);
	float rad = INSTANCE_CUSTOM.x + (UV.x - 0.5) * INSTANCE_CUSTOM.y;
	VERTEX = vec3(d.x * rad, 0.0, d.y * rad);
}

void fragment() {
	float across = 1.0 - abs(UV.x * 2.0 - 1.0);
	ALBEDO = look_tint(col.rgb, here) * energy;
	ALPHA = col.a * across;
}
