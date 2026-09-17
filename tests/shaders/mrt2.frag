#version 450
/* Two render targets, deliberately different colours. If the two attachments
 * aliased the same memory the two readbacks could not differ. */
layout(location = 0) out vec4 c0;
layout(location = 1) out vec4 c1;
void main() {
    c0 = vec4(1.0, 0.0, 0.0, 1.0);   /* red  -> RT0 */
    c1 = vec4(0.0, 0.0, 1.0, 1.0);   /* blue -> RT1 */
}
