// Analytic HSV -> RGB, hue in [0,1] (wraps), sat/val in [0,1]. Standard
// six-piece formulation (identical math to raylib's ColorFromHSV, just
// GPU-side so particle_render.vert can compute a per-particle palette tint
// from world position without a CPU round-trip -- see FogColorSettings'
// palette fields in EngineSettings.h and NeonFogVisualizer's PaletteParams
// wiring).
vec3 Hsv2Rgb(vec3 hsv) {
    float h = fract(hsv.x) * 6.0;
    float s = clamp(hsv.y, 0.0, 1.0);
    float v = clamp(hsv.z, 0.0, 1.0);

    float c = v * s;
    float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
    float m = v - c;

    vec3 rgb;
    if (h < 1.0) rgb = vec3(c, x, 0.0);
    else if (h < 2.0) rgb = vec3(x, c, 0.0);
    else if (h < 3.0) rgb = vec3(0.0, c, x);
    else if (h < 4.0) rgb = vec3(0.0, x, c);
    else if (h < 5.0) rgb = vec3(x, 0.0, c);
    else rgb = vec3(c, 0.0, x);
    return rgb + m;
}
