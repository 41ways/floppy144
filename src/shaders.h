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
"uniform float uMonster;\n"
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
/* Two kinds of mark. Yours hold at near-full. The defibrillator kind -
   flagged by a gain above 1.5 - land far brighter and close over in about
   two seconds: that light was lent, not earned. */
"  float memory = (aGain > 1.5 ? exp(-max(age, 0.0) / 1.15) * 1.2\n"
"                              : 0.95) * (age > 0.0 ? 1.0 : 0.0) * uPersist;\n"
/* a metre of free sight so the player never walks off a ledge blind */
"  float close  = exp(-pow(d / 1.45, 2.0)) * 0.22 * uPersist;\n"
/* uBase holds a thing lit whatever the wavefront is doing, so the title
   swells as the sweep crosses it and settles back rather than going
   dark and coming on again. */
"  float lit = max(max(max(front, memory), close), uBase);\n"
"  vBright = mix(lit, 1.0, uFlat) * min(aGain, 1.45) * uFade;\n"
"  gl_PointSize = mix(clamp(230.0 / max(clip.w, 0.25), 2.0, 5.4), 2.0, uFlat);\n"
/* A returned mark is one speck of a wall and the cloud is sparse, so it is
   drawn fat enough to be seen. A monster is six thousand points inside a body
   a metre wide -- at the same size that is fifty times over the same pixels,
   and every one of them came back as a solid white blot with no legs, no head
   and no hollow under it. Thinner points give the density back as shape. */
