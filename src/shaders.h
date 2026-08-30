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
"uniform float uFade;\n"
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
"  float memory = (age > 0.0 ? 0.34 : 0.0) * uPersist;\n"
/* a metre of free sight so the player never walks off a ledge blind */
"  float close  = exp(-pow(d / 1.45, 2.0)) * 0.22 * uPersist;\n"
/* uBase holds a thing lit whatever the wavefront is doing, so the title
   swells as the sweep crosses it and settles back rather than going
   dark and coming on again. */
"  float lit = max(max(max(front, memory), close), uBase);\n"
"  vBright = mix(lit, 1.0, uFlat) * aGain * uFade;\n"
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
"  a *= mix(0.52 + 0.48 * smoothstep(0.5, 4.0, vDist), 1.0, uFlat);\n"
"  a = mix(a, min(a * 2.2, 1.0), uMonster);\n"
/* Waking floods the screen white, and light added to white is still
   white - so that one pass writes dark ink over it instead. */
"  if (uInk > 0.5) { FragColor = vec4(0.04, 0.05, 0.08, a); }\n"
"  else            { FragColor = vec4(c * a, 1.0); }\n"
"}\n";


/* The ending is the one place this game draws a surface instead of a point.
 * The room is a handful of boxes marched per pixel, lit by the panel over the
 * bed and the window beside it - and then most of it is thrown away again by
 * an eyelid that is only just opening. */
static const char *WAKE_VS =
"#version 330 core\n"
"void main(){\n"
"  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
"  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
"}\n";

static const char *WAKE_FS =
"#version 330 core\n"
"out vec4 FragColor;\n"
"uniform vec2  uRes;\n"
"uniform float uTime;\n"
"uniform float uOpen;    // eyelid, 0 shut to 1 wide\n"
"uniform float uBright;  // how much light has come back\n"
"uniform float uSharp;   // how much of it is holding still\n"
"\n"
"float box(vec3 p, vec3 b){ vec3 q = abs(p) - b;\n"
"  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }\n"
"float cyl(vec3 p, float r, float h){\n"
"  vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);\n"
"  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); }\n"
"\n"
"float smin(float a, float b, float k){\n"
"  float h = clamp(0.5 + 0.5*(b-a)/k, 0.0, 1.0);\n"
"  return mix(b, a, h) - k*h*(1.0-h); }\n"
"float cap(vec3 p, vec3 a, vec3 b, float r){\n"
"  vec3 pa = p-a, ba = b-a;\n"
"  float h = clamp(dot(pa,ba)/dot(ba,ba), 0.0, 1.0);\n"
"  return length(pa - ba*h) - r; }\n"

/* A sphere on a box reads as a ball on a pillar, which is what it was. What
   makes a shape read as a person is the run from shoulder to neck to head and
   the arms hanging beside it - so it is built as limbs and fused with a
   smooth minimum into one body. Seen from a bed it stands over you. */
"float person(vec3 p){\n"
"  float d = cap(p, vec3(-0.10,-0.25,0.0), vec3(-0.06,0.62,0.0), 0.115);\n"
"  d = min(d, cap(p, vec3(0.10,-0.25,0.0), vec3(0.06,0.62,0.0), 0.115));\n"
"  d = smin(d, cap(p, vec3(0.0,0.58,0.0), vec3(0.0,1.06,-0.02), 0.205), 0.10);\n"
"  d = smin(d, cap(p, vec3(0.0,1.06,-0.02), vec3(0.0,1.19,-0.01), 0.075), 0.05);\n"
"  d = smin(d, length(p - vec3(0.0,1.34,0.0)) - 0.125, 0.045);\n"
"  d = smin(d, cap(p, vec3(-0.21,1.00,0.0), vec3(-0.25,0.50,0.10), 0.058), 0.05);\n"
"  d = smin(d, cap(p, vec3( 0.21,1.00,0.0), vec3( 0.26,0.52,0.12), 0.058), 0.05);\n"
"  return d; }\n"

