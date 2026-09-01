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
/* The borrowed light does not dim with the room. uFade exists to let the
   sounding step back as the building lights up -- which is right for marks
   you made, and wrong for the two kinds you did not: the defibrillator, and
   the pulse from the heart that knows where the door is. Those are a signal
   from outside, and inside the building they were being multiplied down to a
   tenth against walls at four fifths, so the one hint the maze gives you was
   invisible in the only place it is needed. Gain over 1.5 is that light. */
"  float fade = mix(uFade, 1.0, step(1.5, aGain));\n"
"  vBright = mix(lit, 1.0, uFlat) * min(aGain, 1.45) * fade;\n"
"  gl_PointSize = mix(clamp(230.0 / max(clip.w, 0.25), 2.0, 5.4), 2.0, uFlat);\n"
/* A returned mark is one speck of a wall and the cloud is sparse, so it is
   drawn fat enough to be seen. A monster is six thousand points inside a body
   a metre wide -- at the same size that is fifty times over the same pixels,
   and every one of them came back as a solid white blot with no legs, no head
   and no hollow under it. Thinner points give the density back as shape. */
"  if (uMonster > 0.5 && uMonster < 3.5) gl_PointSize = clamp(90.0 / max(clip.w, 0.25), 1.6, 2.6);\n"
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
/* Aqua, not cyan. The far end of the distance palette is already a deep
   blue, and a blue-leaning water read as more of the same; the green
   is what nothing else in the game has. */
"    vec3 mc = uMonster > 3.5 ? vec3(0.10, 1.00, 0.62)   // water\n"
"            : uMonster < 1.5 ? vec3(1.00, 0.42, 0.20)\n"
"            : uMonster < 2.5 ? vec3(1.00, 0.07, 0.04)\n"
"            : vec3(0.58, 0.04, 0.30);\n"
"    c = mc;\n"
"  }\n"
"  float a = vBright * exp(-vDist * 0.055);\n"
/* the rock under your nose is already known - let the distance read */
"  a *= mix(0.88 + 0.12 * smoothstep(0.5, 4.0, vDist), 1.0, uFlat);\n"
/* the things come back brighter than rock; water does not, it glints */
"  a = mix(a, min(a * 2.2, 1.0), (uMonster > 0.5 && uMonster < 3.5) ? 1.0 : 0.0);\n"
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
"uniform float uRun;     // the doctor, 0 at the door and 1 at the bedside\n"
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
/* One beat of a sinus rhythm, over 0..1. The whole reason the room reads as
   an intensive care unit rather than as a bedroom is this shape: everybody
   knows it, nobody has to be told what it means, and it is the one thing on
   screen that says the body you are in is working. */
"float ecg(float x){\n"
"  return  0.16*exp(-pow((x-0.14)/0.038, 2.0))\n"
"        - 0.16*exp(-pow((x-0.30)/0.013, 2.0))\n"
"        + 1.00*exp(-pow((x-0.345)/0.012, 2.0))\n"
"        - 0.34*exp(-pow((x-0.395)/0.017, 2.0))\n"
"        + 0.30*exp(-pow((x-0.60)/0.060, 2.0)); }\n"
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
"// 1 room  2 panel  3 window  4 metal  5 visitor  6 bedding  7 door\n"
"// 8 monitor case  9 screen  10 coat  11 skin  12 lead  13 fluid bag\n"
"// 14 curtain  15 scrubs  16 headwall\n"
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
/* The headwall. Every one of these rooms has one behind the bed: a long
   white trunking full of outlets and gas ports, and it is the thing that
   says ward rather than bedroom more plainly than anything except the
   monitor. It sits on the right wall where the eye can reach it. */
"  float hw = box(p - vec3(3.24, 1.42, 0.10), vec3(0.07, 0.30, 1.30));\n"
"  if (hw < d) { d = hw; id = 16.0; }\n"
"  float rail = min(cyl(p - vec3(0.92, 0.66, -0.30), 0.040, 1.20),\n"
"                   cyl(vec3(p.x, p.z, p.y) - vec3(0.92, -0.30, 0.66), 0.040, 0.34));\n"
"  if (rail < d) { d = rail; id = 4.0; }\n"
/* A curtain, hung off a track: the partition every bay has, and the only
   large soft thing in a room made of hard ones. */
"  float trk = cyl(vec3(p.y, p.x, p.z) - vec3(2.62, -2.05, -0.60), 0.022, 1.45);\n"
"  if (trk < d) { d = trk; id = 4.0; }\n"
"  float curt = box(p - vec3(-2.05, 1.72, -0.60),\n"
"                   vec3(0.035 + 0.020*sin(p.z*7.0 + p.y*1.3), 0.90, 1.42));\n"
"  if (curt < d) { d = curt; id = 14.0; }\n"
/* The drip stand, with two bags on it and a line running down off them. */
"  float stand = cyl(p - vec3(2.05, 1.05, -1.15), 0.026, 1.05);\n"
"  stand = min(stand, cyl(p - vec3(2.05, 2.12, -1.15), 0.030, 0.03));\n"
"  stand = min(stand, box(p - vec3(2.05, 2.10, -1.15), vec3(0.16, 0.020, 0.020)));\n"
"  if (stand < d) { d = stand; id = 4.0; }\n"
"  float bag1 = box(p - vec3(1.92, 1.86, -1.15), vec3(0.075, 0.155, 0.035));\n"
"  float bag2 = box(p - vec3(2.18, 1.90, -1.15), vec3(0.060, 0.125, 0.030));\n"
"  float bags = min(bag1, bag2);\n"
"  if (bags < d) { d = bags; id = 13.0; }\n"
/* the line off the bag, down to the back of a hand */
"  float ivl = cap(p, vec3(1.92, 1.70, -1.15), vec3(1.05, 1.16, -0.30), 0.011);\n"
"  ivl = min(ivl, cap(p, vec3(1.05, 1.16, -0.30), vec3(0.45, 0.78, 0.30), 0.011));\n"
"  if (ivl < d) { d = ivl; id = 12.0; }\n"
"  float fig = person(p - vec3(-1.52, 0.00, -1.05));\n"
"  if (fig < d) { d = fig; id = 5.0; }\n"
/* --- and the body this is all attached to ---------------------------------
   You are in the bed, so the bed is not scenery in front of you: it is your
   own chest under a blanket, your arms lying on top of it, the back of your
   hand with a cannula taped to it, and the leads coming off you. Nothing
   else says woken up in one of these anything like as well, and a first
   person ending that does not show it is a camera in an empty room. */
"  vec3 bp = p - vec3(0.0, 0.56, 0.00);\n"
"  float blank = box(bp, vec3(0.60, 0.18 + 0.050*sin(p.x*5.0), 1.26));\n"
"  if (blank < d) { d = blank; id = 6.0; }\n"
/* the chest, above the blanket, in a gown */
"  float chest = smin(cap(p, vec3(0.0, 0.80, 1.12), vec3(0.0, 0.83, 0.56), 0.200),\n"
"                     cap(p, vec3(0.0, 0.83, 0.56), vec3(0.0, 0.79, 0.16), 0.185), 0.09);\n"
"  if (chest < d) { d = chest; id = 15.0; }\n"
/* two arms out along the blanket, and the hands at the end of them */
"  float arms = cap(p, vec3(-0.31, 0.79, 1.06), vec3(-0.41, 0.75, 0.34), 0.064);\n"
"  arms = min(arms, cap(p, vec3(0.31, 0.79, 1.06), vec3(0.43, 0.76, 0.34), 0.064));\n"
"  arms = smin(arms, length(p - vec3(-0.43, 0.75, 0.27)) - 0.077, 0.04);\n"
"  arms = smin(arms, length(p - vec3( 0.45, 0.76, 0.27)) - 0.077, 0.04);\n"
"  if (arms < d) { d = arms; id = 11.0; }\n"
/* three leads off the chest, up to the monitor, hanging as they go */
"  vec3 mhub = vec3(1.44, 1.42, -1.05);\n"
"  float lead = cap(p, vec3(-0.12, 0.95, 0.92), vec3(0.60, 0.80, 0.22), 0.016);\n"
"  lead = min(lead, cap(p, vec3(0.60, 0.80, 0.22), mhub, 0.016));\n"
"  lead = min(lead, cap(p, vec3(0.05, 0.98, 0.84), vec3(0.69, 0.86, 0.14), 0.016));\n"
"  lead = min(lead, cap(p, vec3(0.69, 0.86, 0.14), mhub, 0.016));\n"
"  lead = min(lead, cap(p, vec3(0.19, 0.94, 0.98), vec3(0.77, 0.92, 0.24), 0.016));\n"
"  lead = min(lead, cap(p, vec3(0.77, 0.92, 0.24), mhub, 0.016));\n"
"  if (lead < d) { d = lead; id = 12.0; }\n"
"  float door = box(p - vec3(1.30, 1.05, 4.16), vec3(0.55, 1.05, 0.05));\n"
"  if (door < d) { d = door; id = 7.0; }\n"
"  vec3 mo = p - vec3(1.62, 1.60, -1.25);\n"
"  float mbody = box(mo, vec3(0.31, 0.24, 0.075));\n"
"  if (mbody < d) { d = mbody; id = 8.0; }\n"
"  float mscr  = box(mo - vec3(0.0, 0.025, 0.080), vec3(0.265, 0.180, 0.006));\n"
"  if (mscr < d) { d = mscr; id = 9.0; }\n"
"  float mpole = cyl(p - vec3(1.62, 0.78, -1.25), 0.026, 0.80);\n"
"  if (mpole < d) { d = mpole; id = 4.0; }\n"
"  float rbob = sin(uTime * 10.5) * 0.22 * uRun * (1.0 - uRun);\n"
"  vec3 dp = p - vec3(mix(1.52, 1.24, uRun), rbob,\n"
"                     mix(3.85, -1.45, uRun));\n"
"  float doc = person(dp);\n"
"  if (doc < d) { d = doc; id = 10.0; }\n"
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
"    r = min(r, 11.0*d/t);\n"
"    t += clamp(d, 0.03, 0.35);\n"
"    if (t > dist) break; }\n"
"  return clamp(r, 0.0, 1.0); }\n"
"\n"
"float ao(vec3 p, vec3 n){ float o = 0.0, s = 1.0, id;\n"
"  for (int i = 1; i <= 5; i++){ float h = 0.05*float(i);\n"
"    o += (h - scene(p + n*h, id)) * s; s *= 0.6; }\n"
"  return clamp(1.0 - 2.4*o, 0.0, 1.0); }\n"
"\n"
"vec3 face_of(vec3 hp, vec3 body){\n"
"  float head  = smoothstep(0.150, 0.105, length(hp));\n"
"  vec3  c     = mix(body, vec3(0.78, 0.64, 0.56), head);\n"
"  float front = smoothstep(0.010, 0.055, hp.z);\n"
"  float e1 = smoothstep(0.030, 0.014, length(hp.xy - vec2(-0.045, 0.028)));\n"
"  float e2 = smoothstep(0.030, 0.014, length(hp.xy - vec2( 0.045, 0.028)));\n"
"  float br = smoothstep(0.016, 0.006, abs(hp.y - 0.062))\n"
"           * smoothstep(0.086, 0.052, abs(hp.x));\n"
"  float mo = smoothstep(0.014, 0.005, abs(hp.y + 0.052))\n"
"           * smoothstep(0.050, 0.026, abs(hp.x));\n"
"  float f  = clamp(e1 + e2 + br * 0.7 + mo * 0.8, 0.0, 1.0) * front * head;\n"
"  return mix(c, vec3(0.15, 0.12, 0.11), f); }\n"
"\n"
"vec3 albedo(float id, vec3 p, vec3 n){\n"
"  if (id < 1.5) {         // painted wall, vinyl floor, ceiling\n"
"    if (n.y > 0.9 && p.y < 0.2) {   // floor: warm vinyl with a tile seam\n"
"      vec2 g = fract(p.xz * 0.55) - 0.5;\n"
"      float seam = smoothstep(0.47, 0.5, max(abs(g.x), abs(g.y)));\n"
"      return mix(vec3(0.55, 0.50, 0.44), vec3(0.38, 0.34, 0.30), seam)\n"
"           * (0.92 + 0.16 * vn(p.xz * 7.0));\n"
"    }\n"
"    return vec3(0.80, 0.84, 0.80) * (0.94 + 0.09 * vn(p.zy * 3.0 + p.xx));\n"
"  }\n"
"  if (id < 2.5) return vec3(1.0);\n"
"  if (id < 3.5) return vec3(1.0);\n"
"  if (id < 4.5) return vec3(0.58, 0.60, 0.63);   // chrome\n"
/* A head the colour of a head, with a face on the front of it. Two figures
   stood in this room as featureless white lumps, and a lump is not a person
   -- in the photographs the first thing you read is that somebody is looking
   at you. Eyes, a brow and a mouth, on the half of the head that faces the
   bed, is the whole of what that takes at this distance. */
