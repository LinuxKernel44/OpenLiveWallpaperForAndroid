#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 4) in vec4 m0;
layout(location = 5) in vec4 m1;
layout(location = 6) in vec4 m2;
layout(location = 7) in vec4 m3;
void main() {
    mat4 model = mat4(m0, m1, m2, m3);
    gl_Position = F.lightVP * (model * vec4(aPos, 1.0));
}
