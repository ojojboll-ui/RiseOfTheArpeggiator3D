// The look grows with the band (2026-10-27, "helt i wire frame som ett gammalt
// arkadspel ... ju fler musiklager desto färggrannare, ljus, skuggor, effekter
// och yta. Lite som karaktären i REZ").
//
// The look is the band's layer count, 0 (the kick alone) to 8. With the kick
// alone everything is a vector wireframe in one pale phosphor colour on black;
// colour comes back over the first layers, surfaces fade in through an
// ordered dither from the drone on, light from the open hat on (shadows follow
// in main.gd), and the last layers bring full glow. A death strips a layer,
// and the picture loses it with the sound.
//
// A new layer does not fade in everywhere at once: it sweeps out from the ship
// as a bright wavefront, starting on the downbeat the layer comes in on
// (2026-10-27, "en tydlig effekt som utgår ifrån spelaren och sveper över
// skärmen"). Inside the front is look_to, outside look_from.

global uniform float look_from;
global uniform float look_to;
global uniform vec3 sweep_origin;
global uniform float sweep_radius;     // metres; large once the sweep is over

const vec3 VECTOR_TINT = vec3(0.62, 0.92, 1.0);
const float SWEEP_EDGE = 1.0;          // metres the old look and the new blend over
const float SWEEP_BAND = 1.1;          // the wavefront's glow, half-width in metres
const float SWEEP_CORE = 0.18;         // its bright line

float sweep_distance(vec3 world) {
	return distance(world.xz, sweep_origin.xz);
}

// The look at a point on the level.
float look_at(vec3 world) {
	float d = sweep_distance(world);
	return mix(look_to, look_from, smoothstep(sweep_radius - SWEEP_EDGE, sweep_radius, d));
}

// The wavefront: a soft glow (sweep_band) round a sharp bright line
// (sweep_core); 1 on it, 0 away from it and when no sweep runs.
float sweep_band(vec3 world) {
	if (look_from == look_to)
		return 0.0;
	float x = (sweep_distance(world) - sweep_radius) / SWEEP_BAND;
	return exp(-x * x);
}

float sweep_core(vec3 world) {
	if (look_from == look_to)
		return 0.0;
	float x = (sweep_distance(world) - sweep_radius) / SWEEP_CORE;
	return exp(-x * x);
}

float look_colour(float l) { return clamp(l / 4.0, 0.0, 1.0); }
float look_fill(float l)   { return clamp((l - 3.0) / 3.0, 0.0, 1.0); }
float look_lit(float l)    { return clamp((l - 4.0) / 2.0, 0.0, 1.0); }

// One pale colour for everything at first, the real colours as layers come in.
vec3 look_tint(vec3 c, float l) {
	return mix(VECTOR_TINT, c, look_colour(l));
}

// A 4x4 ordered dither: surfaces appear in a screen-door pattern, the way old
// hardware faked transparency.
float bayer4(vec2 p) {
	const float m[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
	ivec2 q = ivec2(mod(p, 4.0));
	return (m[q.x + q.y * 4] + 0.5) / 16.0;
}