"  if (id < 5.5) {\n"
"    vec3 hp = p - vec3(-1.50, 1.34, -1.03);\n"
"    return face_of(hp, vec3(0.20, 0.24, 0.32));\n"
"  }\n"
"  if (id < 6.5) {                                // bedding, and it is not white\n"
"    float w = 0.5 + 0.5*sin(p.x * 14.0 + p.z * 2.0);\n"
"    return mix(vec3(0.74, 0.78, 0.80), vec3(0.58, 0.66, 0.73), w * 0.6);\n"
"  }\n"
"  if (id < 7.5) return vec3(0.42, 0.33, 0.24);   // the door\n"
"  if (id < 8.5) return vec3(0.11, 0.12, 0.14);   // the monitor case\n"
"  if (id < 9.5) return vec3(0.02, 0.03, 0.03);   // its screen, off\n"
"  if (id < 10.5) {\n"
"    vec3 lp = p - vec3(mix(1.52, 1.24, uRun), 0.0, mix(3.85, -1.45, uRun));\n"
"    float collar = smoothstep(1.00, 1.13, lp.y) * smoothstep(1.30, 1.13, lp.y)\n"
"                 * smoothstep(0.19, 0.06, abs(lp.x));\n"
"    float plack  = smoothstep(0.060, 0.022, abs(lp.x))\n"
"                 * smoothstep(1.04, 0.94, lp.y) * smoothstep(0.40, 0.58, lp.y);\n"
"    vec3 ca = mix(vec3(0.90, 0.91, 0.92), vec3(0.26, 0.55, 0.54),\n"
"                  clamp(collar + plack, 0.0, 1.0));\n"
"    return face_of(lp - vec3(0.02, 1.34, 0.02), ca);\n"
"  }\n"
"  if (id < 11.5) return vec3(0.78, 0.64, 0.56);  // skin\n"
"  if (id < 12.5) return vec3(0.10, 0.11, 0.13);  // the leads\n"
"  if (id < 13.5) return vec3(0.86, 0.90, 0.84);  // fluid\n"
"  if (id < 14.5) return vec3(0.42, 0.60, 0.52);  // the curtain, ward green\n"
"  if (id < 15.5) return vec3(0.55, 0.72, 0.70);  // the gown, ward teal\n"
"  return vec3(0.90, 0.91, 0.89);                 // the headwall\n"
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
"  float lid  = uOpen * 0.78 * (1.0 - 0.48 * uv.x * uv.x);\n"
"  float lash = smoothstep(lid, lid - 0.05, abs(uv.y + 0.02));\n"
"  if (lash <= 0.001) { FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
"\n"
"  // the eye is not steady yet: the view drifts and settles as uSharp rises\n"
"  vec2 drift = vec2(sin(uTime*0.7), cos(uTime*0.9)) * 0.012 * (1.0 - uSharp);\n"
"  vec3 ro = vec3(0.0, 1.26, 1.86);\n"
"  vec3 rd = normalize(vec3(uv.x + drift.x, uv.y + 0.14 + drift.y, -1.0) * vec3(1.05,1.05,1.0));\n"
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
"    if (id > 8.5 && id < 9.5) {\n"
/* The screen. A trace scrolling right to left with the cursor eating the
   old sweep ahead of it, the way every one of these has looked since the
   sixties -- and under it the two numbers anybody recognises. */
"      float su = (p.x - 1.62) / 0.265;\n"
"      float sv = (p.y - 1.625) / 0.180;\n"
"      float sweep = fract(uTime * 0.34);\n"
"      float sx    = (su * 0.5 + 0.5);\n"
"      float age   = fract(sx - sweep + 1.0);\n"
"      float w     = ecg(fract((sx - sweep) * 2.0 + 1.0)) * 0.46 + 0.24;\n"
"      float line  = smoothstep(0.075, 0.018, abs(sv - w));\n"
"      line *= smoothstep(0.02, 0.10, age) * smoothstep(1.0, 0.72, age);\n"
"      col = vec3(0.02, 0.05, 0.04);\n"
"      col += vec3(0.16, 1.70, 0.55) * line;\n"
/* a second trace below it, slower and blue -- the breathing one */
"      float w2 = sin((sx - sweep * 0.6) * 12.0) * 0.10 - 0.52;\n"
"      col += vec3(0.20, 0.55, 1.60)\n"
"           * smoothstep(0.050, 0.014, abs(sv - w2)) * 0.55;\n"
/* and the numbers, as blocks: nobody has to read them to know what they are */
"      if (su > 0.52 && su < 0.94) {\n"
"        float ny = sv * 3.0;\n"
"        float dg = step(0.55, fract(su * 7.0)) * step(0.35, fract(ny + 0.5));\n"
"        if (sv >  0.28 && sv <  0.72) col += vec3(0.16, 1.70, 0.55) * dg * 0.9;\n"
"        if (sv > -0.40 && sv < -0.02) col += vec3(0.20, 0.55, 1.60) * dg * 0.7;\n"
"      }\n"
"    } else if (id > 1.5 && id < 3.5) {\n"
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
/* Below the panel, not inside it. The fitting is a box spanning 2.755..2.845
   and the light was at 2.78 -- inside its own solid -- so every shadow ray
   toward it struck the panel from underneath and came back zero. The ceiling
   light in this room has never lit anything: the median value of its
   contribution over the whole frame was zero, the room was lit by a trickle
   of window and fill at about two per cent, and the grade was hauling that
   up to mid grey. Which is exactly what it looked like. */
"      vec3 pp = vec3(0.0, 2.70, -1.2);\n"
"      vec3 pl = pp - p; float pdist = length(pl); pl /= pdist;\n"
"      vec3 ceil_l = vec3(0.95, 0.96, 1.0)\n"
"                  * max(dot(n, pl), 0.0)\n""                  * mix(shadow(p, pl, pdist - 0.05), 1.0, 0.55)\n"
"                  / (1.0 + pdist*pdist*0.10);\n"
"      // sky fill so shadow cores stay alive\n"
"      vec3 fill = vec3(0.30, 0.33, 0.38) * (0.5 + 0.5*n.y);\n"
/* The fill was doing almost all of it, and a fill casts nothing -- so there
   was no shadow anywhere in the room and the whole frame lived inside a fifth
   of the range. Cut to a floor that keeps shadow cores from going black, with
   the panel and the window carrying the rest, and the room gets its darks. */
"      col = alb * (sun * 1.35 + ceil_l * 2.20 + fill * 0.11) * occ;\n"
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
"  col *= 1.70;\n"
"  col = mix(col, vec3(dot(col, vec3(0.333))), (1.0 - uSharp) * 0.55);\n"
"  col += (1.0 - uSharp) * 0.24 * vec3(0.9, 0.94, 1.0);\n"
/* uBright arrives as a flood and then lets go. Held at full it never stopped
   being a white wash, so the last thing the game shows -- the room, the
   monitor, the person who came running -- stayed behind a fog it had no
   reason to be behind. It comes in, and then the room is simply there. */
"  col = mix(col, vec3(1.0), uBright * 0.50);\n"
"  col = (col * (2.51*col + 0.03)) / (col * (2.43*col + 0.59) + 0.14);\n"
/* The room came out of the tone curve inside a fifth of the range, the same
   way the corridor did -- everything from the blanket to the coat landing
   between 0.6 and 0.7 and reading as one grey sheet behind glass. An S over
   the top gives it its blacks and its whites back, and the monitor stops
   being the only thing with any contrast in it. */
"  col = mix(col, col*col*(3.0 - 2.0*col), 0.30);\n"
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
"uniform float uHospY;    // the height the building is built at\n"
"uniform float uBlink;    // the heart, in the fluorescents\n"
"uniform float uCorrX;    // the corridor runs down this line\n"
/* WAKE_Z, fed rather than copied. This shader had 526 hardcoded in nine
   places from when WAKE_Z was GATE_END+46; it is GATE_END+66 now, so the
   corridor, the door, the lamp room and the end of the world were all being
   drawn twenty metres early -- at 514 m the rock said corridor wall where
   the game said open room, and the camera spawned inside it. Third time a
   constant drifted between the two fields this session; a uniform cannot. */
"uniform float uWakeZ;    // GATE_END + 66; CORR_Z is -20, LAMP_Z is +26\n"
/* What the ward is doing to the body, arriving in the place made out of it.
   uWard: somebody put the light on out there. uHand: somebody took hold. */
"uniform float uWard;\n"
"uniform float uHand;\n"
"uniform float uChoke;   // no air, and the walls come in with it\n"
"uniform float uDark;    // the ward went home\n"
"uniform float uRain;    // they are washing the body\n"
"uniform float uAlarm;   // the monitor is unhappy\n"
/* -proto N: draw one of the wrongness proposals so it can be looked at
   before anybody commits to it. 0 is the building as it ships. */
"uniform int   uProto;\n"
"uniform float uDoor;     // 1 once it is open\n"
"uniform float uLampOut;  // 1 once the bulb has gone\n"
"uniform int   uMonN;\n"
"uniform vec4  uMonP[6];  // xyz position, w kind (1,2,3)\n"
"uniform vec4  uMonD[6];  // xyz facing, w stun 0..1\n"

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
/* One stretch of the building, with soft ends. The whole point of a zone is
   that you cross into it: a treatment applied to all three hundred metres is
   not a zone, it is the building, and then nothing has changed. */
/* ---- 4단계 배치 ----------------------------------------------------------
 *
 * 익숙한 게 대부분이고 기괴한 것이 거기서 튀어나와야 무섭다. 기괴하기만 하면
 * 그냥 기괴한 건물이고, 두 번째 방부터는 아무것도 아니다. 그래서 302 m 가운데
 * 절반 넘게는 평범한 병동이고, 가구는 넓게 깔려 병원답게 만들고, 어긋남은
 * 드물게 한 번씩만 나온다.
 *
 * 사고 흔적은 특히 아껴 쓴다. 두 군데뿐이다 -- 한 번은 스키드 마크로 흘리고,
 * 한 번은 벽에서 전조등이 나온다. 차 사고로 여기 온 사람이니 이 건물이 그걸
 * 기억하고 있는 것이고, 기억은 자주 나지 않아야 기억이다.
 */
"float zone(float dep, float a, float b){\n"
"  return smoothstep(a - 5.0, a, dep) * (1.0 - smoothstep(b, b + 5.0, dep)); }\n"
/* 어디에 무엇이 있는지. -proto N 은 이 배치를 무시하고 하나만 켠다. */
"float lay(int n, float dep){\n"
"  if (uProto == n) return 1.0;\n"
"  if (uProto != 0) return 0.0;\n"
/* 가구 -- 넓게, 병원답게 */
"  if (n == 12) return zone(dep, 268.0, 286.0);\n"        /* O-1 의자 */
"  if (n == 21) return zone(dep, 356.0, 364.0);\n"        /* O-6 처치 카트 */
"  if (n == 22) return zone(dep, 306.0, 312.0);\n"        /* O-7 린넨 카트 */
"  if (n == 23) return zone(dep, 452.0, 458.0);\n"        /* O-8 청소 카트 */
"  if (n == 25) return max(zone(dep, 296.0, 302.0),\n"    /* O-5 휠체어 */
"                         zone(dep, 424.0, 430.0));\n"
"  if (n == 26) return max(zone(dep, 258.0, 266.0),\n"    /* O-12 게시판 */
"                         zone(dep, 388.0, 396.0));\n"
"  if (n == 28) return zone(dep, 466.0, 472.0);\n"        /* O-16 폐기물통 */
"  if (n == 29) return zone(dep, 360.0, 368.0);\n"        /* O-17 정수기 */
"  if (n == 10) return 1.0;\n"                            /* O-11 명패 -- 길찾기 */
"  if (n == 11) return 1.0;\n"                            /* O-9/10/14 벽 설비 */
/* 병동 -- 각각 한 구역 */
"  if (n == 46) return zone(dep, 368.0, 382.0);\n"        /* W-8 외래 대기 */
"  if (n == 16) return zone(dep, 446.0, 460.0);\n"        /* W-4 수술부 */
"  if (n == 17) return zone(dep, 488.0, 500.0);\n"        /* W-5 영안실 */
/* 어긋남 -- 드물게 */
"  if (n == 34) return zone(dep, 320.0, 334.0);\n"        /* SC-4 문 없는 복도 */
"  if (n == 33) return zone(dep, 414.0, 424.0);\n"        /* SC-2 내려앉은 천장 */
"  if (n == 36) return zone(dep, 502.0, 508.0);\n"        /* PL-4 벽이 된 복도 */
"  if (n == 39) return zone(dep, 480.0, 486.0);\n"        /* CT-4 여덟 EXIT */
"  if (n == 42) return 1.0;\n"                            /* LT-1 -- 등 하나뿐 */
/* 사고 -- 두 군데 */
"  if (n == 50) return zone(dep, 390.0, 400.0);\n"        /* 스키드 마크 */
"  if (n == 51) return zone(dep, 436.0, 446.0);\n"        /* 전조등·가드레일·도로 */
"  return 0.0; }\n"
/* boxes and posts, for the furniture proposals */
"float bx(vec3 p, vec3 b){ vec3 q = abs(p) - b;\n"
"  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }\n"
"float pst(vec3 p, float r, float h){\n"
"  vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);\n"
"  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); }\n"
"float segd(vec3 p, vec3 a, vec3 b){ vec3 pa = p-a, ba = b-a;\n"
"  float t = clamp(dot(pa,ba)/max(dot(ba,ba),1e-6), 0.0, 1.0);\n"
"  return length(pa - ba*t); }\n"
/* the same perpendicular under a Chebyshev norm: a square bore */
"float segc(vec3 p, vec3 a, vec3 b){ vec3 pa = p-a, ba = b-a;\n"
"  float t = clamp(dot(pa,ba)/max(dot(ba,ba),1e-6), 0.0, 1.0);\n"
"  vec3 e = abs(pa - ba*t);\n"
"  return max(e.x, max(e.y, e.z)); }\n"
"\n"
/* The same rooms game.c walks through. Both have to agree or you can see
   through a wall you cannot pass, so this is a transcription, not a
   second design. */
"bool openN(float cx, float cz){\n"
/* the first rows always let you in -- see cell_open_n in game.c */
"  if (cz * 7.0 > -266.0) return true;\n"
"  return h1(cx * 13.31 + cz * 7.77 + uSeed) < 0.62; }\n"
"bool openW(float cx, float cz){\n"
"  float h = h1(cx * 5.19 - cz * 11.03 + uSeed * 3.0);\n"
"  if (h < 0.34) return true;\n"
"  return !openN(cx, cz); }\n"
"float roomsAir(vec3 p, float floorY){\n"
"  float air = 1.55 - abs(p.y - floorY - 1.30);\n"
/* A doorway belongs to the cell the point is in, so the -z walls take their
   x from cx alone and vary over the two z-edges, and the -x walls take their
   z from cz alone. Running both over both neighbours applied each wall twice,
   once carrying a doorway seven metres away, and the second copy sealed the
   first -- see rooms_air in game.c. */
