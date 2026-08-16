#version 430
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 uTexelStep; // (1/width, 0) for the horizontal pass, (0, 1/height) for vertical
out vec4 finalColor;

void main() {
    // 9-tap separable Gaussian (5 unique weights, mirrored). Two passes
    // (horizontal then vertical) approximate a full 2D Gaussian blur at a
    // fraction of the cost of a genuine NxN kernel.
    float weights[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

    vec3 result = texture(texture0, fragTexCoord).rgb * weights[0];
    for (int i = 1; i < 5; i++) {
        vec2 offset = uTexelStep * float(i);
        result += texture(texture0, fragTexCoord + offset).rgb * weights[i];
        result += texture(texture0, fragTexCoord - offset).rgb * weights[i];
    }
    finalColor = vec4(result, 1.0);
}