"  if (uMonster > 0.5) gl_PointSize = clamp(90.0 / max(clip.w, 0.25), 1.6, 2.6);\n"
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
/* uMonster carries which kind: 1 stalker, 2 rusher, 3 listener */
"  if (uMonster > 0.5) {\n"
"    vec3 mc = uMonster < 1.5 ? vec3(1.00, 0.42, 0.20)\n"
"            : uMonster < 2.5 ? vec3(1.00, 0.07, 0.04)\n"
"            : vec3(0.58, 0.04, 0.30);\n"
"    c = mc;\n"
"  }\n"
"  float a = vBright * exp(-vDist * 0.055);\n"
/* the rock under your nose is already known - let the distance read */
"  a *= mix(0.88 + 0.12 * smoothstep(0.5, 4.0, vDist), 1.0, uFlat);\n"
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
"uniform float uLamp;    // 1: the room before the room\n"
"uniform float uLampB;   // its bulb, until it is put out\n"
"\n"
"float h21(vec2 q){ uvec2 u = floatBitsToUint(q);\n"
"  uint h = u.x * 0x2c1b3c6du ^ u.y * 0x297a2d39u;\n"
"  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;\n"
"  return float(h & 0xFFFFFFu) / 16777216.0; }\n"
"float vn(vec2 q){ vec2 i = floor(q), f = fract(q);\n"
"  vec2 u = f*f*(3.0-2.0*f);\n"
"  return mix(mix(h21(i), h21(i+vec2(1,0)), u.x),\n"
"             mix(h21(i+vec2(0,1)), h21(i+vec2(1,1)), u.x), u.y); }\n"
"\n"
"float box(vec3 p, vec3 b){ vec3 q = abs(p) - b;\n"
"  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }\n"
"float cyl(vec3 p, float r, float h){\n"
"  vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);\n"
"  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); }\n"
"float smin(float a, float b, float k){\n"
"  float h = clamp(0.5 + 0.5*(b-a)/k, 0.0, 1.0);\n"
"  return mix(b, a, h) - k*h*(1.0-h); }\n"
"float cap(vec3 p, vec3 a, vec3 b, float r){\n"
"  vec3 pa = p-a, ba = b-a;\n"
"  float h = clamp(dot(pa,ba)/dot(ba,ba), 0.0, 1.0);\n"
"  return length(pa - ba*h) - r; }\n"
"\n"
"float person(vec3 p){\n"
"  float d = cap(p, vec3(-0.10,-0.25,0.0), vec3(-0.06,0.62,0.0), 0.115);\n"
"  d = min(d, cap(p, vec3(0.10,-0.25,0.0), vec3(0.06,0.62,0.0), 0.115));\n"
"  d = smin(d, cap(p, vec3(0.0,0.58,0.0), vec3(0.0,1.06,-0.02), 0.205), 0.10);\n"
"  d = smin(d, cap(p, vec3(0.0,1.06,-0.02), vec3(0.0,1.19,-0.01), 0.075), 0.05);\n"
"  d = smin(d, length(p - vec3(0.02,1.34,0.02)) - 0.125, 0.045);\n"
"  d = smin(d, cap(p, vec3(-0.21,1.00,0.0), vec3(-0.28,0.45,0.14), 0.058), 0.05);\n"
"  d = smin(d, cap(p, vec3( 0.21,1.00,0.0), vec3( 0.20,0.48,0.30), 0.058), 0.05);\n"
"  return d; }\n"
"\n"
"// id: 1 wall  2 panel  3 window  4 metal  5 person  6 blanket  7 doorleaf\n"
"float scene(vec3 p, out float id){\n"
"  float d = -box(p - vec3(0.0, 1.30, 0.0), vec3(3.30, 1.55, 4.20));\n"
"  id = 1.0;\n"
"  float panel = box(p - vec3(0.0, 2.80, -1.20), vec3(0.85, 0.045, 0.32));\n"
"  if (panel < d) { d = panel; id = 2.0; }\n"
"  float win = box(p - vec3(-3.28, 1.55, -1.40), vec3(0.05, 0.72, 1.18));\n"
"  if (win < d) { d = win; id = 3.0; }\n"
"  float frame = box(p - vec3(-3.26, 1.55, -1.40), vec3(0.045, 0.76, 1.24));\n"
"  frame = max(frame, -win + 0.02);\n"
"  if (frame < d) { d = frame; id = 4.0; }\n"
"  float rail = min(cyl(p - vec3(0.92, 0.66, -0.30), 0.040, 1.20),\n"
"                   cyl(vec3(p.x, p.z, p.y) - vec3(0.92, -0.30, 0.66), 0.040, 0.34));\n"
"  if (rail < d) { d = rail; id = 4.0; }\n"
"  float stand = cyl(p - vec3(2.30, 1.10, -1.85), 0.030, 1.10);\n"
"  stand = min(stand, box(p - vec3(2.30, 2.06, -1.85), vec3(0.06, 0.14, 0.03)));\n"
"  if (stand < d) { d = stand; id = 4.0; }\n"
"  float fig = person(p - vec3(1.42, 0.00, -0.95));\n"
"  if (fig < d) { d = fig; id = 5.0; }\n"
"  // the bed you are lying in: a blanket rising into the bottom of the view\n"
"  vec3 bp = p - vec3(0.0, 0.72, 0.85);\n"
"  float blank = box(bp, vec3(0.52, 0.16 + 0.05*sin(p.x*6.0), 0.85));\n"
"  if (blank < d) { d = blank; id = 6.0; }\n"
"  float door = box(p - vec3(1.30, 1.05, 4.16), vec3(0.55, 1.05, 0.05));\n"
"  if (door < d) { d = door; id = 7.0; }\n"
"  return d; }\n"
"\n"
"vec3 nrm(vec3 p){ float i; vec2 e = vec2(0.0015, 0.0);\n"
"  return normalize(vec3(scene(p+e.xyy,i)-scene(p-e.xyy,i),\n"
"                        scene(p+e.yxy,i)-scene(p-e.yxy,i),\n"
"                        scene(p+e.yyx,i)-scene(p-e.yyx,i))); }\n"
"\n"
"float march(vec3 ro, vec3 rd, out float id){\n"
"  float t = 0.0; id = 1.0;\n"
"  for (int i = 0; i < 84; i++){\n"
"    float d = scene(ro + rd*t, id);\n"
"    if (d < 0.0018) return t;\n"
"    t += d * 0.9; if (t > 24.0) break; }\n"
"  return -1.0; }\n"
"\n"
"float shadow(vec3 p, vec3 l, float dist){\n"
"  float t = 0.05, r = 1.0, id;\n"
"  for (int i = 0; i < 30; i++){\n"
"    float d = scene(p + l*t, id);\n"
"    if (d < 0.004) return 0.0;\n"
"    r = min(r, 14.0*d/t);\n"
"    t += clamp(d, 0.03, 0.35);\n"
"    if (t > dist) break; }\n"
"  return clamp(r, 0.0, 1.0); }\n"
"\n"
"float ao(vec3 p, vec3 n){ float o = 0.0, s = 1.0, id;\n"
"  for (int i = 1; i <= 5; i++){ float h = 0.05*float(i);\n"
"    o += (h - scene(p + n*h, id)) * s; s *= 0.6; }\n"
"  return clamp(1.0 - 2.4*o, 0.0, 1.0); }\n"
"\n"
"vec3 albedo(float id, vec3 p, vec3 n){\n"
"  if (id < 1.5) {         // painted wall, vinyl floor, ceiling\n"
"    if (n.y > 0.9 && p.y < 0.2) {   // floor: warm vinyl with a tile seam\n"
"      vec2 g = fract(p.xz * 0.55) - 0.5;\n"
"      float seam = smoothstep(0.47, 0.5, max(abs(g.x), abs(g.y)));\n"
"      return mix(vec3(0.55, 0.50, 0.44), vec3(0.38, 0.34, 0.30), seam)\n"
"           * (0.92 + 0.16 * vn(p.xz * 7.0));\n"
"    }\n"
"    return vec3(0.88, 0.87, 0.83) * (0.94 + 0.09 * vn(p.zy * 3.0 + p.xx));\n"
"  }\n"
"  if (id < 2.5) return vec3(1.0);\n"
"  if (id < 3.5) return vec3(1.0);\n"
"  if (id < 4.5) return vec3(0.55, 0.57, 0.60);\n"
"  if (id < 5.5) return vec3(0.22, 0.26, 0.34);   // scrubs blue, dark\n"
"  if (id < 6.5) return vec3(0.72, 0.78, 0.82);   // the blanket\n"
"  return vec3(0.42, 0.33, 0.24);                 // the door\n"
"}\n"
"\n"
"void main(){\n"
"  vec2 uv = (gl_FragCoord.xy - 0.5*uRes) / uRes.y;\n"
"\n"

