#version 430
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float uThreshold;
out vec4 finalColor;

void main() {
    vec3 color = texture(texture0, fragTexCoord).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Soft-knee: fades in over [threshold, threshold*2] instead of a hard
    // cutoff, which would make the bloom flicker on/off as brightness
    // crosses the line.
    float contribution = clamp((luminance - uThreshold) / max(uThreshold, 0.0001), 0.0, 1.0);
    finalColor = vec4(color * contribution, 1.0);
}