"// returns distance; id says what was hit\n"
"float scene(vec3 p, out float id){\n"
"  float d = -box(p - vec3(0.0, 1.30, 0.0), vec3(3.30, 1.55, 4.20));\n"
"  id = 1.0;                                  // room shell\n"
"  float panel = box(p - vec3(0.0, 2.80, -1.20), vec3(0.85, 0.04, 0.30));\n"
"  if (panel < d) { d = panel; id = 2.0; }     // the light over the bed\n"
"  float win = box(p - vec3(-3.28, 1.55, -1.40), vec3(0.06, 0.70, 1.15));\n"
"  if (win < d) { d = win; id = 3.0; }         // daylight\n"
"  float rail = min(cyl(p - vec3(0.90, 0.62, -0.40), 0.045, 1.30),\n"
"                   cyl(vec3(p.x, p.z, p.y) - vec3(0.90, -0.40, 0.62), 0.045, 0.30));\n"
"  if (rail < d) { d = rail; id = 4.0; }       // bed rail\n"
"  float stand = cyl(p - vec3(2.30, 1.10, -1.80), 0.035, 1.10);\n"
"  if (stand < d) { d = stand; id = 4.0; }     // drip stand\n"
"  float fig = person(p - vec3(1.42, 0.00, -0.95));\n"
"  if (fig < d) { d = fig; id = 5.0; }         // someone standing there\n"
"  return d; }\n"
"\n"
"vec3 normal(vec3 p){ float i; vec2 e = vec2(0.0015, 0.0);\n"
"  return normalize(vec3(scene(p+e.xyy,i)-scene(p-e.xyy,i),\n"
"                        scene(p+e.yxy,i)-scene(p-e.yxy,i),\n"
"                        scene(p+e.yyx,i)-scene(p-e.yyx,i))); }\n"
"\n"
"void main(){\n"
"  vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;\n"
"\n"
"  // Waking is not a fade. It is a slit that widens, and the shape of that\n"
"  // slit is the whole reason the moment reads without a word of text.\n"
"  float lid  = uOpen * 0.62 * (1.0 - 0.55 * uv.x * uv.x);\n"
"  float lash = smoothstep(lid, lid - 0.045, abs(uv.y + 0.02));\n"
"  if (lash <= 0.001) { FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
"\n"
"  vec3 ro = vec3(0.0, 1.05, 1.55);\n"
"  vec3 rd = normalize(vec3(uv.x * 1.05, uv.y * 1.05 + 0.30, -1.0));\n"
"  float t = 0.0, id = 1.0, hid = 1.0;\n"
"  for (int i = 0; i < 78; i++){\n"
"    float d = scene(ro + rd * t, id);\n"
"    if (d < 0.002) { hid = id; break; }\n"
"    t += d * 0.85; if (t > 22.0) break; }\n"
"\n"
"  vec3 p = ro + rd * t, n = normal(p);\n"
"  vec3 alb = vec3(0.86);\n"
"  if (hid > 1.5 && hid < 2.5) alb = vec3(1.00, 0.98, 0.94);\n"
"  if (hid > 2.5 && hid < 3.5) alb = vec3(0.96, 0.98, 1.00);\n"
"  if (hid > 3.5 && hid < 4.5) alb = vec3(0.62, 0.65, 0.70);\n"
"  if (hid > 4.5)              alb = vec3(0.30, 0.30, 0.33);\n"
"\n"
"  vec3 lp = vec3(0.0, 2.70, -1.20);\n"
"  vec3 ld = normalize(lp - p);\n"
"  float dif = max(dot(n, ld), 0.0) * 1.15;\n"
"  float win = max(dot(n, normalize(vec3(-1.0, 0.10, -0.25))), 0.0) * 0.55;\n"
"  vec3 col = alb * (0.34 + dif + win);\n"
"  if (hid > 1.5 && hid < 3.5) col = alb * 2.6;   // the two light sources\n"
"\n"
"  // everything is still swimming until the very end\n"
"  float blur = (1.0 - uSharp);\n"
"  col = mix(col, vec3(dot(col, vec3(0.33))), blur * 0.75);\n"
"  col += blur * 0.30 * vec3(0.92, 0.95, 1.00);\n"
"  col = mix(col, vec3(1.0), uBright * 0.52);\n"
"\n"
"  float vig = 1.0 - 0.35 * dot(uv, uv);\n"
"  FragColor = vec4(col * vig * lash, 1.0); }\n";

#endif /* SHADERS_H */