/* Before the hospital there is a room with nothing in it but a lamp.
   It reads as infinite because there is nothing to measure it by: a
   floor, white fog, one bulb on a cord. When the bulb goes out you are
   nowhere at all - and then the eye opens. */
"  if (uLamp > 0.5) {\n"
"    vec3 lro = vec3(0.0, 1.25, 4.0);\n"
"    vec3 lrd = normalize(vec3(uv.x, uv.y + 0.06, -1.0));\n"
"    vec3 L   = vec3(0.0, 2.30, -2.2);\n"
"    vec3 lc  = vec3(0.0);\n"
"    if (lrd.y < -0.001) {\n"
"      float lt = -lro.y / lrd.y;\n"
"      vec3 lp2 = lro + lrd * lt;\n"
"      float ld2 = length(lp2 - vec3(L.x, 0.0, L.z));\n"
"      float lit2 = uLampB / (1.0 + ld2 * ld2 * 0.10);\n"
"      lc = vec3(0.99, 0.97, 0.92) * (0.06 + 1.1 * lit2);\n"
"      lc = mix(lc, vec3(0.94) * (0.10 + 0.85 * uLampB),\n"
"               smoothstep(4.0, 22.0, lt));\n"
"    } else {\n"
"      lc = vec3(0.93) * (0.05 + 0.80 * uLampB) * (1.0 - uv.y * 0.55);\n"
"    }\n"
"    vec3 toL = L - lro;\n"
"    float bh = length(cross(lrd, normalize(toL)));\n"
"    if (dot(lrd, normalize(toL)) > 0.0) {\n"
"      if (bh < 0.020) lc = vec3(1.5, 1.42, 1.2) * (uLampB + 0.06);\n"
"      else lc += vec3(1.0, 0.95, 0.8) * uLampB * 0.05 / (bh * 18.0 + 0.25);\n"
"      if (abs(lrd.x * length(toL) - toL.x) < 0.006 && lrd.y * length(toL) > toL.y)\n"
"        lc = vec3(0.10);\n"
"    }\n"
"    lc += (h21(gl_FragCoord.xy + uTime) - 0.5) * 0.030;\n"
"    lc *= 1.0 - 0.28 * dot(uv, uv);\n"
"    FragColor = vec4(clamp(lc, 0.0, 1.0), 1.0); return;\n"
"  }\n"
"  float lid  = uOpen * 0.62 * (1.0 - 0.55 * uv.x * uv.x);\n"
"  float lash = smoothstep(lid, lid - 0.05, abs(uv.y + 0.02));\n"
"  if (lash <= 0.001) { FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
"\n"
"  // the eye is not steady yet: the view drifts and settles as uSharp rises\n"
"  vec2 drift = vec2(sin(uTime*0.7), cos(uTime*0.9)) * 0.012 * (1.0 - uSharp);\n"
"  vec3 ro = vec3(0.0, 1.02, 1.55);\n"
"  vec3 rd = normalize(vec3(uv.x + drift.x, uv.y + 0.30 + drift.y, -1.0) * vec3(1.05,1.05,1.0));\n"
"\n"
"  float id; float t = march(ro, rd, id);\n"
"  vec3 col;\n"
"  if (t < 0.0) col = vec3(0.9);\n"
"  else {\n"
"    vec3 p = ro + rd*t;\n"
"    vec3 n = nrm(p);\n"
"    vec3 alb = albedo(id, p, n);\n"
"    float occ = ao(p, n);\n"
"\n"
"    if (id > 1.5 && id < 3.5) {\n"
"      // the two emitters, seen directly: panel cool, window warm daylight\n"
"      col = (id < 2.5) ? vec3(1.15, 1.14, 1.10)\n"
"                       : vec3(1.35, 1.22, 0.95) * (0.85 + 0.3*uv.y);\n"
"    } else {\n"
"      // window as an area light: four jittered samples across the glass\n"
"      vec3 sun = vec3(0.0);\n"
"      for (int i = 0; i < 4; i++){\n"
"        vec2 j = vec2(h21(uv + float(i)*1.7), h21(uv.yx + float(i)*2.3)) - 0.5;\n"
"        vec3 wp = vec3(-3.2, 1.55 + j.x*1.3, -1.4 + j.y*2.2);\n"
"        vec3 ld = wp - p; float dist = length(ld); ld /= dist;\n"
"        float sh = shadow(p, ld, dist);\n"
"        sun += vec3(1.30, 1.18, 0.98) * max(dot(n, ld), 0.0) * sh\n"
"             / (1.0 + dist*dist*0.05);\n"
"      }\n"
"      sun *= 0.55;\n"
"      // the panel overhead, one shadowed sample\n"
"      vec3 pp = vec3(0.0, 2.78, -1.2);\n"
"      vec3 pl = pp - p; float pdist = length(pl); pl /= pdist;\n"
"      vec3 ceil_l = vec3(0.95, 0.96, 1.0)\n"
"                  * max(dot(n, pl), 0.0) * shadow(p, pl, pdist)\n"
"                  / (1.0 + pdist*pdist*0.10);\n"
"      // sky fill so shadow cores stay alive\n"
"      vec3 fill = vec3(0.30, 0.33, 0.38) * (0.5 + 0.5*n.y);\n"
"      col = alb * (sun + ceil_l * 0.9 + fill * 0.35) * occ;\n"
"      // vinyl gloss: one bounce toward whatever is bright\n"
"      if (id < 1.5 && n.y > 0.9 && p.y < 0.2) {\n"
"        vec3 rr = reflect(rd, n); float rid;\n"
"        float rt = march(p + n*0.01, rr, rid);\n"
"        if (rt > 0.0 && rid > 1.5 && rid < 3.5)\n"
"          col += vec3(0.5) * 0.30 / (1.0 + rt*0.6);\n"
"      }\n"
"    }\n"
"  }\n"
"\n"
"  // the grade: exposure, a filmic knee, grain, vignette, and the wash of\n"
"  // waking - unfocused first, then white as the light arrives\n"
"  col *= 1.15;\n"
"  col = mix(col, vec3(dot(col, vec3(0.333))), (1.0 - uSharp) * 0.6);\n"
"  col += (1.0 - uSharp) * 0.24 * vec3(0.9, 0.94, 1.0);\n"
"  col = mix(col, vec3(1.0), uBright * 0.45);\n"
"  col = (col * (2.51*col + 0.03)) / (col * (2.43*col + 0.59) + 0.14);\n"
"  col += (h21(gl_FragCoord.xy + uTime) - 0.5) * 0.035;\n"
"  col *= 1.0 - 0.30 * dot(uv, uv);\n"
"  FragColor = vec4(clamp(col, 0.0, 1.0) * lash, 1.0);\n"
"}\n";


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
"uniform float uRoad;     // past the door: no walls end it\n"
"uniform float uWhite;    // and the world goes to white\n"
"uniform float uPulse;    // the beat you feel as the door nears\n"
"\n"
"uniform vec3  uBrA[16];\n"
"uniform vec3  uBrB[16];\n"
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
"float dk(float z){ return clamp(-z/480.0, 0.0, 1.0); }\n"
"float gate(float z){ float d = -z, best = 1e9, dz; int k = 0;\n"
"  float G[4] = float[4](120.0, 240.0, 360.0, 480.0);\n"
"  for (int i = 0; i < 4; i++){ float t = abs(d - G[i]);\n"
"    if (t < best) { best = t; k = i; } }\n"
/* R[0] is 4 and not 15 because the first threshold stopped being a wider
   piece of cave: it is a room, carved below. The C field was changed and
   this one was not, so for a while the sounding drew a square room with a
   pool in it while the rock drawn behind it was a round chamber. */
