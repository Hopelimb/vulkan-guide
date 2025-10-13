#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_structures.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;

struct Vertex{
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 joints;
    vec4 weights;
};

layout (buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout (buffer_reference, std430) readonly buffer JointMatrixBuffer {
    mat4 jointMatrices[];
};
layout (push_constant) uniform constants {
    mat4 camera_matrix;
    VertexBuffer vertexBuffer;
    JointMatrixBuffer jointMatrixBuffer;
} PushConstants;

void main() {
    Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    vec4 position = vec4(v.position, 1.0f);

	gl_Position =  sceneData.viewproj * PushConstants.camera_matrix * position;
    
    outNormal = (PushConstants.camera_matrix * vec4(v.normal, 0.f)).xyz;
    outColor = materialData.colorFactors.xyz;
    outUV.x = v.uv_x;
    outUV.y = v.uv_y;
}