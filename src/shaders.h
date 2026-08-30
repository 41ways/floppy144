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
"uniform float uGrey;\n"
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
"uniform float uGrey;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"  if (vBright < 0.02) discard;\n"
/* round points read as returns; square ones read as pixels */
"  vec2 q = gl_PointCoord - 0.5;\n"
"  if (dot(q, q) > 0.25) discard;\n"
/* lidar palette: near returns warm, far returns cold */
"  float t = clamp(1.0 - vDist / 26.0, 0.0, 1.0);\n"
"  vec3  c = mix(vec3(0.13, 0.20, 0.90), vec3(1.00, 0.62, 0.16), t * t);\n"
/* deeper down the palette drains toward the room it is becoming */
"  c = mix(c, vec3(dot(c, vec3(0.35))) * vec3(1.02, 1.03, 1.06), uGrey);\n"
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


/* The cave, marched and shaded instead of sampled.
 *
 * The same distance field game.c walks through, ported to GLSL so it can be
 * lit. It fades in over the point cloud as the descent goes on: stage three
 * is mostly points with surfaces showing through, stage four is mostly
 * surface. Nothing is loaded to make any of it - the rock colour, the bump,
 * the occlusion and the shadow are the same noise the tunnel is carved from,
 * read at other scales. */
static const char *CAVE_FS =
"#version 330 core\n"
"out vec4 FragColor;\n"
"uniform vec2  uRes;\n"
"uniform vec3  uCam, uFwd, uRight, uUp;\n"
"uniform float uSeed, uWander, uRough, uTime;\n"
"uniform float uLight;    // how much of it is lit from ahead\n"
"uniform float uWet;      // stage two shines\n"
"uniform float uRoom;     // the rock squaring into somewhere built\n"
"\n"
"uniform vec3  uBrA[9];\n"
"uniform vec3  uBrB[9];\n"
"\n"
"float h1(float n){ uint h = floatBitsToUint(n);\n"
"  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;\n"
"  return float(h & 0xFFFFFFu) / 16777216.0; }\n"
"float n1(float x){ float i = floor(x), f = x - i, u = f*f*(3.0-2.0*f);\n"
"  return mix(h1(i), h1(i+1.0), u); }\n"
"float fbm2(float a, float b){ return 0.55*n1(a*1.7+b*3.1)\n"
"  + 0.30*n1(a*3.9-b*2.3+17.0) + 0.15*n1(a*8.1+b*6.7+43.0); }\n"
"\n"
"vec2 centre(float z){ float w = uWander;\n"
"  return vec2(sin(z*0.100+uSeed)*3.8*w + sin(z*0.400+uSeed*1.7)*1.4,\n"
"              cos(z*0.070+uSeed*2.3)*2.4*w + sin(z*0.310+uSeed*3.1)*1.1); }\n"
"float dk(float z){ return clamp(-z/120.0, 0.0, 1.0); }\n"
"float gate(float z){ float d = -z, best = 1e9, dz; int k = 0;\n"
"  float G[4] = float[4](28.0, 58.0, 90.0, 120.0);\n"
"  for (int i = 0; i < 4; i++){ float t = abs(d - G[i]);\n"
"    if (t < best) { best = t; k = i; } }\n"
"  if (best > 20.0) return 0.0;\n"
"  dz = d - G[k]; return 9.5 * exp(-(dz*dz)/50.0); }\n"
"float segd(vec3 p, vec3 a, vec3 b){ vec3 pa = p-a, ba = b-a;\n"
"  float t = clamp(dot(pa,ba)/max(dot(ba,ba),1e-6), 0.0, 1.0);\n"
"  return length(pa - ba*t); }\n"
"\n"
"float field(vec3 p){\n"
"  vec2 c = centre(p.z);\n"
"  vec2 d2 = vec2(p.x - c.x, (p.y - c.y) * 1.25);\n"
"  float rad = (2.35 - 0.95*dk(p.z))\n"
"            + 1.15*uRough*fbm2(atan(d2.y, d2.x)*1.6, p.z*0.42 + uSeed)\n"
"            + gate(p.z);\n"
"  float m = rad - length(d2);\n"
/* Toward the end the tunnel forgets how to be a tunnel. A box corridor
   with the same axis takes over, corners first: the cave becomes
   architecture around you before there is a room. */