"  float R[4] = float[4]( 4.0, 9.5, 9.5, 11.0);\n"
"  float W[4] = float[4]( 8.0, 5.0, 5.0,  5.0);\n"
"  if (best > W[k]*4.0) return 0.0;\n"
"  dz = d - G[k]; return R[k] * exp(-(dz*dz)/(2.0*W[k]*W[k])); }\n"
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
/* The first threshold, mirrored from cave_sdf: flat floor, flat walls, flat
   ceiling, and a rectangular pool cut into the middle of it. Blended in over
   seven metres at each end so the passage runs into the doorways. */
"  { float dep = -p.z, rd = abs(dep - 125.0);\n"
"    if (rd < 21.0) {\n"
"      float fl   = c.y - 1.05;\n"
"      float pool = min(min(8.5 - abs(d2.x), 8.0 - rd), p.y - (fl - 7.0));\n"
"      float flor = max(p.y - fl, pool);\n"
"      float room = min(min(12.0 - abs(d2.x), (c.y + 3.2) - p.y),\n"
"                       min(17.0 - rd, flor));\n"
"      m = mix(m, room, 1.0 - smoothstep(13.0, 20.0, rd));\n"
"    } }\n"
/* Toward the end the tunnel forgets how to be a tunnel. A box corridor
   with the same axis takes over, corners first: the cave becomes
   architecture around you before there is a room. */