"  float cx = floor(p.x / 7.0), cz = floor(p.z / 7.0);\n"
"  for (int k = 0; k <= 1; k++) {\n"
"    float az = cz + float(k);\n"
"    float bz = az * 7.0, bx = cx * 7.0;\n"
"    float d1 = abs(p.z - bz) - 0.22;\n"
"    float o1 = h1(cx * 3.7 + az * 9.1 + uSeed) - 0.5;\n"
"    float c1 = bx + 3.5 + o1 * (7.0 - 1.35 * 2.4);\n"
"    if (!openN(cx, az) || abs(p.x - c1) > 1.35) air = min(air, d1);\n"
"  }\n"
"  for (int k = 0; k <= 1; k++) {\n"
"    float ax = cx + float(k);\n"
"    float bx = ax * 7.0, bz = cz * 7.0;\n"
"    float d2 = abs(p.x - bx) - 0.22;\n"
"    float o2 = h1(ax * 8.3 - cz * 2.9 + uSeed) - 0.5;\n"
"    float c2 = bz + 3.5 + o2 * (7.0 - 1.35 * 2.4);\n"
"    if (!openW(ax, cz) || abs(p.z - c2) > 1.35) air = min(air, d2);\n"
"  }\n"
"  return air; }\n"
/* One of the things, as a distance. A low body slung between eight legs,
   a head carried out in front on a neck, and the legs themselves as two
   segments with a knee above the body - the same animal the point cloud
   describes, solid enough for light to land on. */
"float sdCap(vec3 p, vec3 a, vec3 b, float r){\n"
"  vec3 pa = p - a, ba = b - a;\n"
"  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);\n"
"  return length(pa - ba * h) - r; }\n"
"float smin2(float a, float b, float k){\n"
"  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);\n"
"  return mix(b, a, h) - k * h * (1.0 - h); }\n"
/* A cone rather than a capsule: a leg that does not taper reads as a
   sausage, which is the single thing that kept the first version from
   being unpleasant to look at. */
"float sdTaper(vec3 p, vec3 a, vec3 b, float ra, float rb){\n"
"  vec3 pa = p - a, ba = b - a;\n"
"  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);\n"
"  return length(pa - ba * h) - mix(ra, rb, h); }\n"

/* Two body segments, not one. A spider is a flat plated cephalothorax
   with a swollen abdomen slung behind it, and the join between them is
   thin - that waist is most of the silhouette. Legs get four visible
   segments, the knee carried above the back, and they end in points.
   Nothing is symmetric: every leg takes its length, its thickness and
   its timing from its own index. */
"float beast(vec3 q, float kind, float t){\n"
"  float sc  = kind < 1.5 ? 0.24 : (kind < 2.5 ? 0.52 : 0.34);\n"
"  float leg = kind < 1.5 ? 1.30 : (kind < 2.5 ? 0.80 : 2.60);\n"
"  q /= sc;\n"
"  // abdomen: swollen, hung behind and a little high\n"
"  vec3 ab = q - vec3(0.0, 0.08, 0.52);\n"
"  float d = length(ab * vec3(1.05, 1.25, 0.86)) - 0.40;\n"
"  // cephalothorax: flatter, wider than tall, plated\n"
"  vec3 cf = q - vec3(0.0, -0.02, -0.30);\n"
"  float ce = length(cf * vec3(0.92, 1.55, 1.05)) - 0.40;\n"
"  d = smin2(d, ce, 0.05);\n"
"  // the waist between them is thin enough to see through\n"
"  d = smin2(d, sdTaper(q, vec3(0.0, 0.02, 0.16),\n"
"                          vec3(0.0, 0.06, 0.34), 0.13, 0.17), 0.07);\n"
"  // chelicerae: a pair of fangs slung under the front, and the palps\n"
"  d = min(d, sdTaper(q, vec3(-0.13, -0.16, -0.62),\n"
"                        vec3(-0.07, -0.40, -0.80), 0.09, 0.008));\n"
"  d = min(d, sdTaper(q, vec3( 0.13, -0.16, -0.62),\n"
"                        vec3( 0.06, -0.41, -0.78), 0.09, 0.008));\n"
"  d = min(d, sdTaper(q, vec3(-0.22, -0.06, -0.58),\n"
"                        vec3(-0.40, -0.26, -0.90), 0.07, 0.02));\n"
"  d = min(d, sdTaper(q, vec3( 0.22, -0.06, -0.58),\n"
"                        vec3( 0.43, -0.22, -0.86), 0.07, 0.02));\n"
"  // eight eyes, in two rows, none of them the same size\n"
"  for (int e = 0; e < 4; e++) {\n"
"    float fe = float(e);\n"
"    float ex = 0.06 + fe * 0.075;\n"
"    float ey = -0.14 + mod(fe, 2.0) * 0.11;\n"
"    float er = 0.036 + 0.020 * h1(fe * 3.1);\n"
"    d = min(d, length(q - vec3( ex, ey, -0.62)) - er);\n"
"    d = min(d, length(q - vec3(-ex, ey, -0.62)) - er);\n"
"  }\n"
"  for (int i = 0; i < 4; i++) {\n"
"    float fi = float(i);\n"
"    float a  = 0.42 + fi * 0.60;\n"
"    float ph = t * 5.5 + fi * 1.9;\n"
"    // no two legs the same: length, girth and phase from the index\n"
"    float ll = leg * (0.82 + 0.36 * h1(fi * 7.7));\n"
"    float gr = 0.075 + 0.038 * h1(fi * 4.3 + 1.0);\n"
"    for (int sgn = 0; sgn < 2; sgn++) {\n"
"      float sx = (sgn == 0) ? 1.0 : -1.0;\n"
"      float sk = 0.86 + 0.28 * h1(fi * 2.9 + (sgn == 0 ? 0.0 : 5.0));\n"
"      vec3 hip = vec3(sx * 0.26, -0.02, -0.30 + fi * 0.26);\n"
"      // the knee goes up over the back - the arch is the spider read\n"
"      vec3 kne = hip + vec3(sx * cos(a) * 0.62 * ll * sk, 1.16 * sk,\n"
"                            sin(a) * 0.26 * ll);\n"
"      vec3 ank = kne + vec3(sx * cos(a) * 0.86 * ll * sk, -1.35 * sk,\n"
"                            sin(a) * 0.34 * ll + 0.10 * sin(ph));\n"
"      vec3 toe = ank + vec3(sx * cos(a) * 0.20 * ll, -0.62 * sk,\n"
"                            sin(a) * 0.14 * ll + 0.07 * sin(ph));\n"
"      d = min(d, sdTaper(q, hip, kne, gr * 1.5, gr));\n"
"      d = min(d, sdTaper(q, kne, ank, gr, gr * 0.55));\n"
/* a tip thinner than the march threshold simply is not there, and the
   first render came back with every leg cut off at the ankle */
"      d = min(d, sdTaper(q, ank, toe, gr * 0.55, 0.022));\n"
"    }\n"
"  }\n"
"  return d * sc * 0.9; }\n"
"float beasts(vec3 p, out float kind, out float stun){\n"
"  float best = 1e9; kind = 0.0; stun = 0.0;\n"
"  for (int i = 0; i < 6; i++) {\n"
"    if (i >= uMonN) break;\n"
"    vec3  c  = uMonP[i].xyz;\n"
"    vec3  f  = normalize(vec3(uMonD[i].x, 0.0, uMonD[i].z) + vec3(1e-5));\n"
"    vec3  r  = vec3(-f.z, 0.0, f.x);\n"
"    vec3  q  = p - c;\n"
"    vec3  lq = vec3(dot(q, r), q.y, dot(q, f));\n"
"    float d  = beast(lq, uMonP[i].w, uTime + float(i));\n"
"    if (d < best) { best = d; kind = uMonP[i].w; stun = uMonD[i].w; }\n"
"  }\n"
"  return best; }\n"
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
"  float st2  = smoothstep(244.0, 259.0, -p.z);\n"
"  float cyh  = mix(c.y, uHospY, st2);\n"
"  float boxm = roomsAir(p, cyh - 1.35);\n"
/* ---- 시안: 지오메트리를 바꾸는 것들 -------------------------------------
   셰이더 필드에만 들어 있다. 채택하면 cave_sdf에도 넣고 mazecheck를 다시
   돌려야 한다 -- 천장을 내리고 벽을 세우는 것은 통과 가능성을 바꾼다. */
"  if (lay(33, -p.z) > 0.02) {\n"
/* SC-2 내려앉은 천장 — 한 구역만 1.55 m에서 0.80 m로 */
"    float zb = zone(-p.z, 524.0, 538.0);\n"
"    float low = 0.80 - abs(p.y - (cyh - 1.35) - 0.62);\n"
"    boxm = min(boxm, mix(9.0, low, zb));\n"
"  }\n"
"  if (lay(34, -p.z) > 0.02) {\n"
/* SC-4 끝이 안 보이는 복도 — 가로벽도 문도 없는 한 줄 */
"    float zb = zone(-p.z, 518.0, 548.0);\n"
"    float hall = min(2.6 - abs(p.x - uCorrX), 1.55 - abs(p.y - (cyh - 1.35) - 1.30));\n"
"    boxm = mix(boxm, hall, zb);\n"
"  }\n"
"  if (lay(36, -p.z) > 0.02) {\n"
/* PL-4 벽이 된 복도 — 다 갖춘 복도가 아무 표시 없이 끝난다 */
"    if (-p.z > 536.0 && -p.z < 536.5) boxm = min(boxm, abs(-p.z - 536.25) - 0.25);\n"
"  }\n"
"  float dep = -p.z;\n"
/* The T at the end of the rooms, matching cave_sdf: a cross-corridor the
   whole maze can reach, running into the one that goes to the door. */
"  if (dep > uWakeZ - 23.0) {\n"
"    float fl2 = cyh - 1.35;\n"
"    float ch  = 1.55 - abs(p.y - fl2 - 1.30);\n"
"    float cor = min(1.9 - abs(p.x - uCorrX), ch);\n"
"    float lat = min(1.9 - abs(dep - (uWakeZ - 20.0)), ch);\n"
"    boxm = max(boxm, lat);\n"
"    boxm = max(boxm, cor);\n"
"    boxm = mix(boxm, cor, smoothstep(uWakeZ - 17.0, uWakeZ - 11.0, dep));\n"
"  }\n"
"  if (dep > uWakeZ + 1.2) {\n"
"    float fl3 = cyh - 1.35;\n"
"    boxm = min(min(15.0 - abs(p.x - uCorrX),\n"
"                   1.75 - abs(p.y - fl3 - 1.45)), (uWakeZ + 40.0) - dep);\n"
"  }\n"
"  m = mix(m, boxm, uRoom);\n"
/* ---- 2차 시안: 바닥에 서는 것들 -----------------------------------------
   Visual only -- these live in the shader field and not in cave_sdf, so you
   walk through them. That is the right trade for a proposal: it costs one
   function to look at, and nothing has to be re-verified until somebody
   decides to keep it. If one is kept it goes into both fields and mazecheck
   runs again, because a chair in a corridor is a chair you can be trapped by. */
