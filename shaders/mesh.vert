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
    uvec4 joints;
    vec4 weights;
};

layout (buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout (buffer_reference, std430) readonly buffer SkinBuffer {
    mat4 bones[];
};
layout (push_constant) uniform constants {
    mat4 camera_matrix;
    VertexBuffer vertexBuffer;
    SkinBuffer uSkin;
} PushConstants;

void main() {
    Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    vec4 position = vec4(v.position, 1.0f);
    uvec4 inJoints = v.joints;
    vec4 inWeights = v.weights;
    SkinBuffer uSkin = PushConstants.uSkin;
    mat4 skinMatrix =
        uSkin.bones[inJoints.x] * inWeights.x +
        uSkin.bones[inJoints.y] * inWeights.y +
        uSkin.bones[inJoints.z] * inWeights.z +
        uSkin.bones[inJoints.w] * inWeights.w;

	gl_Position =  sceneData.viewproj * position * skinMatrix;
    
    outNormal = (PushConstants.camera_matrix * vec4(v.normal, 0.f)).xyz;
    outColor = materialData.colorFactors.xyz;
    outUV.x = v.uv_x;
    outUV.y = v.uv_y;
}