"  float slab = 1.45 - abs(p.y - c.y + 0.3);\n"
"  vec2 mp = mod(p.xz + 3.5, 7.0) - 3.5;\n"
"  float pil = max(abs(mp.x), abs(mp.y)) - 0.42;\n"
"  float boxm = min(slab, pil);\n"
/* the maze: a wall every seventh metre with one doorway, stubs off the
   pillars - rooms that stop, and one way that always leads on */
/* but not on the road. Through the door there is no room, there is distance,
   and a wall every seventh metre is a room -- the last thing the ending wants
   is the maze slamming shut in front of the reveal. Pillars only. */
"  if (uRoad < 0.5) {\n"
"    float rz = mod(p.z + 3.5, 7.0) - 3.5;\n"
"    float ri = floor((-p.z + 3.5) / 7.0);\n"
"    vec2  rc = centre(ri * -7.0 + 3.5);\n"
"    float gx = rc.x + (h1(ri * 7.77 + uSeed * 2.0) - 0.5) * 16.0;\n"
"    float wd = abs(rz) - 0.20;\n"
"    if (abs(p.x - gx) > 1.60 && wd < boxm) boxm = wd;\n"
"    float ci = floor((p.x + 3.5) / 7.0) * 57.0 + ri;\n"
"    float hs = h1(ci * 3.17 + uSeed);\n"
"    if (hs < 0.18) { if (mp.y > 0.40 && mp.y < 3.12) {\n"
"      float w2 = abs(mp.x) - 0.17; if (w2 < boxm) boxm = w2; } }\n"
"    else if (hs < 0.36) { if (mp.x > 0.40 && mp.x < 3.12) {\n"
"      float w2 = abs(mp.y) - 0.17; if (w2 < boxm) boxm = w2; } }\n"
"    {\n"
"      float open2 = min(1.60 - abs(p.x - gx), 1.60 - abs(rz));\n"
"      open2 = min(open2, slab);\n"
"      boxm = max(boxm, open2);\n"
"    }\n"
"  }\n"
"  m = mix(m, boxm, uRoom);\n"
/* 526, matching WAKE_Z in cave_sdf. It said 525, so the end wall was drawn a
   metre nearer than the one you actually stop against. */