/* 바닥에 서는 나머지. 전부 셰이더 필드에만 — 통과해서 걸어진다. */
"  if (uProto == 35) {\n"
/* PL-3 바닥에서 자란 침대 — 시트가 장판과 이어져 경계가 없다 */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 3.5, 7.0) - 3.5;\n"
"    vec3  c  = vec3(uCorrX - 1.05, fy + 0.10, 0.0);\n"
"    float bd = bx(q - c, vec3(0.40, 0.20, 0.95));\n"
"    bd = smin2(bd, q.y - fy, 0.34);\n"
"    m = min(m, bd);\n"
"  }\n"
"  if (uProto == 44) {\n"
/* W-6 신생아실 — 통유리 너머 빈 아기침대가 줄지어 */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 0.85, 1.7) - 0.85;\n"
"    vec3  c  = vec3(uCorrX + 1.95, fy + 0.62, 0.0);\n"
"    float ct = bx(q - c, vec3(0.26, 0.16, 0.32));\n"
"    ct = min(ct, pst(q - c - vec3(0.20, -0.34, 0.24), 0.020, 0.30));\n"
"    ct = min(ct, pst(q - c - vec3(-0.20, -0.34, 0.24), 0.020, 0.30));\n"
"    float glass = max(abs(p.x - uCorrX - 1.10) - 0.02,\n"
"                      abs(p.y - fy - 1.10) - 0.70);\n"
"    m = min(m, min(ct, glass));\n"
"  }\n"
"  if (uProto == 45) {\n"
/* W-7 재활치료실 — 평행봉과 매트 */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p;\n"
"    vec3  c  = vec3(uCorrX, fy + 0.05, 0.0);\n"
"    float mat = bx(q - c, vec3(0.70, 0.05, 2.2));\n"
"    float bar = pst(vec3(q.z, q.y, q.x) - vec3(0.0, fy + 0.92, uCorrX + 0.46),\n"
"                    0.026, 2.0);\n"
"    bar = min(bar, pst(vec3(q.z, q.y, q.x) - vec3(0.0, fy + 0.92, uCorrX - 0.46),\n"
"                       0.026, 2.0));\n"
"    vec3 qp = q; qp.z = mod(p.z + 0.9, 1.8) - 0.9;\n"
"    bar = min(bar, pst(qp - vec3(uCorrX + 0.46, fy + 0.46, 0.0), 0.030, 0.46));\n"
"    bar = min(bar, pst(qp - vec3(uCorrX - 0.46, fy + 0.46, 0.0), 0.030, 0.46));\n"
"    m = min(m, min(mat, bar));\n"
"  }\n"
"  if (lay(46, -p.z) > 0.02) {\n"
/* W-8 외래 대기 — 의자 줄과 접수 카운터 */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 0.32, 0.64) - 0.32;\n"
"    vec3  c  = vec3(uCorrX - 1.44, fy + 0.44, 0.0);\n"
"    float ch = bx(q - c, vec3(0.25, 0.032, 0.25));\n"
"    ch = min(ch, bx(q - c - vec3(-0.21, 0.26, 0.0), vec3(0.030, 0.25, 0.25)));\n"
"    ch = min(ch, bx(q - c - vec3(0.0, -0.23, 0.0), vec3(0.19, 0.21, 0.025)));\n"
"    float cnt = bx(p - vec3(uCorrX + 1.30, fy + 0.55, -534.0), vec3(0.45, 0.55, 1.6));\n"
"    cnt = min(cnt, bx(p - vec3(uCorrX + 1.10, fy + 1.14, -534.0), vec3(0.62, 0.05, 1.7)));\n"
"    m = min(m, min(ch, cnt));\n"
"  }\n"
"  if (lay(21,-p.z) + lay(22,-p.z) + lay(23,-p.z) + lay(25,-p.z)\n"
"    + lay(28,-p.z) + lay(29,-p.z) > 0.02) {\n"
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 3.5, 7.0) - 3.5;\n"
"    float o  = 1e9;\n"
"    if (lay(21, -p.z) > 0.02) {\n"
/* O-6 처치 카트 — 서랍 한 칸이 열려 있다 */
"      vec3 c = vec3(uCorrX - 1.30, fy + 0.46, 0.0);\n"
"      o = bx(q - c, vec3(0.24, 0.36, 0.19));\n"
"      o = min(o, bx(q - c - vec3(0.0, 0.42, 0.0), vec3(0.27, 0.022, 0.22)));\n"
"      o = min(o, bx(q - c - vec3(0.0, 0.06, 0.26), vec3(0.22, 0.075, 0.09)));\n"
"      o = min(o, pst(q - c - vec3(0.19, -0.42, 0.14), 0.035, 0.035));\n"
"      o = min(o, pst(q - c - vec3(-0.19, -0.42, 0.14), 0.035, 0.035));\n"
"    } else if (lay(22,-p.z) > 0.02) {\n"
/* O-7 린넨 카트 — 수건이 쌓여 넘친다 */
"      vec3 c = vec3(uCorrX - 1.32, fy + 0.40, 0.0);\n"
"      o = bx(q - c, vec3(0.30, 0.30, 0.22));\n"
"      o = min(o, bx(q - c - vec3(0.0, 0.34, 0.0), vec3(0.28, 0.10, 0.20)));\n"
"      o = min(o, bx(q - c - vec3(0.06, 0.44, 0.03), vec3(0.20, 0.06, 0.15)));\n"
"    } else if (lay(23,-p.z) > 0.02) {\n"
/* O-8 청소 카트와 미끄럼 주의 표지판 */
"      vec3 c = vec3(uCorrX - 1.25, fy + 0.30, 0.0);\n"
"      o = pst(q - c, 0.16, 0.24);\n"
"      o = min(o, pst(q - c - vec3(0.0, 0.62, 0.0), 0.022, 0.42));\n"
"      vec3 sg = vec3(uCorrX + 0.10, fy + 0.30, 1.2);\n"
"      float s1 = bx(q - sg - vec3(0.0, 0.0, -0.10), vec3(0.18, 0.30, 0.012));\n"
"      float s2 = bx(q - sg - vec3(0.0, 0.0,  0.10), vec3(0.18, 0.30, 0.012));\n"
"      o = min(o, min(s1, s2));\n"
"    } else if (uProto == 24) {\n"
/* O-3 세워둔 이송침대 — 벽에 바짝 */
"      vec3 c = vec3(uCorrX - 1.42, fy + 0.72, 0.0);\n"
"      o = bx(q - c, vec3(0.11, 0.86, 0.36));\n"
"      o = min(o, bx(q - c - vec3(0.07, 0.0, 0.0), vec3(0.075, 0.80, 0.30)));\n"
"      o = min(o, pst(vec3(q.y, q.x, q.z) - vec3(fy + 0.06, uCorrX - 1.34, 0.28),\n"
"                     0.055, 0.020));\n"
"      o = min(o, pst(vec3(q.y, q.x, q.z) - vec3(fy + 0.06, uCorrX - 1.34, -0.28),\n"
"                     0.055, 0.020));\n"
"    } else if (lay(25,-p.z) > 0.02) {\n"
/* O-5 휠체어 — 복도를 향해 돌려진 채 */
"      vec3 c = vec3(uCorrX - 0.85, fy + 0.48, 0.0);\n"
"      o = bx(q - c, vec3(0.23, 0.032, 0.22));\n"
"      o = min(o, bx(q - c - vec3(-0.20, 0.30, 0.0), vec3(0.032, 0.28, 0.22)));\n"
"      o = min(o, bx(q - c - vec3(0.24, 0.14, 0.0), vec3(0.10, 0.028, 0.17)));\n"
"      o = min(o, pst(vec3(q.y, q.x, q.z) - vec3(c.y - 0.20, c.x, c.z + 0.26),\n"
"                     0.26, 0.022));\n"
"      o = min(o, pst(vec3(q.y, q.x, q.z) - vec3(c.y - 0.20, c.x, c.z - 0.26),\n"
"                     0.26, 0.022));\n"
"      o = min(o, pst(q - c - vec3(-0.22, 0.62, 0.24), 0.020, 0.26));\n"
"      o = min(o, pst(q - c - vec3(-0.22, 0.62, -0.24), 0.020, 0.26));\n"
"    } else if (lay(28,-p.z) > 0.02) {\n"
/* O-16 의료폐기물 통 — 노란 통, 가득 */
"      vec3 c = vec3(uCorrX - 1.44, fy + 0.34, 0.0);\n"
"      o = pst(q - c, 0.20, 0.34);\n"
"      o = min(o, pst(q - c - vec3(0.0, 0.37, 0.0), 0.21, 0.028));\n"
"    } else if (lay(29,-p.z) > 0.02) {\n"
/* O-17 정수기 — 복도 끝의 미끼 */
"      vec3 c = vec3(uCorrX - 1.40, fy + 0.55, 0.0);\n"
"      o = bx(q - c, vec3(0.16, 0.55, 0.19));\n"
"      o = min(o, pst(q - c - vec3(0.0, 0.78, 0.0), 0.145, 0.24));\n"
"      o = min(o, bx(q - c - vec3(0.17, 0.10, 0.0), vec3(0.045, 0.05, 0.05)));\n"
"    } else if (lay(30,-p.z) > 0.02) {\n"
/* O-18 보호자 간이침대 — 담요가 개어져 있고 그 위에 책이 엎어져 */
"      vec3 c = vec3(uCorrX - 1.05, fy + 0.24, 0.0);\n"
"      o = bx(q - c, vec3(0.34, 0.075, 0.92));\n"
"      o = min(o, bx(q - c - vec3(0.0, -0.14, 0.0), vec3(0.28, 0.13, 0.84)));\n"
"      o = min(o, bx(q - c - vec3(0.0, 0.13, 0.34), vec3(0.30, 0.075, 0.26)));\n"
"      o = min(o, bx(q - c - vec3(0.04, 0.22, 0.30), vec3(0.11, 0.014, 0.08)));\n"
"    } else if (lay(31,-p.z) > 0.02) {\n"
/* O-19 이동형 X선 — 복도를 좁힌다 */
"      vec3 c = vec3(uCorrX - 0.75, fy + 0.55, 0.0);\n"
"      o = bx(q - c, vec3(0.30, 0.55, 0.28));\n"
"      o = min(o, pst(q - c - vec3(0.0, 0.92, 0.0), 0.055, 0.40));\n"
"      o = min(o, bx(q - c - vec3(0.34, 1.28, 0.0), vec3(0.36, 0.050, 0.055)));\n"
"      o = min(o, bx(q - c - vec3(0.66, 1.16, 0.0), vec3(0.13, 0.11, 0.13)));\n"
"    }\n"
"    m = min(m, o);\n"
"  }\n"
"  if (lay(12, -p.z) > 0.02) {\n"
/* O-1 연결 대기 의자 — a three-seat run against the wall, repeated so it goes
   the length of the corridor: the CT-3 version of the same object. */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 0.32, 0.64) - 0.32;\n"
"    vec3  c  = vec3(uCorrX - 1.44, fy + 0.44, 0.0);\n"
"    float ch = bx(q - c, vec3(0.25, 0.032, 0.25));\n"
"    ch = min(ch, bx(q - c - vec3(-0.21, 0.26, 0.0), vec3(0.030, 0.25, 0.25)));\n"
"    ch = min(ch, bx(q - c - vec3(0.0, -0.23, 0.0), vec3(0.19, 0.21, 0.025)));\n"
"    m = min(m, ch);\n"
"  }\n"
"  if (uProto == 13) {\n"
/* O-2 빈 병상 — one to a bay, made up, rails raised. */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 3.5, 7.0) - 3.5;\n"
"    vec3  c  = vec3(uCorrX - 1.15, fy + 0.56, 0.0);\n"
"    float bd = bx(q - c, vec3(0.42, 0.085, 0.98));\n"
"    bd = min(bd, bx(q - c - vec3(0.0, -0.30, 0.0), vec3(0.34, 0.22, 0.86)));\n"
"    bd = min(bd, bx(q - c - vec3(0.0, 0.30, 0.92), vec3(0.40, 0.30, 0.035)));\n"
"    bd = min(bd, bx(q - c - vec3(0.44, 0.22, 0.30), vec3(0.028, 0.16, 0.42)));\n"
"    bd = min(bd, bx(q - c - vec3(-0.44, 0.22, 0.30), vec3(0.028, 0.16, 0.42)));\n"
"    m = min(m, bd);\n"
"  }\n"
"  if (uProto == 14) {\n"
/* O-4 링거대 and O-5 휠체어, the two things that are always in a corridor */
"    float fy = cyh - 1.60;\n"
"    vec3  q  = p; q.z = mod(p.z + 3.5, 7.0) - 3.5;\n"
"    vec3  sc = vec3(uCorrX + 1.30, fy + 0.80, 0.55);\n"
"    float st = pst(q - sc, 0.020, 0.80);\n"
"    st = min(st, bx(q - sc - vec3(0.0, 0.82, 0.0), vec3(0.14, 0.016, 0.016)));\n"
"    st = min(st, bx(q - sc - vec3(-0.11, 0.66, 0.0), vec3(0.055, 0.13, 0.028)));\n"
"    st = min(st, pst(q - sc - vec3(0.0, -0.78, 0.0), 0.17, 0.012));\n"
"    vec3  wc = vec3(uCorrX + 1.16, fy + 0.46, -0.85);\n"
"    float wh = bx(q - wc, vec3(0.22, 0.030, 0.21));\n"
"    wh = min(wh, bx(q - wc - vec3(0.0, 0.28, -0.19), vec3(0.21, 0.26, 0.030)));\n"
"    wh = min(wh, pst(vec3(q.y, q.x, q.z) - vec3(wc.y - 0.18, wc.x + 0.24, wc.z),\n"
"                     0.24, 0.020));\n"
"    wh = min(wh, pst(vec3(q.y, q.x, q.z) - vec3(wc.y - 0.18, wc.x - 0.24, wc.z),\n"
"                     0.24, 0.020));\n"
"    m = min(m, min(st, wh));\n"
"  }\n"

/* 526, matching WAKE_Z in cave_sdf. It said 525, so the end wall was drawn a
   metre nearer than the one you actually stop against. */
"  if (uRoad < 0.5) m = min(m, p.z + (uWakeZ + 40.0));\n"
"  if (dep > uWakeZ - 0.4 && dep < uWakeZ + 0.4) {\n"
"    float leaf = abs(dep - uWakeZ) - 0.18;\n"
"    float gx2  = 1.05 - abs(p.x - uCorrX);\n"
"    float d3   = (uDoor > 0.5 && gx2 > 0.0) ? 1000.0 : leaf;\n"
"    if (gx2 < 0.0) d3 = 1000.0;\n"
"    m = min(m, d3);\n"
"  }\n"
"  int i0 = int(clamp(-p.z/34.0, 0.0, 14.0));\n"
/* narrowed where the corridor takes over -- mirrors branch_air */
"  float brk = 1.75 * (1.0 - 0.42 * smoothstep(368.0, 476.0, -p.z));\n"
/* square section once the walls are square -- see branch_air */
"  float sq = smoothstep(244.0, 270.0, -p.z);\n"
"  m = max(m, brk - mix(segd(p, uBrA[i0], uBrB[i0]),\n"
"                       segc(p, uBrA[i0], uBrB[i0]), sq));\n"
"  m = max(m, brk - mix(segd(p, uBrA[i0+1], uBrB[i0+1]),\n"
"                       segc(p, uBrA[i0+1], uBrB[i0+1]), sq));\n"
"  return m; }\n"
"\n"
/* field() is the building; beasts() are in it. Both are distances to the
   nearest surface, so the world is the nearer of the two - a min. The
   first version combined them with a max, which is the union of the two
   solids rather than of their surfaces, and drew nothing at all. */
