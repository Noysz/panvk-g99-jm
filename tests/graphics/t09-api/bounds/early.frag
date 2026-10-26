#version 450
layout(early_fragment_tests) in;
layout(location=0) flat in vec4 vColor;
layout(location=0) out vec4 color;
void main() { color=vColor; }