"  if (uRoad < 0.5) m = min(m, p.z + 526.0);\n"
"  int i0 = int(clamp(-p.z/34.0, 0.0, 14.0));\n"
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
/* In the dark, distance is black and that is the whole point - the cave is
   only what the sounding gave back. But once the corridor takes over, the
   place is lit, and a hole of pure black in the middle of a lit room reads
   as a tear in the picture rather than as depth. So far away goes to the
   room's own haze, at the value the horizon lands on after tone mapping. */
"  if (!hit) {\n"
"    FragColor = vec4(mix(vec3(0.68, 0.67, 0.64), vec3(1.0), uWhite),\n"
"                     max(uRoom, uWhite));\n"
"    return; }\n"
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
/* the last thing the corridor shows you: a door, darker than its wall */
"  if (uRoad < 0.5 && p.z < -523.9) {\n"
"    vec2 dc = centre(p.z);\n"
"    if (abs(p.x - dc.x) < 0.62 && p.y - dc.y < 0.75 && p.y - dc.y > -1.55)\n"
"      alb = vec3(0.24, 0.20, 0.16);\n"
"  }\n"
"  alb = mix(alb, vec3(0.30,0.34,0.38), uWet*0.55);\n"
/* built surfaces are pale and even; the grain fades with the rock */
"  alb = mix(alb, vec3(0.84, 0.79, 0.55), uRoom * 0.9);\n"
/* The sign over the door -- the one saturated thing in the game. Down here
   every surface is the same pale beige and there is nothing to walk toward;
   this says the way out is a real place, and gives the last stretch of the
   hall a direction. Green, because that is what the sign over a door is. */
"  vec3 sign_col = vec3(0.0);\n"
"  float sign_m  = 0.0;\n"
"  if (uRoad < 0.5 && uRoom > 0.3 && p.z < -523.9) {\n"
"    vec2 sc = centre(p.z);\n"
"    vec2 q  = vec2(p.x - sc.x, p.y - sc.y - 0.95);\n"
"    if (abs(q.x) < 0.55 && abs(q.y) < 0.15) {\n"
/* a figure walking, and an arrow saying which way: pale on green, the way
   the sign over a door is. It is lit from inside, so it covers the wall
   rather than adding to it -- added, the beige leaked through and took the
   colour out of the only coloured thing in the game. */
"      float head = step(length(q - vec2(-0.27, 0.070)), 0.029);\n"
"      float body = step(abs(q.x + 0.27), 0.026) * step(abs(q.y + 0.004), 0.060);\n"
"      float legs = step(abs(abs(q.x + 0.27) - 0.034), 0.019)\n"
"                 * step(abs(q.y + 0.088), 0.030);\n"
"      float arrw = step(0.14, q.x) * step(q.x, 0.36)\n"
"                 * step(abs(q.y), 0.058 * (1.0 - (q.x - 0.14) / 0.22));\n"
"      float fig  = clamp(head + body + legs + arrw, 0.0, 1.0);\n"
"      sign_col = mix(vec3(0.02, 2.20, 0.16), vec3(2.20, 2.40, 2.20), fig);\n"
"      sign_m   = uRoom;\n"
"    }\n"
"  }\n"
/* A corridor reads because its surfaces do not share a value: the ceiling
   carries the light, the walls take less of it, the floor least of all.
   Lit only by panels overhead, every plane landed on the same beige and the
   room lost its corners -- you could not tell where the floor met the wall. */