"float worldD(vec3 p){\n"
"  float d = field(p);\n"
"  if (uMonN > 0) { float k2, s2; float dm = beasts(p, k2, s2);\n"
"    if (dm < d) d = dm; }\n"
"  return d; }\n"
"vec3 fnorm(vec3 p){ vec2 e = vec2(0.012, 0.0);\n"
"  return normalize(vec3(worldD(p+e.xyy)-worldD(p-e.xyy),\n"
"                        worldD(p+e.yxy)-worldD(p-e.yxy),\n"
"                        worldD(p+e.yyx)-worldD(p-e.yyx))); }\n"
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
/* Over-relaxed sphere tracing.

   The field lies: the row walls carve their doorways with a hard
   conditional and rooms union with a max, so both hand back a distance
   larger than the true one. Marching a fixed fraction of it walks into
   walls, and on a wall seen edge-on that reads as the wall bulging toward
   you - the back half of the game looked like it was melting. Capping the
   step hid part of it and cost more: the setting that actually flattened
   the wall ran at 82 ms a frame against 34.

   So rather than trust the distance less, take a bolder step and check it.
   If the sphere at the new point does not reach back to the old one, the
   step jumped a surface; undo it and retry unrelaxed. Overshoot is caught
   instead of avoided, so the ray keeps its reach across open floor and
   still cannot pass through a wall. (Keinert et al., Enhanced Sphere
   Tracing.) The variable is not called step because GLSL already has one.
*/
"  float om = 1.20, prev = 1e9, adv = 0.0;\n"
"  for (int i = 0; i < 200; i++){\n"
"    float d = worldD(uCam + rd*t);\n"
"    bool over = (om > 1.0) && (d + prev) < adv;\n"
"    if (over) { adv -= om * adv; om = 1.0; }\n"
"    else {\n"
"      if (d < 0.006) { hit = true; break; }\n"
"      adv  = d * om;\n"
"      prev = d;\n"
"    }\n"
"    t += adv;\n"
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
"  float onBeast = 0.0, beastKind = 0.0, beastStun = 0.0;\n"
"  if (uMonN > 0) {\n"
"    float dm = beasts(p, beastKind, beastStun);\n"
"    if (dm < 0.045) onBeast = 1.0;\n"
"  }\n"
/* the last thing the corridor shows you: a door, darker than its wall */
"  if (uRoad < 0.5 && p.z < -523.9) {\n"
"    vec2 dc = centre(p.z);\n"
"    if (abs(p.x - dc.x) < 0.62 && p.y - dc.y < 0.75 && p.y - dc.y > -1.55)\n"
"      alb = vec3(0.24, 0.20, 0.16);\n"
"  }\n"
"  alb = mix(alb, vec3(0.30,0.34,0.38), uWet*0.55);\n"
/* built surfaces are pale and even; the grain fades with the rock.
   Three materials, not one. Every plane in here was the same beige with a
   brightness multiplier on it, and a brightness multiplier is not a
   material -- it is the same paint under more light. A corridor is grey-green
   vinyl underfoot, warm painted plaster at the sides and near-white mineral
   fibre overhead, and those do not sit on the same hue at all. */
"  float m_flr = smoothstep(0.48, 0.86,  n.y);\n"
"  float m_cei = smoothstep(0.48, 0.86, -n.y);\n"
"  {\n"
/* Colder, and a little green. Warm beige is a house; a corridor nobody has
   walked down in two weeks is lit by tubes, and tubes are not warm. This is
   most of what separates "a room" from "somewhere you should not still be". */
"    vec3 rmat = vec3(0.760, 0.775, 0.735);\n"                 /* wall paint */
"    rmat = mix(rmat, vec3(0.372, 0.400, 0.378), m_flr);\n"   /* vinyl */
"    rmat = mix(rmat, vec3(0.880, 0.900, 0.870), m_cei);\n"   /* ceiling tile */
"    alb = mix(alb, rmat, uRoom * 0.9);\n"
"  }\n"
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
/* Softer than it was, because the three materials above now carry most of
   this. Left at the old strength on top of them the floor went to mud. */
"  alb *= mix(1.0, 0.94 - 0.11*clamp(n.y, 0.0, 1.0)\n"
"                      + 0.13*clamp(-n.y, 0.0, 1.0), uRoom);\n"
/* What a corridor has that a lit tube does not: a rail at hand height, a
   skirting where the wall meets the floor, and tiles overhead and underfoot
   to count your way along. None of it is geometry -- it is read off the
   wall's own coordinates, so it costs nothing, and it is the whole reason
   the place has a size you can feel. It fades out with distance rather than
   shimmering away into noise. */
"  if (uRoom > 0.3) {\n"
/* Off the building's height, not the cave's. The rail, the skirting and the
   dado are all measured from this, and the axis they were being measured from
   wanders four metres -- so a handrail slid up and down the wall as you walked
   the corridor, which is not something handrails do. */
"    float wy    = p.y - mix(centre(p.z).y, uHospY,\n"
"                           smoothstep(244.0, 259.0, -p.z));\n"
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
/* Wear. A surface with no history on it is the tell that gives away every
   rendered room: real paint is dirtier at the bottom than the top, because
   that is where the trolleys and the mops and the shoes reach. Grime rising
   from the skirting, and streaks running down -- constant in y, so the noise
   reads as something that ran rather than as noise. */
"    float low  = smoothstep(-0.55, -1.66, wy);\n"
"    float dirt = grain(p * 2.1) * 0.55 + grain(p * 8.5) * 0.45;\n"
"    float strk = grain(vec3(p.x * 5.5, 0.0, p.z * 5.5));\n"
"    alb *= mix(1.0, 0.70 + 0.34 * dirt, upr * low * near * 0.80);\n"
"    alb *= mix(1.0, 0.84 + 0.22 * strk, upr * low * near * 0.55);\n"
/* and the floor is not clean either: mop swirl, wide and faint */
"    alb *= mix(1.0, 0.90 + 0.16 * grain(p * 1.35), m_flr * near * 0.70);\n"
/* A suspended ceiling is a 600 mm grid of tiles in a metal tee. The seam
   above was on the floor's 1.75 m tile, which is the wrong size for a
   ceiling by nearly three times and read as a big flat sheet. */
"    vec2  cg   = abs(mod(p.xz + 0.3, 0.6) - 0.3);\n"
"    float tbar = smoothstep(0.268, 0.294, max(cg.x, cg.y));\n"
"    alb = mix(alb, alb * 0.74, m_cei * tbar * near);\n"
/* A hospital corridor is two colours, not one: a darker dado below the rail
   where the trolleys hit it, pale above. A single tone floor to ceiling is
   what a cave painted beige looks like, which is what this was. This one
   does not fade with distance -- it is the wall's colour, not a detail on
   it, and it is most of what makes the place read as built. */
/* CL-3: the same building, twenty years later, and not much longer.
 *
 * Yellowing on its own is just a colour grade -- it reads as a filter, not as
 * a place. What makes a derelict ward look derelict is that you can see the
 * building failing in a particular order: water gets in at the ceiling and
 * comes down the wall, the paint lets go where the water ran, the plaster
 * cracks along its own stress lines, and the ceiling tiles are the first
 * thing to actually fall out because they are only resting in a grid.
 *
 * All of it read off the wall's own coordinates, so it costs no data, and all
 * of it behind uProto so the shipped building is untouched. */
/* Two stretches of this building have been left to fail, and the rest has
   not. Crossing from one into the other is the whole effect -- a corridor
   away and it is twenty years later, which is a thing that only happens in
   here. */
"    float decayZ = max(zone(-p.z, 286.0, 312.0), zone(-p.z, 398.0, 436.0));\n"
"    if (decayZ > 0.004) {\n"
"      float dirt = grain(p * 1.1);\n"
"      alb = mix(alb, alb * vec3(1.08, 0.96, 0.71), decayZ);\n"
"      alb = mix(alb, alb * (0.88 + 0.20 * dirt), decayZ);\n"
/* Where the building has actually failed, and where it merely looks old.
   Everything below is multiplied by this, because damage everywhere is not
   damage -- it is a texture, and the first pass came back looking like the
   whole ward had been carved out of granite. A building falls down in
   patches, and the patches are what the eye reads. */
"      float dmg = smoothstep(0.50, 0.72, grain(p * 0.26)) * decayZ;\n"
/* water gets in at the ceiling first and comes down the wall in columns */
"      float cn  = grain(vec3(p.x * 2.7, 0.0, p.z * 2.7));\n"
"      float run = smoothstep(0.66, 0.92, cn) * smoothstep(1.20, -0.55, wy);\n"
"      alb = mix(alb, vec3(0.26, 0.19, 0.10), upr * run * decayZ * 0.70);\n"
/* the paint lets go where the water ran, and grey render shows under it */
"      float peel = smoothstep(0.58, 0.66, grain(p * 2.4)) * dmg;\n"
"      alb = mix(alb, vec3(0.40, 0.39, 0.36), upr * peel * 0.85);\n"
/* and then it cracks, along its own lines, wider low down where the wall is
   carrying the load. Two sets, both thin, both only inside the patches. */
"      float c1  = grain(p * 1.35);\n"
"      float c2  = grain(p * 3.3 + 7.0);\n"
"      float wid = 0.008 + 0.007 * smoothstep(0.6, -1.5, wy);\n"
"      float ck  = smoothstep(wid, 0.0008, abs(c1 - 0.5));\n"
"      ck = max(ck, smoothstep(wid * 0.6, 0.0005, abs(c2 - 0.5)) * 0.8);\n"
"      alb = mix(alb, vec3(0.085, 0.078, 0.070), upr * ck * dmg * 0.95);\n"
/* The ceiling tiles are the first thing to actually fall, because they are
   only resting in a grid. What is behind them is not another surface. */
"      float gx = floor((p.x + 0.3) / 0.6), gz = floor((p.z + 0.3) / 0.6);\n"
"      float gone = step(0.78, h1(gx * 31.7 + gz * 7.13 + 3.0));\n"
"      alb = mix(alb, vec3(0.035, 0.035, 0.040), m_cei * gone * decayZ * 0.94);\n"
/* and there is a building above the ceiling. A void with nothing in it is a
   black rectangle; a void with one dull pipe crossing it is a hole. */
"      float pip = smoothstep(0.052, 0.030, abs(fract(p.x * 0.9 + 0.35) - 0.5) * 1.1)\n"
"                + smoothstep(0.044, 0.024, abs(fract(p.z * 1.3 + 0.2) - 0.5) * 1.1);\n"
"      alb = mix(alb, vec3(0.20, 0.19, 0.17),\n"
"                m_cei * gone * clamp(pip, 0.0, 1.0) * decayZ * 0.85);\n"
/* a few places where the render has come off the wall entirely, not flaked */
"      float loss = smoothstep(0.70, 0.78, grain(p * 0.62));\n"
"      alb = mix(alb, vec3(0.30, 0.28, 0.25), upr * loss * decayZ * 0.80);\n"
/* mould where the wall meets anything, which is where it always starts */
"      float mould = smoothstep(-1.34, -1.74, wy) + smoothstep(1.10, 1.46, wy);\n"
"      alb = mix(alb, vec3(0.11, 0.13, 0.09),\n"
"                upr * clamp(mould, 0.0, 1.0) * (0.30 + 0.45 * dirt) * decayZ * 0.62);\n"
/* and the floor is only dirty -- vinyl does not crack, it lifts and stains */
"      alb = mix(alb, vec3(0.33, 0.30, 0.22),\n"
"                m_flr * smoothstep(0.55, 0.80, grain(p * 0.9)) * decayZ * 0.42);\n"
"    }\n"
"    float dado = smoothstep(-0.88, -0.74, wy);\n"
"    alb *= mix(1.0, mix(0.70, 1.10, dado), upr * uRoom);\n"
"    if (uProto == 4)\n"
"      alb = mix(alb, vec3(0.30, 0.055, 0.045),\n"
"                upr * (1.0 - dado) * uRoom * 0.92);\n"
/* and doors. Shut, one to a bay, with a frame around them. A corridor with
   nothing off it is a tube; a corridor lined with doors that do not open is
   the thing this place is supposed to be. */
"    float dpitch = (uProto == 2) ? 0.75 : 7.0;\n"
"    float dw     = (uProto == 2) ? 0.30 : 1.00;\n"
"    float dh     = (uProto == 5) ? 1.70 : (uProto == 6 ? 0.575 : 1.02);\n"
"    float dc     = (uProto == 5) ? -0.04 : (uProto == 6 ? -1.165\n"
"                 : (uProto == 7 ? 1.28 : -0.72));\n"
"    float dz2   = abs(mod(p.z + dpitch * 0.5, dpitch) - dpitch * 0.5);\n"
"    float e1    = max(dz2 / dw, abs(wy - dc) / dh);\n"
"    float door  = smoothstep(1.00, 0.93, e1);\n"
"    float frame = smoothstep(0.90, 0.99, e1) * smoothstep(1.16, 1.03, e1);\n"
"    alb = mix(alb, alb * 0.52, upr * door  * uRoom);\n"
"    alb = mix(alb, alb * 1.22, upr * frame * uRoom);\n"
/* What a ward door has: a wired-glass vision panel at head height, and a
   steel kickplate at the bottom where it gets shoved open. Two rectangles,
   and the door stops being a darker patch of wall. */
"    float vis  = smoothstep(0.52, 0.44, max(dz2 / (0.30 * dw),\n"
"                                        abs(wy - dc - 0.60 * dh) / (0.34 * dh)))\n"
"               * door;\n"
"    float kick = smoothstep(0.99, 0.90, dz2 / dw)\n"
"               * smoothstep(dc - 0.62 * dh, dc - 0.70 * dh, wy) * door;\n"
"    alb = mix(alb, alb * 2.05, upr * vis  * uRoom);\n"
"    alb = mix(alb, alb * 1.45, upr * kick * uRoom);\n"
/* ---- 2차 시안: 벽에 붙는 것들과 병동 마감 ------------------------------
   All albedo, all read off the wall's own coordinates, all behind uProto.
   dz2 is distance to the nearest door centre along the wall, so anything
   placed against it sits beside a door the way real fittings do. */
