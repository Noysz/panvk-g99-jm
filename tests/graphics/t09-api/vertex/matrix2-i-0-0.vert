#version 450
layout(location=0) in ivec4 attr;
layout(set=0,binding=0,std430) buffer Output { uvec4 values[]; } result;
void main() { result.values[gl_VertexIndex+1]=uvec4(attr); gl_Position=vec4(0,0,0,1); gl_PointSize=1.0; }