"  alb *= mix(1.0, 0.78 - 0.22*clamp(n.y, 0.0, 1.0)\n"
"                      + 0.30*clamp(-n.y, 0.0, 1.0), uRoom);\n"
/* What a corridor has that a lit tube does not: a rail at hand height, a
   skirting where the wall meets the floor, and tiles overhead and underfoot
   to count your way along. None of it is geometry -- it is read off the
   wall's own coordinates, so it costs nothing, and it is the whole reason
   the place has a size you can feel. It fades out with distance rather than
   shimmering away into noise. */
"  if (uRoom > 0.3) {\n"
"    float wy    = p.y - centre(p.z).y;\n"
"    float upr   = 1.0 - clamp(abs(n.y) * 2.4, 0.0, 1.0);\n"
"    float lying = clamp(abs(n.y) * 1.8 - 0.7, 0.0, 1.0);\n"
"    float near  = exp(-t * 0.11) * uRoom;\n"
"    float rail  = smoothstep(0.062, 0.036, abs(wy + 0.80));\n"
"    float skirt = smoothstep(0.19, 0.12, abs(wy + 1.62));\n"
"    vec2  tm    = abs(mod(p.xz + 0.875, 1.75) - 0.875);\n"
"    float seam  = smoothstep(0.842, 0.871, max(tm.x, tm.y));\n"
/* all of it darker than the wall, never lighter: after the panels are done
   with it the wall is already near white, and a bright line on a bright
   wall is a line nobody sees */
"    alb = mix(alb, alb * 0.40, upr   * skirt * near);\n"
"    alb = mix(alb, alb * 0.45, upr   * rail  * near);\n"
"    alb = mix(alb, alb * 0.48, lying * seam  * near);\n"
/* A hospital corridor is two colours, not one: a darker dado below the rail
   where the trolleys hit it, pale above. A single tone floor to ceiling is
   what a cave painted beige looks like, which is what this was. This one
   does not fade with distance -- it is the wall's colour, not a detail on
   it, and it is most of what makes the place read as built. */
"    float dado = smoothstep(-0.88, -0.74, wy);\n"
"    alb *= mix(1.0, mix(0.70, 1.10, dado), upr * uRoom);\n"
/* and doors. Shut, one to a bay, with a frame around them. A corridor with
   nothing off it is a tube; a corridor lined with doors that do not open is
   the thing this place is supposed to be. */
"    float dz2   = abs(mod(p.z + 3.5, 7.0) - 3.5);\n"
"    float e1    = max(dz2 / 1.00, abs(wy + 0.72) / 1.02);\n"
"    float door  = smoothstep(1.00, 0.93, e1);\n"
"    float frame = smoothstep(0.90, 0.99, e1) * smoothstep(1.16, 1.03, e1);\n"
"    alb = mix(alb, alb * 0.52, upr * door  * uRoom);\n"
"    alb = mix(alb, alb * 1.22, upr * frame * uRoom);\n"
"  }\n"
/* ceiling panels on the same seven-metre grid as the pillars */
"  if (uRoom > 0.5 && n.y < -0.7) {\n"
"    vec2 pm = abs(mod(p.xz, 7.0) - 3.5);\n"
"    if (max(pm.x, pm.y) < 1.1) alb = vec3(1.35, 1.33, 1.22);\n"
"  }\n"
"\n"
"  vec2 lc = centre(uCam.z - 20.0);\n"
"  vec3 lp = vec3(lc.x, lc.y + 0.6, max(uCam.z - 20.0, -521.5));\n"
"  vec3 ld = normalize(lp - p);\n"
/* Wrapped diffuse: rock in a cave is lit by everything the light has
   already touched, so the terminator softens instead of cutting to
   black - the moon look was a hard max(dot,0) with no fill at all. */