"  vec2 q = abs(vec2(p.x - c.x, p.y - c.y)) - vec2(2.30, 1.75);\n"
"  float boxm = -max(q.x, q.y);\n"
"  m = mix(m, boxm, uRoom);\n"
"  int i0 = int(clamp(-p.z/34.0, 0.0, 7.0));\n"
"  m = max(m, 1.75 - segd(p, uBrA[i0],   uBrB[i0]));\n"
"  m = max(m, 1.75 - segd(p, uBrA[i0+1], uBrB[i0+1]));\n"
"  return m; }\n"
"\n"
"vec3 fnorm(vec3 p){ vec2 e = vec2(0.012, 0.0);\n"
"  return normalize(vec3(field(p+e.xyy)-field(p-e.xyy),\n"
"                        field(p+e.yxy)-field(p-e.yxy),\n"
"                        field(p+e.yyx)-field(p-e.yyx))); }\n"
"\n"
"float grain(vec3 p){\n"
"  return 0.55*fbm2(p.x*5.1 + p.y*2.3, p.z*4.7 + p.y*1.9)\n"
"       + 0.30*fbm2(p.y*11.3 + p.z*4.1, p.x*9.7 + p.z*3.3)\n"
"       + 0.15*fbm2(p.z*23.0 + p.x*8.0, p.y*19.0 + p.x*6.0); }\n"
"\n"
"float occl(vec3 p, vec3 n){ float o = 0.0, s = 1.0;\n"
"  for (int i = 1; i <= 5; i++){ float h = 0.055*float(i);\n"
"    o += (h - field(p + n*h)) * s; s *= 0.62; }\n"
"  return clamp(1.0 - 2.2*o, 0.0, 1.0); }\n"
"\n"
"float shade_to(vec3 p, vec3 l){ float t = 0.10, r = 1.0;\n"
"  for (int i = 0; i < 22; i++){ float d = field(p + l*t);\n"
"    if (d < 0.01) return 0.0;\n"
"    r = min(r, 9.0*d/t); t += clamp(d, 0.04, 0.5);\n"
"    if (t > 14.0) break; }\n"
"  return clamp(r, 0.0, 1.0); }\n"
"\n"
"void main(){\n"
"  vec2 uv = (gl_FragCoord.xy - 0.5*uRes) / uRes.y;\n"
"  vec3 rd = normalize(uRight*uv.x + uUp*uv.y + uFwd*1.35);\n"
"  float t = 0.05;\n"
"  bool hit = false;\n"
"  for (int i = 0; i < 150; i++){\n"
"    float d = field(uCam + rd*t);\n"
"    if (d < 0.006) { hit = true; break; }\n"
"    t += max(d*0.42, 0.012);\n"
"    if (t > 32.0) break; }\n"
"  if (!hit) { FragColor = vec4(0.0, 0.0, 0.0, 0.0); return; }\n"
"\n"
"  vec3 p = uCam + rd*t;\n"
"  vec3 n = fnorm(p);\n"
"  float g0 = grain(p);\n"
"  vec3 gp = vec3(grain(p+vec3(0.03,0.0,0.0)) - g0,\n"
"                 grain(p+vec3(0.0,0.03,0.0)) - g0,\n"
"                 grain(p+vec3(0.0,0.0,0.03)) - g0);\n"
"  n = normalize(n - (gp - n*dot(gp,n)) * 1.6 * (1.0 - uRoom));\n"
"\n"
"  vec3 alb = mix(vec3(0.20,0.19,0.185), vec3(0.44,0.39,0.34), g0);\n"
"  alb = mix(alb, vec3(0.30,0.34,0.38), uWet*0.55);\n"
/* built surfaces are pale and even; the grain fades with the rock */
"  alb = mix(alb, vec3(0.78, 0.79, 0.81), uRoom * 0.85);\n"
"\n"
"  vec2 lc = centre(uCam.z - 20.0);\n"
"  vec3 lp = vec3(lc.x, lc.y + 0.6, uCam.z - 20.0);\n"
"  vec3 ld = normalize(lp - p);\n"
"  float dif = max(dot(n, ld), 0.0);\n"
"  float sh  = shade_to(p, ld);\n"
"  float ao  = occl(p, n);\n"
"  float fre = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);\n"
"\n"
"  vec3 warm = mix(vec3(1.00, 0.93, 0.82), vec3(0.97, 0.98, 1.00), uRoom);\n"
"  vec3 col  = alb * (0.030 + 0.16*uLight) * ao;\n"
"  col += alb * warm * dif * sh * (0.18 + 0.62*uLight);\n"
"  col += warm * fre * (0.02 + 0.14*uLight) * ao;\n"
"  col += warm * pow(max(dot(reflect(-ld, n), -rd), 0.0), 34.0) * uWet * 0.65 * sh;\n"
"\n"
"  float fog = exp(-t * (0.075 - 0.03*uLight));\n"
"  col = mix(vec3(0.010,0.012,0.018) + warm*0.045*uLight, col, fog);\n"
"  col = col / (col + vec3(0.9));\n"
"  col = pow(max(col, vec3(0.0)), vec3(0.4545));\n"
"  FragColor = vec4(col, 1.0); }\n";

#endif /* SHADERS_H */