"    if (lay(10, -p.z) > 0.02) {\n"
/* O-11 병실 명패 — a plate beside every door, room number on it, name slot
   blank. The only proposal that improves wayfinding while it unsettles. */
"      float pe = max(abs(dz2 - 1.42) / 0.30, abs(wy - 0.34) / 0.115);\n"
"      alb = mix(alb, alb * 1.42, smoothstep(1.00, 0.90, pe));\n"
"      float slot = max(abs(dz2 - 1.42) / 0.24, abs(wy - 0.30) / 0.055);\n"
"      alb = mix(alb, alb * 0.34, smoothstep(1.00, 0.86, slot));\n"
"      float num = max(abs(dz2 - 1.42) / 0.20, abs(wy - 0.395) / 0.035);\n"
"      alb = mix(alb, alb * 0.22,\n"
"                smoothstep(1.0, 0.88, num) * step(0.45, fract(dz2 * 26.0)));\n"
"    }\n"
"    if (lay(11, -p.z) > 0.02) {\n"
/* O-9 손소독제 · O-10 소화기함 · O-14 산소 배관구, all at once so the
   corridor gets its rhythm back */
"      float dsp = max(abs(dz2 - 1.72) / 0.085, abs(wy - 0.10) / 0.145);\n"
"      alb = mix(alb, alb * 0.55, smoothstep(1.00, 0.88, dsp));\n"
"      float fz = abs(mod(p.z + 10.5, 21.0) - 10.5);\n"
"      float ext = max(fz / 0.28, abs(wy + 0.06) / 0.42);\n"
"      alb = mix(alb, vec3(0.46, 0.055, 0.045), smoothstep(1.00, 0.90, ext) * 0.92);\n"
"      alb = mix(alb, vec3(0.75, 0.74, 0.70),\n"
"                smoothstep(0.86, 0.74, ext) * smoothstep(0.50, 0.62, ext));\n"
"      float hw = max(abs(dz2 - 2.35) / 0.62, abs(wy - 0.46) / 0.085);\n"
"      alb = mix(alb, alb * 1.30, smoothstep(1.00, 0.90, hw));\n"
"      alb = mix(alb, alb * 0.40,\n"
"                smoothstep(1.0, 0.85, hw) * step(0.62, fract(dz2 * 5.5)));\n"
"    }\n"
/* 벽에 붙는 나머지 — 명패와 같은 dz2 배치. 시계가 전에 실패한 이유가 이것:
   벽을 따라가는 임의의 mod 대신 문 중심에서 잰 거리를 쓰면 반드시 보이는
   벽에 앉는다. */
/* ---- 시안: 알베도만 바꾸는 것들 ----------------------------------------- */
"    if (uProto == 37) {\n"
/* PL-5 뒤집힌 병실 — 조명기구가 바닥에, 장판 타일이 머리 위에 */
"      vec2 fm = abs(mod(p.xz, 7.0) - 3.5);\n"
"      if (max(fm.x, fm.y) < 1.16 && m_flr > 0.4) {\n"
"        vec2 pd = mod(p.xz, 7.0) - 3.5;\n"
"        float tb = smoothstep(0.17, 0.09, abs(abs(pd.x) - 0.40));\n"
"        alb = mix(vec3(0.62, 0.61, 0.58), vec3(1.35, 1.33, 1.22) * (0.42 + 0.85 * tb),\n"
"                  smoothstep(1.10, 1.02, max(fm.x, fm.y)));\n"
"      }\n"
"      vec2 ft = abs(mod(p.xz + 0.875, 1.75) - 0.875);\n"
"      alb = mix(alb, alb * 0.55, m_cei * smoothstep(0.842, 0.871, max(ft.x, ft.y)));\n"
"      alb = mix(alb, vec3(0.395, 0.412, 0.390), m_cei * 0.7);\n"
"    }\n"
"    if (uProto == 38) {\n"
/* CT-2 아까 그 방 — 벽의 긁힌 자국 하나, 세 베이 뒤에 똑같이 */
"      float mk = smoothstep(0.020, 0.004, abs(wy + 0.44 - 0.16 * sin(dz2 * 9.0)))\n"
"               * smoothstep(2.10, 1.30, dz2) * smoothstep(0.60, 1.05, dz2);\n"
"      alb = mix(alb, alb * 0.30, upr * mk * near);\n"
"    }\n"
/* ---- 사고 -------------------------------------------------------------
 * 차에 치여 여기 온 사람이고, 이 건물은 그걸 기억하고 있다. 두 군데뿐이다.
 * 한 번은 바닥에 흘리고, 한 번은 벽에서 나온다. 기억은 자주 나면 기억이 아니다.
 */
"    if (lay(50, -p.z) > 0.02) {\n"
/* 스키드 마크 -- 병원 장판 위에 휘어진 두 줄. 먼저 이것만 보여주고,\n"
   무엇이었는지는 나중에 말한다. */
"      float k = lay(50, -p.z);\n"
"      float u = (-p.z - 395.0) * 0.16;\n"
"      float bend = u * u * 1.30 - 0.20;\n"
"      float d1 = abs(p.x - uCorrX - bend + 0.34);\n"
"      float d2 = abs(p.x - uCorrX - bend - 0.34);\n"
"      float sk = smoothstep(0.085, 0.030, min(d1, d2));\n"
"      sk *= 0.55 + 0.45 * grain(p * 3.4);\n"
"      alb = mix(alb, vec3(0.055, 0.050, 0.048), m_flr * sk * k * 0.92);\n"
"    }\n"
"    if (lay(51, -p.z) > 0.02) {\n"
/* 도로가 된 바닥, 벽에 박힌 가드레일, 그리고 벽에서 나오는 전조등 두 개. */
"      float k = lay(51, -p.z);\n"
/* 바닥이 아스팔트로 -- 가운데 흰 차선까지 */
"      float lane = smoothstep(0.075, 0.030, abs(p.x - uCorrX))\n"
"                 * step(0.45, fract(-p.z * 0.30));\n"
"      vec3 road = mix(vec3(0.085, 0.085, 0.090), vec3(0.86, 0.86, 0.83), lane);\n"
"      road *= 0.85 + 0.30 * grain(p * 5.0);\n"
"      alb = mix(alb, road, m_flr * k * 0.90);\n"
/* 가드레일 -- 벽 허리에 박혀 찌그러진 채 */
"      float gy = abs(wy + 0.30);\n"
"      float gr = smoothstep(0.115, 0.075, gy);\n"
"      float wav = smoothstep(0.030, 0.010, abs(gy - 0.055));\n"
"      float dent = 0.55 + 0.45 * sin(-p.z * 1.7);\n"
"      alb = mix(alb, vec3(0.52, 0.53, 0.55), upr * gr * k * 0.92);\n"
"      alb = mix(alb, vec3(0.22, 0.22, 0.24), upr * wav * k * 0.85);\n"
"      alb = mix(alb, vec3(0.30, 0.14, 0.06),\n"
"                upr * gr * k * smoothstep(0.55, 0.95, dent) * 0.5);\n"
"      float post = smoothstep(0.055, 0.022, abs(fract(-p.z * 0.42) - 0.5) * 2.4)\n"
"                 * smoothstep(-0.20, -1.40, wy);\n"
"      alb = mix(alb, vec3(0.38, 0.39, 0.40), upr * post * k * 0.8);\n"
"    }\n"
"    if (lay(39, -p.z) > 0.02) {\n"
/* CT-4 여덟 개의 EXIT -- 천장에, 전부 다른 방향. 이 게임에서 초록은 오직
   출구 색이고, 여덟 번 반복하면 색의 의미가 무너진다. 문 위 표지판과 같은
   그림(달리는 사람 + 화살표)을 써서 "표지판"으로 확실히 읽히게 한다. */
"      vec2 bc = (floor(p.xz / 7.0) + 0.5) * 7.0;\n"
"      vec2 rel = p.xz - bc;\n"
"      float ai = floor(atan(rel.y, rel.x) / 0.7853981 + 0.5);\n"
"      float aa = ai * 0.7853981;\n"
"      vec2  ctr = bc + vec2(cos(aa), sin(aa)) * 2.05;\n"
"      vec2  d2  = p.xz - ctr;\n"
"      vec2  q2  = vec2(d2.x * cos(-aa) - d2.y * sin(-aa),\n"
"                       d2.x * sin(-aa) + d2.y * cos(-aa));\n"
"      if (m_cei > 0.4 && abs(q2.x) < 0.46 && abs(q2.y) < 0.15) {\n"
"        vec2 g = vec2(q2.x, q2.y);\n"
"        float head = step(length(g - vec2(-0.24, 0.055)), 0.030);\n"
"        float body = step(abs(g.x + 0.24), 0.026) * step(abs(g.y + 0.004), 0.058);\n"
"        float legs = step(abs(abs(g.x + 0.24) - 0.034), 0.019)\n"
"                   * step(abs(g.y + 0.082), 0.030);\n"
"        float arrw = step(0.10, g.x) * step(g.x, 0.34)\n"
"                   * step(abs(g.y), 0.056 * (1.0 - (g.x - 0.10) / 0.24));\n"
"        float fig  = clamp(head + body + legs + arrw, 0.0, 1.0);\n"
"        alb = mix(vec3(0.02, 2.30, 0.16), vec3(2.20, 2.40, 2.20), fig);\n"
"      }\n"
"    }\n"
"    if (uProto == 40) {\n"
/* FM-2 벽을 보는 창 — 유리 20 cm 뒤가 콘크리트 */
"      float we = max(abs(dz2 - 2.60) / 0.52, abs(wy - 0.28) / 0.40);\n"
"      alb = mix(alb, alb * 1.35, smoothstep(1.06, 0.96, we) * smoothstep(0.90, 1.00, we));\n"
"      alb = mix(alb, vec3(0.20, 0.19, 0.18), smoothstep(0.96, 0.88, we));\n"
"      float mull = smoothstep(0.030, 0.012, abs(dz2 - 2.60))\n"
"                 + smoothstep(0.024, 0.010, abs(wy - 0.28));\n"
"      alb = mix(alb, alb * 1.9, smoothstep(0.94, 0.86, we) * clamp(mull, 0.0, 1.0));\n"
"    }\n"
"    if (lay(42, -p.z) > 0.02) {\n"
/* LT-1 깜빡이는 하나 -- 건물 전체에서 딱 한 등. 나머지는 전부 심장 박동에
   맞춰 고요히 부푸는데 이것만 불규칙하다. 자꾸 돌아오게 되는 교차로에 둔다. */
"      vec2 cell = floor(p.xz / 7.0);\n"
"      if (abs(cell.x - floor(uCorrX / 7.0)) < 0.5 && abs(cell.y + 62.0) < 0.5) {\n"
"        float t9 = floor(uTime * 13.0);\n"
"        float fl = step(0.42, fract(sin(t9 * 91.7) * 4371.3));\n"
"        vec2 pm2 = abs(mod(p.xz, 7.0) - 3.5);\n"
"        if (max(pm2.x, pm2.y) < 1.16 && m_cei > 0.4)\n"
"          alb *= mix(0.10, 1.35, fl);\n"
"      }\n"
"    }\n"
"    if (uProto == 43) {\n"
/* W-2 격리병동 — 바닥 노란 경계선, 음압 표시등, 비닐 커튼 */
"      alb = mix(alb, vec3(0.95, 0.78, 0.10),\n"
"                m_flr * smoothstep(0.10, 0.04, abs(dz2 - 1.9)) * 0.9);\n"
"      float lamp = max(abs(dz2 - 0.70) / 0.075, abs(wy - 0.88) / 0.075);\n"
"      alb = mix(alb, vec3(2.2, 0.9, 0.05), smoothstep(1.0, 0.7, lamp));\n"
"      alb = mix(alb, alb * 1.28 + vec3(0.02),\n"
"                upr * door * smoothstep(0.9, 0.4, abs(wy + 0.2)) * 0.55);\n"
"    }\n"
"    if (uProto == 47) {\n"
/* W-9 영상의학과 — 납문, 두꺼운 문설주, 방사선 삼엽 */
"      alb = mix(alb, vec3(0.30, 0.31, 0.33), upr * door * 0.9);\n"
"      alb = mix(alb, alb * 1.5, upr * frame * 1.4);\n"
"      vec2 tq = vec2(dz2, wy - 0.30);\n"
"      float tr = length(tq);\n"
"      float ang = atan(tq.y, tq.x);\n"
"      float blade = step(0.35, fract(ang * 1.4324));\n"
"      alb = mix(alb, vec3(1.30, 1.00, 0.05),\n"
"                door * smoothstep(0.26, 0.22, tr) * blade);\n"
"      alb = mix(alb, vec3(1.30, 1.00, 0.05),\n"
"                door * smoothstep(0.055, 0.035, tr));\n"
"    }\n"
"    if (uProto == 20) {\n"
/* O-13 멈춘 벽시계 — 문과 문 사이, 전부 같은 시각 */
"      vec2  cq = vec2(dz2 - 2.95, wy - 0.52);\n"
"      float cr = length(cq);\n"
"      alb = mix(alb, alb * 1.55, smoothstep(0.175, 0.160, cr));\n"
"      alb = mix(alb, alb * 0.28, smoothstep(0.160, 0.175, cr)\n"
"                                * smoothstep(0.200, 0.184, cr));\n"
"      float h1a = abs(cq.x * 0.906 + cq.y * 0.423);\n"
"      float h2a = abs(cq.x * 0.500 - cq.y * 0.866);\n"
"      float hands = max(smoothstep(0.014, 0.005, h1a) * smoothstep(0.105, 0.092, cr),\n"
"                        smoothstep(0.011, 0.004, h2a) * smoothstep(0.140, 0.126, cr));\n"
"      alb = mix(alb, alb * 0.16, hands * smoothstep(0.170, 0.155, cr));\n"
"    }\n"
"    if (lay(26, -p.z) > 0.02) {\n"
/* O-12 안내 게시판 — 읽히지 않는 종이 몇 장, 한 장은 압정 하나에 매달려 */
"      float be = max(abs(dz2 - 2.80) / 0.62, abs(wy - 0.30) / 0.42);\n"
"      alb = mix(alb, vec3(0.42, 0.33, 0.22), smoothstep(1.00, 0.92, be));\n"
"      vec2 pq = vec2(dz2, wy);\n"
"      float sheet = smoothstep(1.0, 0.88, max(abs(fract(pq.x * 2.6) - 0.5) / 0.32,\n"
"                                              abs(fract(pq.y * 3.1) - 0.5) / 0.34));\n"
"      alb = mix(alb, vec3(0.90, 0.89, 0.85),\n"
"                smoothstep(1.00, 0.90, be) * sheet * 0.92);\n"
"      alb = mix(alb, alb * 0.55,\n"
"                smoothstep(1.00, 0.90, be) * sheet\n"
"                * step(0.55, fract(pq.x * 21.0)) * step(0.5, fract(pq.y * 15.0)) * 0.6);\n"
"    }\n"
"    if (uProto == 32) {\n"
/* O-20 비상 손전등 거치대 — 충전 표시등만 초록으로 깜빡인다 */
"      float fe = max(abs(dz2 - 1.95) / 0.075, abs(wy - 0.02) / 0.185);\n"
"      alb = mix(alb, vec3(0.16, 0.17, 0.18), smoothstep(1.00, 0.88, fe));\n"
"      float led = max(abs(dz2 - 1.95) / 0.020, abs(wy + 0.15) / 0.020);\n"
"      alb = mix(alb, vec3(0.10, 2.40, 0.55), smoothstep(1.0, 0.5, led));\n"
"    }\n"
"    if (uProto == 15) {\n"
/* W-3 폐쇄병동 — padding, an observation port in every door, and the wall
   quilted where a wall should be flat */