"  float dif = clamp((dot(n, ld) + 0.38) / 1.38, 0.0, 1.0);\n"
"  float sh  = shade_to(p, ld);\n"
"  float ao  = occl(p, n);\n"
"  float fre = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);\n"
"\n"
"  vec3 warm = mix(vec3(1.00, 0.93, 0.82), vec3(1.00, 0.96, 0.84), uRoom);\n"
/* No ambient floor: a surface no light reaches stays black, or the
   sounding would stop being the way you see. The corridor doubles it. */
"  float sky = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);\n"
"  vec3 hemi = mix(warm * 0.55, vec3(0.85, 0.90, 1.0), sky);\n"
"  vec3 col  = alb * hemi * uLight * (0.16 + 0.40*uRoom) * ao;\n"
/* and a floor bounce, so ceilings are not painted-on black */
"  col += alb * warm * clamp(-n.y, 0.0, 1.0) * uLight * 0.10;\n"
"  col += alb * warm * dif * mix(sh, 1.0, 0.22) * (0.18 + 0.55*uLight);\n"
"  col += warm * fre * (0.02 + 0.14*uLight) * ao;\n"
"  col += warm * pow(max(dot(reflect(-ld, n), -rd), 0.0), 34.0) * uWet * 0.65 * sh;\n"
"\n"
/* The kkrieger half of the look: the ceiling panels are not just painted on,
   the nearest four of them are lights. Positions come from snapping to the
   same seven-metre grid the geometry uses, so this costs no data at all -
   diffuse plus a Blinn lobe each, falling off with distance squared. */
"  if (uRoom > 0.15) {\n"
"    vec2 base = floor((p.xz - vec2(3.5)) / 7.0) * 7.0 + vec2(3.5);\n"
"    vec3 pan = vec3(1.00, 0.97, 0.86) * uRoom;\n"
"    for (int px = 0; px < 2; px++)\n"
"    for (int pz = 0; pz < 2; pz++) {\n"
"      vec2 pcv = base + vec2(float(px), float(pz)) * 7.0;\n"
"      vec3 plp = vec3(pcv.x, centre(pcv.y).y + 1.02, pcv.y);\n"
"      vec3 pld = plp - p;\n"
"      float pd2 = dot(pld, pld);\n"
"      pld *= inversesqrt(max(pd2, 1e-4));\n"
"      float att = 1.0 / (1.0 + pd2 * 0.10);\n"
"      float pdif = max(dot(n, pld), 0.0);\n"
"      vec3 hv = normalize(pld - rd);\n"
"      float pspec = pow(max(dot(n, hv), 0.0), 26.0);\n"
/* with the corners left alone, so the pillars have a base and the wall
   meeting the floor is a line rather than a guess */
"      col += alb * pan * pdif * att * 0.85 * mix(ao, 1.0, 0.35);\n"
"      col += pan * pspec * att * 0.55;\n"
"    }\n"
"  }\n"
/* Depth needs somewhere to go. In the cave that is darkness; in the corridor
   everything is the same pale albedo under the same soft panels, so without
   a haze to recede into the far wall sits at the same value as the near one
   and the whole frame flattens into one sheet of beige. */
"  float fog = exp(-t * (0.075 - 0.03*uLight + 0.022*uRoom));\n"
"  col = mix(mix(vec3(0.010,0.012,0.018) + warm*0.045*uLight,\n"
"                vec3(0.66, 0.63, 0.55), uRoom), col, fog);\n"/* The sign takes some of the haze and not all of it. Fogged like a wall it
   came out the same mint as everything else, and a sign that has gone the
   colour of the corridor is not a sign -- it is meant to be the one thing
   down here you can pick out from the far end. */
"  col = mix(col, sign_col, sign_m * mix(0.55, 1.0, fog));\n"
"  col = col / (col + vec3(0.9));\n"
"  col = pow(max(col, vec3(0.0)), vec3(0.4545));\n"
"  col += vec3(1.0, 0.98, 0.95) * uPulse * 0.10 * uRoom;\n"
"  col = mix(col, vec3(1.0), uWhite);\n"
"  FragColor = vec4(col, 1.0); }\n";

#endif /* SHADERS_H */
