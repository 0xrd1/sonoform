#version 430
in vec2 fragTexCoord;
uniform sampler2D texture0;   // base scene (sampled with the flip already baked into fragTexCoord by the outer draw call)
uniform sampler2D uBloomTex;  // blurred bright-pass, still in raw render-texture (bottom-up) orientation
uniform float uBloomIntensity;
out vec4 finalColor;

void main() {
    vec3 base = texture(texture0, fragTexCoord).rgb;

    // texture0's sampling is already Y-flip-corrected by the outer
    // DrawTexturePro call (negative source height), but uBloomTex was
    // never drawn through that same flip -- it's a render texture sampled
    // directly here, so it needs its own V flip to align with texture0.
    vec2 bloomUv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec3 bloom = texture(uBloomTex, bloomUv).rgb;

    finalColor = vec4(base + bloom * uBloomIntensity, 1.0);
}
