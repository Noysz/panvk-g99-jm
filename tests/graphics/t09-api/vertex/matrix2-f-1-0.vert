#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
layout(location=0) in f16vec4 attr;
layout(set=0,binding=0,std430) buffer Output { uvec4 values[]; } result;
void main() { result.values[gl_VertexIndex+1]=floatBitsToUint(vec4(attr)); gl_Position=vec4(0,0,0,1); gl_PointSize=1.0; }