"      float q = sin(p.z * 7.4) * sin(wy * 7.4);\n"
"      alb = mix(alb, alb * (0.90 + 0.16 * q), upr * 0.85);\n"
"      alb = mix(alb, vec3(0.60, 0.62, 0.58), upr * 0.35);\n"
"      float port = length(vec2(dz2, wy + 0.10) - vec2(0.0, 0.42));\n"
"      alb = mix(alb, alb * 2.30, smoothstep(0.115, 0.085, port) * door);\n"
"      alb = mix(alb, alb * 0.30, smoothstep(0.140, 0.118, port)\n"
"                                * smoothstep(0.100, 0.120, port) * door);\n"
"    }\n"
"    if (lay(16, -p.z) > 0.02) {\n"
/* W-4 수술부 — tiled green floor to ceiling, and a red lamp over each door */
"      vec2 tq = abs(fract(vec2(p.z, wy) * 4.0) - 0.5);\n"
"      float tl = smoothstep(0.40, 0.47, max(tq.x, tq.y));\n"
"      alb = mix(alb, vec3(0.42, 0.60, 0.55), upr * 0.80);\n"
"      alb = mix(alb, alb * 0.74, upr * tl * 0.9);\n"
"      float lamp = max(dz2 / 0.20, abs(wy - 0.92) / 0.075);\n"
"      alb = mix(alb, vec3(1.60, 0.10, 0.06), smoothstep(1.00, 0.86, lamp));\n"
"    }\n"
"    if (lay(17, -p.z) > 0.02) {\n"
/* W-5 영안실 — tiled to the ceiling, and one wall of stainless drawers */
"      vec2 tq = abs(fract(vec2(p.z, wy) * 5.0) - 0.5);\n"
"      alb = mix(alb, vec3(0.74, 0.78, 0.79), upr * 0.85);\n"
"      alb = mix(alb, alb * 0.80, upr * smoothstep(0.41, 0.48, max(tq.x, tq.y)));\n"
"      vec2 dq = abs(fract(vec2(p.z * 1.6, (wy + 1.7) * 1.5)) - 0.5);\n"
"      float drw = step(0.0, n.x) * upr\n"
"                * smoothstep(-1.72, -1.60, wy) * smoothstep(0.90, 0.76, wy);\n"
"      alb = mix(alb, vec3(0.62, 0.65, 0.67), drw * 0.9);\n"
"      alb = mix(alb, alb * 0.42, drw * smoothstep(0.40, 0.47, max(dq.x, dq.y)));\n"
"      alb = mix(alb, alb * 1.60, drw * smoothstep(0.10, 0.04, abs(dq.x - 0.22))\n"
"                                     * smoothstep(0.16, 0.10, abs(dq.y)));\n"
"    }\n"
"    if (uProto == 18) {\n"
/* W-1 소아병동 — everything a hand lower, and a faded border where the dado
   would be. The proportions are all correct; they are correct for a child. */
"      alb = mix(alb, alb * 1.06, upr * 0.5);\n"
"      float band = smoothstep(0.10, 0.05, abs(wy + 0.62));\n"
"      float hue  = fract(p.z * 0.28);\n"
"      vec3  toy  = mix(vec3(0.85, 0.62, 0.30), vec3(0.42, 0.66, 0.78),\n"
"                       step(0.5, fract(p.z * 1.4)));\n"
"      alb = mix(alb, toy, upr * band * (0.55 + 0.25 * sin(hue * 18.0)));\n"
"      float lorail = smoothstep(0.045, 0.022, abs(wy + 1.10));\n"
"      alb = mix(alb, alb * 0.45, upr * lorail * near);\n"
"    }\n"
"    if (uProto == 19) {\n"
/* W-10 기계실 — no finishes at all. Concrete, duct, cable tray, and the
   building with its back to you. */
"      alb = mix(alb, vec3(0.44, 0.44, 0.43) * (0.86 + 0.28 * grain(p * 1.6)),\n"
"                (upr + m_cei) * 0.92);\n"
"      float duct = max(abs(p.x - 0.9) / 0.42, abs(wy - 1.10) / 0.26);\n"
"      alb = mix(alb, vec3(0.55, 0.56, 0.54), smoothstep(1.00, 0.90, duct));\n"
"      alb = mix(alb, alb * 0.62,\n"
"                smoothstep(1.0, 0.9, duct) * step(0.72, fract(p.z * 3.2)));\n"
"      float tray = max(abs(p.x + 1.4) / 0.22, abs(wy - 1.34) / 0.055);\n"
"      alb = mix(alb, vec3(0.30, 0.31, 0.30), smoothstep(1.00, 0.88, tray));\n"
"    }\n"

"  }\n"
/* Ceiling panels on the same seven-metre grid as the pillars -- and not all
   of them working. A corridor where every fixture is identical and every one
   is lit is a diagram of a corridor. One in eight is out and the rest differ,
   which is what a real ceiling looks like and is most of why the far end of
   this place now feels like somewhere nobody has been for a while. The value
   comes from the cell, so it is the same panel every frame. */
/* the proposals that are not on a wall */
"  if (uProto == 1 && uRoom > 0.5 && m_cei > 0.4) {\n"
/* PL-1: a ward door, complete, set into the ceiling */
"    vec2  cd = abs(mod(p.xz + vec2(1.75, 3.5), vec2(3.5, 7.0)) - vec2(1.75, 3.5));\n"
"    float ce = max(cd.x / 0.52, cd.y / 1.05);\n"
"    alb = mix(alb, alb * 0.42, smoothstep(1.00, 0.93, ce));\n"
"    alb = mix(alb, alb * 1.35, smoothstep(0.90, 0.99, ce) * smoothstep(1.14, 1.03, ce));\n"
"    alb = mix(alb, alb * 2.10, smoothstep(0.50, 0.42, max(cd.x / 0.17, cd.y / 0.36)));\n"
"  }\n"
"  float plive = 1.0;\n"
"  if (uRoom > 0.15) {\n"
"    vec2 pcell = floor(p.xz / 7.0);\n"
"    float pj = h1(pcell.x * 31.7 + pcell.y * 7.13 + uSeed);\n"
"    plive = pj < 0.125 ? 0.10 : (0.80 + 0.42 * pj);\n"
"  }\n"
"  if (uRoom > 0.5 && n.y < -0.7) {\n"
"    vec2 pm = abs(mod(p.xz, 7.0) - 3.5);\n"
/* the diffuser sits in a frame, so the fitting has an edge rather than
   fading into the tile it is set into */
"    float pe = max(pm.x, pm.y);\n"
/* A fitting, not a glowing rectangle. What makes a ceiling light read as a
   light is that you can see the tubes through the diffuser: two bright bars
   with the panel darker between and around them, inside a frame. Flat, it was
   a hole in the ceiling with light behind it. */
"    if (pe < 1.16) {\n"
"      vec2  pd   = mod(p.xz, 7.0) - 3.5;\n"
"      float tube = smoothstep(0.17, 0.09, abs(abs(pd.x) - 0.40));\n"
"      float ends = smoothstep(1.02, 0.90, abs(pd.y));\n"
"      alb = mix(vec3(0.62, 0.61, 0.58),\n"
"                vec3(1.35, 1.33, 1.22) * plive\n"
"                  * (0.42 + 0.85 * tube * ends),\n"
"                smoothstep(1.10, 1.02, pe));\n"
"    }\n"
/* The corridor gets its own line of them, down its own axis, every four
   metres. The room grid is on sevens and the corridor sits wherever the cave
   left it, so the two need not line up at all -- and where they did not, the
   last stretch of the game had no fitting over it anywhere and you walked to
   the door in the dark. A line of lights running away from you is also the
   plainest way there is of saying which direction the building goes. */
"    if (-p.z > uWakeZ - 21.0) {\n"
"      float lx = abs(p.x - uCorrX);\n"
"      float lz = abs(mod(-p.z + 2.0, 4.0) - 2.0);\n"
"      float le = max(lx / 0.62, lz / 0.80);\n"
"      if (le < 1.14) {\n"
/* the corridor's are single-tube battens, which is what a corridor gets */
"        float lt = smoothstep(0.20, 0.10, lx);\n"
"        alb = mix(vec3(0.62, 0.61, 0.58),\n"
"                  vec3(1.35, 1.33, 1.22) * plive * (0.40 + 0.90 * lt),\n"
"                  smoothstep(1.08, 1.00, le));\n"
"      }\n"
"    }\n"
"  }\n"
"\n"
"  vec2 lc = centre(uCam.z - 20.0);\n"
"  vec3 lp = vec3(lc.x, lc.y + 0.6, max(uCam.z - 20.0, -(uWakeZ + 14.0)));\n"
"  vec3 ld = normalize(lp - p);\n"
/* Wrapped diffuse: rock in a cave is lit by everything the light has
   already touched, so the terminator softens instead of cutting to
   black - the moon look was a hard max(dot,0) with no fill at all. */
"  float dif = clamp((dot(n, ld) + 0.38) / 1.38, 0.0, 1.0);\n"
"  float sh  = shade_to(p, ld);\n"
"  float ao  = occl(p, n);\n"
"  float fre = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);\n"
"\n"
"  vec3 warm = mix(vec3(1.00, 0.93, 0.82), vec3(0.95, 1.00, 0.97), uRoom);\n"
/* No ambient floor: a surface no light reaches stays black, or the
   sounding would stop being the way you see. The corridor doubles it. */
"  float sky = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);\n"
"  vec3 hemi = mix(warm * 0.55, vec3(0.85, 0.90, 1.0), sky);\n"
/* Less flat fill in the corridor. Ambient this high plus panels plus haze
   left the whole room inside a third of a stop -- no shadow under anything,
   no dark in any corner, which is the look of a render and not of a room. */
"  vec3 col  = alb * hemi * uLight * (0.16 + 0.20*uRoom) * ao;\n"
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
/* Every fitting in the place is on the same heart. Between beats they
   sit low; on the beat the whole floor plan goes white for a moment,
   which is the only light you are given and the only thing the things
   in here are afraid of. */
/* A pulse, not a strobe. At 0.42 between beats and 2.17 on one the room was
   going almost dark and back five times over, which is a fault in the wiring;
   the fittings now sit at a readable level and swell on the beat. */
/* The heart drives the swell; a hand on a switch out in the ward drives all
   of them at once, which is a different thing and has to look like one. */
/* and two stretches where the tubes are simply the wrong colour, which is a
   thing that happens in real buildings and is worse for being plausible */
"    float greenZ = max(zone(-p.z, 336.0, 354.0), zone(-p.z, 462.0, 480.0));\n"
"    vec3 pan = mix(vec3(0.94, 1.00, 0.96), vec3(0.40, 1.22, 0.58),\n"
"                   max(greenZ, uProto == 3 ? 1.0 : 0.0)) * uRoom\n"
/* The swing was 0.78 to 1.73 every beat, which is the room doubling in
   brightness once a second and is painful to look at for eight minutes.
   The beat should be felt at the edge of vision, not stared into: the
   floor sits bright and the pulse rides on top of it. */
"             * (0.94 + 0.52 * uBlink + 2.30 * uWard) * (1.0 - 0.72 * uDark);\n"
"    for (int px = 0; px < 2; px++)\n"
"    for (int pz = 0; pz < 2; pz++) {\n"
"      vec2 pcv = base + vec2(float(px), float(pz)) * 7.0;\n"
/* Hung from the building's ceiling, not from the cave's axis. The rooms stopped
   undulating and hold one height; these were still being placed off centre(z),
   which wanders four metres up and down, so through much of the hospital the
   fittings were buried in the floor or sat above the ceiling lighting nothing.
   Same mistake as probing the maze against the tunnel centre. */
