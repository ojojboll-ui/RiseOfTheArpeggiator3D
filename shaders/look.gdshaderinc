// The look grows with the band (2026-10-27, "helt i wire frame som ett gammalt
// arkadspel ... ju fler musiklager desto färggrannare, ljus, skuggor, effekter
// och yta. Lite som karaktären i REZ").
//
// `look` is the band's layer count, 0 (the kick alone) to 8, eased by
// main.gd. With the kick alone everything is a vector wireframe in one pale
// phosphor colour on black; colour comes back over the first layers, surfaces
// fade in through an ordered dither from the drone on, light from the open
// hat on (shadows follow in main.gd), and the last layers bring full glow.
// A death strips a layer, and the picture loses it with the sound.

global uniform float look;

const vec3 VECTOR_TINT = vec3(0.62, 0.92, 1.0);

float look_colour() { return clamp(look / 4.0, 0.0, 1.0); }
float look_fill()   { return clamp((look - 3.0) / 3.0, 0.0, 1.0); }
float look_lit()    { return clamp((look - 4.0) / 2.0, 0.0, 1.0); }

// One pale colour for everything at first, the real colours as layers come in.
vec3 look_tint(vec3 c) {
	return mix(VECTOR_TINT, c, look_colour());
}

// A 4x4 ordered dither: surfaces appear in a screen-door pattern, the way old
// hardware faked transparency.
float bayer4(vec2 p) {
	const float m[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
	ivec2 q = ivec2(mod(p, 4.0));
	return (m[q.x + q.y * 4] + 0.5) / 16.0;
}
