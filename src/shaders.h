/* shaders.h — GLSL kept as source strings.
 * The driver compiles these at startup, so a whole material system costs
 * a few kilobytes of text instead of megabytes of baked textures. */
#ifndef SHADERS_H
#define SHADERS_H

static const char *VS_SRC =
"#version 330 core\n"
/* Fullscreen triangle with no vertex buffer at all: three points derived
   from gl_VertexID cover the viewport. Costs zero bytes of geometry. */
"void main(){\n"
"  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
"  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
"}\n";

static const char *FS_SRC =
"#version 330 core\n"
"out vec4 FragColor;\n"
"uniform vec2  uRes;\n"
"uniform float uTime;\n"
"void main(){\n"
"  vec2 uv = (gl_FragCoord.xy - 0.5*uRes) / uRes.y;\n"
"  vec3 ro = vec3(0.0, 1.25, 0.0);\n"
"  vec3 rd = normalize(vec3(uv.x, uv.y - 0.17, -1.35));\n"
"  vec3 col;\n"
"  if (rd.y < -0.0015) {\n"
"    float t = -ro.y / rd.y;\n"
"    vec3 p = ro + rd * t;\n"
"    vec2 d = vec2(p.x, p.z + 19.0);\n"   /* disk hub sits ahead of the camera */
"    float r = length(d);\n"
"    float a = atan(d.y, d.x) + uTime * 0.75;\n"
"    float rw = fwidth(r) * 1.3 + 1e-4;\n"
"    float ring = abs(fract(r * 1.55) - 0.5);\n"
"    float tracks = 1.0 - smoothstep(0.0, rw * 1.55, ring - 0.03);\n"
       /* sin(6a) is continuous across atan's branch cut, so no seam */
"    float spokes = pow(abs(sin(a * 6.0)), 90.0);\n"
"    float hl = 1.0 - smoothstep(0.0, 0.10, abs(r - 7.2));\n"
"    float fog = exp(-t * 0.042);\n"
"    col  = vec3(0.052, 0.070, 0.080);\n"
"    col += vec3(0.42, 0.55, 0.60) * tracks * 0.50;\n"
"    col += vec3(0.42, 0.55, 0.60) * spokes * 0.16;\n"
"    col += vec3(1.00, 0.62, 0.11) * hl * 0.85;\n"
"    col *= fog;\n"
"  } else {\n"
"    col = mix(vec3(0.090, 0.055, 0.020), vec3(0.018, 0.022, 0.028),\n"
"              smoothstep(0.0, 0.34, max(rd.y, 0.0)));\n"
"  }\n"
"  col += vec3(1.0, 0.62, 0.11) * 0.10 * exp(-abs(rd.y) * 24.0);\n"
"  vec2 q = gl_FragCoord.xy / uRes;\n"
"  col *= 0.34 + 0.66 * pow(16.0*q.x*q.y*(1.0-q.x)*(1.0-q.y), 0.25);\n"
"  FragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(0.4545)), 1.0);\n"
"}\n";

#endif /* SHADERS_H */
