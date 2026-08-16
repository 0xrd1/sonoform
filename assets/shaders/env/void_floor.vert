#version 430

// Standard raylib vertex attribute + the model/mvp matrices raylib
// auto-feeds for any DrawMesh call using this shader (see VoidFloor::Draw,
// which builds planeMesh_'s transform explicitly and passes it through
// DrawMesh -- the robust, VBO-backed path for a custom-shaded static mesh,
// as opposed to DrawPlane's immediate-mode path).
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;

out vec3 vWorldPos;

void main() {
    vWorldPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
