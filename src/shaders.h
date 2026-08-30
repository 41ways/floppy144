/* shaders.h - GLSL kept as source strings.
 * The driver compiles these at startup, so a whole material system costs
 * a few kilobytes of text instead of megabytes of baked textures. */
#ifndef SHADERS_H
#define SHADERS_H

/* Each point knows one thing beyond its position: the moment the expanding
   wavefront reaches it. Everything the player sees falls out of that -
   a bright shell racing outwards, and a dim permanent deposit behind it. */
static const char *POINT_VS =
"#version 330 core\n"
"layout(location = 0) in vec3  aPos;\n"
"layout(location = 1) in float aReveal;\n"
"layout(location = 2) in float aGain;\n"
"uniform mat4  uVP;\n"
"uniform vec3  uCam;\n"
"uniform float uTime;\n"
"uniform float uPersist;\n"
"uniform float uFlat;\n"
"uniform float uBase;\n"
"out float vBright;\n"
"out float vDist;\n"
"void main(){\n"
"  vec4 clip = uVP * vec4(aPos, 1.0);\n"
/* the readout is made of the same points as the cave, only pinned to
   the screen instead of standing in it */
"  clip = mix(clip, vec4(aPos.xy, 0.0, 1.0), uFlat);\n"
"  gl_Position = clip;\n"
"  float d = distance(aPos, uCam);\n"
"  vDist = mix(d, 3.0, uFlat);\n"
"  float age = uTime - aReveal;\n"
/* the wavefront itself: a thin bright shell */
"  float front  = exp(-pow(age / 0.13, 2.0));\n"
/* What it leaves behind. Only surfaces get this: the wave crossing open
   air is visible as it passes and then is simply gone, so the map that
   accumulates is walls and nothing else. */
"  float memory = (age > 0.0 ? 0.17 : 0.0) * uPersist;\n"
/* a metre of free sight so the player never walks off a ledge blind */
"  float close  = exp(-pow(d / 1.45, 2.0)) * 0.22 * uPersist;\n"
/* uBase holds a thing lit whatever the wavefront is doing, so the title
   swells as the sweep crosses it and settles back rather than going
   dark and coming on again. */
"  float lit = max(max(max(front, memory), close), uBase);\n"
"  vBright = mix(lit, 1.0, uFlat) * aGain;\n"
"  gl_PointSize = mix(clamp(190.0 / max(clip.w, 0.25), 1.7, 4.6), 2.0, uFlat);\n"
"}\n";

static const char *POINT_FS =
"#version 330 core\n"
"in float vBright;\n"
"in float vDist;\n"
"uniform float uMonster;\n"
"uniform float uFlat;\n"
"uniform float uInk;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"  if (vBright < 0.02) discard;\n"
/* round points read as returns; square ones read as pixels */
"  vec2 q = gl_PointCoord - 0.5;\n"
"  if (dot(q, q) > 0.25) discard;\n"
/* lidar palette: near returns warm, far returns cold */
"  float t = clamp(1.0 - vDist / 26.0, 0.0, 1.0);\n"
"  vec3  c = mix(vec3(0.13, 0.20, 0.90), vec3(1.00, 0.62, 0.16), t * t);\n"
/* the thing does not scatter like rock - it comes back red, and brighter */
"  c = mix(c, vec3(1.00, 0.13, 0.07), uMonster);\n"
"  float a = vBright * exp(-vDist * 0.055);\n"
/* the rock under your nose is already known - let the distance read */
"  a *= mix(0.35 + 0.65 * smoothstep(0.5, 4.0, vDist), 1.0, uFlat);\n"
"  a = mix(a, min(a * 2.2, 1.0), uMonster);\n"
/* Waking floods the screen white, and light added to white is still
   white - so that one pass writes dark ink over it instead. */
"  if (uInk > 0.5) { FragColor = vec4(0.04, 0.05, 0.08, a); }\n"
"  else            { FragColor = vec4(c * a, 1.0); }\n"
"}\n";

#endif /* SHADERS_H */
