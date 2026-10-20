// Slow motion's trails (2026-10-20), as the 2D game made them: last frame's
// picture, faded, kept wherever it is brighter than this frame's — a MAX,
// so trails never pile up into white — with a small floor taken off every
// frame so faint remains do not linger as a haze.
//
// In 3D the camera moves all the time, so a trail of everything smeared the
// whole lit floor; only what is bright — sparks, shots, enemies, the ship —
// leaves a trail, the grid and the walls do not.
//
// Drawn into a viewport that is never cleared, so the screen texture is the
// trail as it stood after the previous frame.
shader_type canvas_item;
render_mode blend_disabled;

uniform sampler2D current : filter_linear;                   // the 3D view this frame
uniform sampler2D previous : hint_screen_texture, filter_linear;
uniform float keep = 0.0;          // how much of the old trail stays this frame; 0 = no trail
uniform float floor_cut = 0.02;
uniform float bright = 0.42;       // below this luminance nothing trails

void fragment() {
	vec3 cur = texture(current, UV).rgb;
	vec3 prev = texture(previous, SCREEN_UV).rgb;
	float luma = dot(prev, vec3(0.299, 0.587, 0.114));
	vec3 trail = max(prev * keep * smoothstep(bright, bright + 0.2, luma) - floor_cut, vec3(0.0));
	COLOR = vec4(max(cur, trail), 1.0);
}