"      float pcy = mix(centre(pcv.y).y, uHospY,\n"
"                      smoothstep(244.0, 259.0, -pcv.y));\n"
"      vec3 plp = vec3(pcv.x, pcy + 1.02, pcv.y);\n"
"      vec3 pld = plp - p;\n"
"      float pd2 = dot(pld, pld);\n"
"      pld *= inversesqrt(max(pd2, 1e-4));\n"
"      float att = 1.0 / (1.0 + pd2 * 0.10);\n"
"      float pdif = max(dot(n, pld), 0.0);\n"
"      vec3 hv = normalize(pld - rd);\n"
/* Two lobes, because two materials. Paint is semi-gloss and answers wide;
   the vinyl underfoot is polished and answers narrow and hard. One lobe for
   everything is what makes a room look like it is made of paper. */
"      float pw = pow(max(dot(n, hv), 0.0), 26.0);\n"
"      float pg = pow(max(dot(n, hv), 0.0), 170.0);\n"
/* and the fitting this light comes out of is the one whose cell it is in,
   so a dead panel is dark and casts nothing */
"      vec2  pcl = floor(pcv / 7.0);\n"
"      float pjj = h1(pcl.x * 31.7 + pcl.y * 7.13 + uSeed);\n"
"      float plv = pjj < 0.125 ? 0.10 : (0.80 + 0.42 * pjj);\n"
/* with the corners left alone, so the pillars have a base and the wall
   meeting the floor is a line rather than a guess */
"      col += alb * pan * pdif * att * 0.85 * plv * mix(ao, 1.0, 0.20);\n"
"      col += pan * pw * att * 0.34 * plv;\n"
"      col += pan * pg * att * 1.30 * plv * m_flr;\n"
"    }\n"
/* and the corridor's own line, lit the same way -- the two nearest of them */
"    if (-p.z > uWakeZ - 21.0) {\n"
"      float lzc = floor((-p.z + 2.0) / 4.0) * 4.0 - 2.0;\n"
"      for (int q = 0; q <= 1; q++) {\n"
"        float lz2 = lzc + float(q) * 4.0;\n"
"        vec3  llp = vec3(uCorrX, uHospY + 1.02, -lz2);\n"
"        vec3  lld = llp - p;\n"
"        float ld2 = dot(lld, lld);\n"
"        lld *= inversesqrt(max(ld2, 1e-4));\n"
"        float lat2 = 1.0 / (1.0 + ld2 * 0.10);\n"
"        vec3  hv3 = normalize(lld - rd);\n"
"        col += alb * pan * max(dot(n, lld), 0.0) * lat2 * 0.85\n"
"             * mix(ao, 1.0, 0.20);\n"
"        col += pan * pow(max(dot(n, hv3), 0.0),  26.0) * lat2 * 0.34;\n"
"        col += pan * pow(max(dot(n, hv3), 0.0), 170.0) * lat2 * 1.30 * m_flr;\n"
"      }\n"
"    }\n"
"  }\n"
/* The floor is polished, and the one thing that says so is the ceiling lying
   in it. Reflect the view ray off the floor, run it up to the height the
   fittings hang at, and see whether it lands on one: if it does, that panel
   is in the floor. It is a real planar reflection and it costs no march --
   and it is the single biggest reason a corridor reads as a photograph
   rather than as a diagram, because a matte floor is a thing that does not
   exist in a building. The footprint softens with reflected distance, so a
   far light smears down the floor toward you instead of staying a rectangle.
   Not on the road: the reflected grid streaming past under the white was
   more than that moment wants. */
"  if (uRoom > 0.4 && uRoad < 0.5 && m_flr > 0.02) {\n"
"    vec3  rv = reflect(rd, n);\n"
"    float ch = (centre(p.z).y + 1.02) - p.y;\n"
"    if (rv.y > 0.05 && ch > 0.1) {\n"
"      float k    = ch / rv.y;\n"
"      vec3  hp   = p + rv * k;\n"
"      vec2  pm2  = abs(mod(hp.xz, 7.0) - 3.5);\n"
"      float soft = 0.05 + 0.030 * k;\n"
"      float inp  = 1.0 - smoothstep(1.10 - soft, 1.10 + soft, max(pm2.x, pm2.y));\n"
"      vec2  rcl  = floor(hp.xz / 7.0);\n"
"      float rjj  = h1(rcl.x * 31.7 + rcl.y * 7.13 + uSeed);\n"
"      float rlv  = rjj < 0.125 ? 0.10 : (0.80 + 0.42 * rjj);\n"
"      col += vec3(1.00, 0.97, 0.88) * inp * rlv * m_flr * uRoom\n"
"           * 0.62 / (1.0 + k * 0.16);\n"
"    }\n"
"  }\n"
/* Depth needs somewhere to go. In the cave that is darkness; in the corridor
   everything is the same pale albedo under the same soft panels, so without
   a haze to recede into the far wall sits at the same value as the near one
   and the whole frame flattens into one sheet of beige. */
/* The corridor's share of this was far too much of it: two thirds of the
   picture was haze by fifteen metres, so every surface converged on the same
   pale value and the room came back looking like weather. A lit building is
   mostly clear air. Enough left to keep the far end from sitting at the same
   value as the near end, and no more. */
/* The one bulb in the last room. It is a real light in the scene rather
   than a bright patch painted on the ceiling: an inverse-square falloff
   from a point, a visible filament, the cord it hangs from, and a warm
   spill on the floor under it. When it goes, it goes - no fade. */
"  if (-p.z > uWakeZ + 1.0 && uLampOut < 0.5) {\n"
"    vec2  lcc = centre(p.z);\n"
"    float lst = smoothstep(244.0, 259.0, -p.z);\n"
"    float lcy = mix(lcc.y, uHospY, lst);\n"
"    vec3  bp  = vec3(uCorrX, lcy + 0.62, -(uWakeZ + 26.0));\n"
"    vec3  bd  = bp - p;\n"
"    float bl2 = dot(bd, bd);\n"
"    vec3  bn  = bd * inversesqrt(max(bl2, 1e-4));\n"
"    float lam = max(dot(n, bn), 0.0) * 26.0 / (1.0 + bl2);\n"
"    col += alb * vec3(1.00, 0.90, 0.72) * lam;\n"
"    col += vec3(1.00, 0.93, 0.78) * pow(max(dot(reflect(-bn, n), -rd), 0.0), 44.0)\n"
"         * 6.0 / (1.0 + bl2);\n"
"  }\n"
"  float fog = exp(-t * (0.075 - 0.03*uLight - 0.026*uRoom));\n"
/* Held. Everything in this building is the colour of a tube; a hand closing
   around yours is the only warm thing that happens in it, so it arrives as
   warmth -- and it is the one moment the place stops being somewhere you are
   alone in. */
/* 벽에서 나오는 전조등.
 *
 * 이 사람을 여기 데려온 것이 이것이고, 게임에서 딱 한 번 나온다. 방 안에 있는
 * 광원이 아니라 벽 안에서 나오는 두 개의 점이라 위치가 설명되지 않는다 --
 * 설명되면 조명기구가 되고, 조명기구는 무섭지 않다. 시야를 실제로 가릴 만큼
 * 밝게: 눈이 부셔서 그 구간을 지나가는 동안 앞이 잘 안 보여야 한다. */
"  {\n"
"    float hk = lay(51, -uCam.z);\n"
"    if (hk > 0.004) {\n"
"      float hz = -441.0;\n"
"      vec2  lc2 = centre(hz);\n"
"      float hy = mix(lc2.y, uHospY, smoothstep(244.0, 259.0, -hz)) + 0.10;\n"
"      for (int e = 0; e < 2; e++) {\n"
"        vec3 lp2 = vec3(uCorrX + (e == 0 ? -0.78 : 0.78), hy, hz);\n"
"        vec3 tl  = lp2 - uCam;\n"
"        float dl = length(tl);\n"
"        vec3  tn = tl / max(dl, 1e-4);\n"
"        float al = max(dot(rd, tn), 0.0);\n"
/* the lamp itself, and the glare it throws across everything else */
"        float core  = pow(al, 9000.0);\n"
"        float glare = pow(al, 22.0) * 0.55 + pow(al, 3.0) * 0.16;\n"
"        col += vec3(1.00, 0.97, 0.90) * (core * 6.0 + glare)\n"
"             * hk * 3.4 / (1.0 + dl * dl * 0.010);\n"
"      }\n"
"    }\n"
"  }\n""  col = mix(col, col * vec3(1.24, 1.06, 0.88) + vec3(0.09, 0.045, 0.008),\n"
"            uHand * 0.55);\n"
"  col = mix(mix(vec3(0.010,0.012,0.018) + warm*0.045*uLight,\n"
"                vec3(0.66, 0.63, 0.55), uRoom), col, fog);\n"/* The sign takes some of the haze and not all of it. Fogged like a wall it
   came out the same mint as everything else, and a sign that has gone the
   colour of the corridor is not a sign -- it is meant to be the one thing
   down here you can pick out from the far end. */
"  col = mix(col, sign_col, sign_m * mix(0.55, 1.0, fog));\n"
/* Struck by the lights it goes bright and stops; otherwise it is a dark,
   wet thing that the room barely wants to light. */
/* Chitin, not skin. Hard and wet: a tight specular that slides over the
   plates, a body far darker than the room it stands in, and bristles -
   the study that asked which spiders people flinch at came back with
   hairy and thick, so the normal is broken up at a hair's scale. */
"  if (onBeast > 0.5) {\n"
"    vec3 vv = normalize(uCam - p);\n"
"    vec3 bn = n;\n"
"    float br = grain(p * 62.0);\n"
"    vec3 bg = vec3(grain(p * 62.0 + vec3(0.017,0,0)) - br,\n"
"                   grain(p * 62.0 + vec3(0,0.017,0)) - br,\n"
"                   grain(p * 62.0 + vec3(0,0,0.017)) - br);\n"
/* bristles, not static: at 3.4 the surface stopped being a surface */
"    bn = normalize(bn - (bg - bn * dot(bg, bn)) * 0.85);\n"
"    vec3 bc = beastKind < 1.5 ? vec3(0.085, 0.045, 0.030)\n"
"            : beastKind < 2.5 ? vec3(0.060, 0.022, 0.020)\n"
"            : vec3(0.052, 0.030, 0.046);\n"
/* Lit by the ceiling like everything else in the room, or it reads as a
   sticker: a view-only lambert flattens the whole animal. */
"    vec3  ld2 = normalize(vec3(0.0, 1.0, 0.0) + vv * 0.35);\n"
"    float lam = max(dot(bn, ld2), 0.0);\n"
"    float amb = 0.35 + 0.65 * clamp(bn.y * 0.5 + 0.5, 0.0, 1.0);\n"
"    vec3  hv2 = normalize(ld2 + vv);\n"
"    float spec = pow(max(dot(bn, hv2), 0.0), 48.0);\n"
"    float rim  = pow(1.0 - max(dot(bn, vv), 0.0), 5.0);\n"
"    col  = bc * (0.22 * amb + 1.25 * lam) * (1.00 + 0.32 * uBlink);\n"
"    col += vec3(0.95, 0.92, 0.88) * spec * (0.95 + 0.30 * uBlink);\n"
"    col += vec3(0.40, 0.08, 0.06) * rim * 0.22;   // wet edge\n"
"    col += vec3(0.9, 0.95, 1.0) * beastStun * 0.50;\n"
"  }\n"
"  col = col / (col + vec3(0.9));\n"
"  col = pow(max(col, vec3(0.0)), vec3(0.4545));\n"
/* And the last of the flatness was the tone curve, not the lighting.
   Reinhard over 0.9 puts everything from a half to twice middle grey inside
   0.62..0.85 on the screen -- a fifth of the range for almost the whole
   picture -- so no amount of work on materials or shadow was going to reach
   the eye. An S over the top of it gives the corridor its blacks and its
   highlights back. Corridor only: the cave is dark on purpose, and a curve
   like this would take what little it has. */
"  col = mix(col, col*col*(3.0 - 2.0*col), 0.45 * uRoom);\n"
/* No air. The picture closes from the edges and what is left of it goes grey
   -- the same thing that happens to a person about to faint, which is what
   this is. Nothing about it is subtle on purpose. */
/* Washed. The dreamer gets wet, so it rains in the dream -- the most literal
   borrowing there is, and the only weather this building will ever have.
   Screen space streaks: there is no water in the field, only on the lens of
   whatever is looking, which is about right for something that is not really
   happening where you are. */
"  if (uRain > 0.001) {\n"
"    vec2  rq = vec2(uv.x * 26.0, uv.y * 2.4 - uTime * 1.35);\n"
"    float rc = floor(rq.x);\n"
"    float rs = fract(sin(rc * 91.7) * 4371.3);\n"
"    float rv = fract(rq.y + rs);\n"
"    float st = smoothstep(0.86, 0.995, rv) * smoothstep(0.16, 0.06, abs(fract(rq.x) - 0.5));\n"
"    col += vec3(0.72, 0.80, 0.86) * st * uRain * 0.30 * step(0.35, rs);\n"
"  }\n"
/* and the monitor going off does what an alarm does to a room: everything in
   it goes the colour of the alarm. */
"  col = mix(col, col * vec3(1.55, 0.62, 0.55) + vec3(0.10, 0.0, 0.0),\n"
"            uAlarm * 0.62);\n"
"  if (uChoke > 0.001) {\n"
"    float vg = smoothstep(0.02, 0.42, dot(uv, uv) * (0.55 + 1.45 * uChoke));\n"
"    col = mix(col, vec3(dot(col, vec3(0.333))), uChoke * 0.55);\n"
"    col *= 1.0 - uChoke * 0.92 * vg;\n"
"  }\n"
"  col += vec3(1.0, 0.98, 0.95) * uPulse * 0.10 * uRoom;\n"
"  col = mix(col, vec3(1.0), uWhite);\n"
"  FragColor = vec4(col, 1.0); }\n";

#endif /* SHADERS_H */
