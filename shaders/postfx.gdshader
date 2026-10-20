// The 3D view on screen, with slow motion's look under the HUD: seven copies
// scaled up to +7% round the ship (radial blur), each split into red, green
// and blue scaled 0.8% apart (chromatic aberration) — the 2D game's
// composite, as one pass. `scene` is the 3D view with its trails.
shader_type canvas_item;

uniform sampler2D scene : filter_linear;
uniform vec2 centre = vec2(0.5);
uniform float amount = 0.0;

void fragment() {
	vec2 uv = UV;
	if (amount < 0.001) {
		COLOR = vec4(texture(scene, uv).rgb, 1.0);
	} else {
		vec3 sum = vec3(0.0);
		float weight = 0.0;
		for (int i = 0; i < 7; i++) {
			float k = float(i) / 6.0;
			float s = 1.0 / (1.0 + 0.07 * amount * k);
			vec2 p = centre + (uv - centre) * s;
			float split = 0.008 * amount;
			vec3 c;
			c.r = texture(scene, centre + (p - centre) * (1.0 + split)).r;
			c.g = texture(scene, p).g;
			c.b = texture(scene, centre + (p - centre) * (1.0 - split)).b;
			float w = 1.0 - 0.6 * k;
			sum += c * w;
			weight += w;
		}
		COLOR = vec4(sum / weight, 1.0);
	}
}
