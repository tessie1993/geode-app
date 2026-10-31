#version 300 es
precision highp float;

// MYCELIUM - the agent pass. One texel = one agent: (x, y, heading, species),
// positions in [0,1) toroidal trail space. The heading is STORED AS A TURN
// (heading / TAU, in [0,1)) rather than in radians: the agent texture rides
// RGBA8 on devices with no renderable float format, where a normalized
// texel clamps to [0,1] and a radian heading above 1 collapsed the whole
// population onto one bearing. Every channel now fits any format by
// construction; the pass decodes to radians on read and encodes on write.
//
// The rule is the published Physarum machine, reimplemented: sense the trail
// at three probes (ahead, +/- sensor angle at sensor distance), turn by the
// classic table - straight when ahead is strongest, a random turn when ahead
// is weakest, otherwise toward the stronger side - then step forward and
// wrap. Two species sense a COMBINED field through a 2x2 matrix (self and
// cross weights), which is where rivalry, symbiosis and predation live.
//
// Sensing is compressed (1 - exp(-k*t)) so saturated highways stop out-
// shouting faint new veins, and the sensor distance BREATHES on the beat -
// agents look further on the hit, which visibly re-organizes the network in
// rhythm without teleporting anything.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uAgents;
uniform sampler2D uTrail;
uniform vec2 uTrailRes;
uniform float uSensorDist;   // texels of trail space
uniform float uSensorAngle;  // radians
uniform float uTurnAngle;    // radians
uniform float uMoveStep;     // texels per frame
uniform vec4 uMatrix;        // (A<-A, A<-B, B<-A, B<-B) sense weights
uniform float uBreath;       // beat envelope onto sensor distance
uniform float uJitter;       // heading noise, radians
uniform float uSnap;         // >0: headings quantized to this many radians
uniform float uReaim;        // beat scatter: fraction of agents re-aimed
uniform float uTime;
uniform float uAniso;        // vertical sensing bias for frost-like growth
uniform float uInit;         // 1 = write a fresh random population and stop
uniform float uSpeciesMix;   // fraction of agents born as species B

// ---- the finger as a spore fall --------------------------------------------
//
// The colony has no seeding pass after uInit - it is the same population from
// the first frame to the last - so the only way a finger can mean anything
// here is to be where some of those agents are BORN AGAIN. A hashed slice of
// the population is relocated under the fingertip each frame with headings
// pointing outward, and it then walks and deposits like every other agent, so
// what appears is a real colony growing out of the touch rather than a mark
// drawn on top of one. See SceneTouch.kt for the packing (xy is y-up NDC,
// which is also agent/trail space once mapped through xy*0.5 + 0.5).
#define TOUCH_MAX_POINTS 5
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots, including ones still fading after release. 0 = nothing touched. */
uniform int uTouchCount;
/** Fraction of the population reborn at a finger THIS FRAME; MycoScene scales it by dt. */
uniform float uTouchBirth;
/** Radius of the spore cloud, in trail-space units. */
#define TOUCH_SPAWN_SPREAD 0.05

const float TAU = 6.2831853;

float hash1(vec2 q) {
    return fract(sin(dot(q, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 hash2(vec2 q) {
    q = vec2(dot(q, vec2(127.1, 311.7)), dot(q, vec2(269.5, 183.3)));
    return fract(sin(q) * 43758.5453);
}

float senseAt(vec2 pos, float species) {
    vec2 t = texture(uTrail, fract(pos)).rg;
    // Compressed sensing, tuned to the field's real levels: steady trunks sit
    // in the tens after additive deposit, and a constant that saturates there
    // makes every probe read alike - all ties, all agents walking straight,
    // and the network hardens into a rectangular grid. At 0.18 the trunks
    // still differ from the veins and the turning stays organic.
    t = 1.0 - exp(-0.18 * t);
    return species < 0.5 ? dot(t, uMatrix.xy) : dot(t, uMatrix.zw);
}

void main() {
    if (uInit > 0.5) {
        // Fresh population: uniform positions, uniform headings, species by
        // the declared mix - the reference machine's uniform-noise start.
        vec2 h = hash2(vUv * 719.7);
        // Already a turn in [0,1): the storage encoding, no TAU here.
        float heading0 = hash1(vUv * 149.3);
        float species0 = step(1.0 - uSpeciesMix, hash1(vUv * 449.1 + 3.3));
        fragColor = vec4(h, heading0, species0);
        return;
    }
    vec4 a = texture(uAgents, vUv);
    vec2 pos = a.xy;
    float heading = a.z * TAU; // stored as a turn; the math runs in radians
    float species = a.w;

    vec2 texel = 1.0 / uTrailRes;
    float dist = uSensorDist * (1.0 + 0.45 * uBreath);
    vec2 stretch = vec2(1.0, 1.0 + uAniso); // frost styles look further up/down

    vec2 fwd = vec2(cos(heading), sin(heading));
    vec2 lft = vec2(cos(heading + uSensorAngle), sin(heading + uSensorAngle));
    vec2 rgt = vec2(cos(heading - uSensorAngle), sin(heading - uSensorAngle));
    float sC = senseAt(pos + fwd * stretch * dist * texel, species);
    float sL = senseAt(pos + lft * stretch * dist * texel, species);
    float sR = senseAt(pos + rgt * stretch * dist * texel, species);

    float rnd = hash1(vUv * 977.0 + fract(uTime) * 61.7 + species);
    // Per-agent turn personality: identical turn angles synchronize the
    // whole population onto one heading lattice, and the network crystallizes
    // into right angles. A fixed +-20% spread per agent keeps the machine
    // deterministic per agent while the colony stays organic.
    float turn = uTurnAngle * (0.8 + 0.4 * hash1(vUv * 57.31));
    if (sC >= sL && sC >= sR) {
        // straight on
    } else if (sC < sL && sC < sR) {
        heading += (rnd < 0.5 ? -1.0 : 1.0) * turn;
    } else if (sL > sR) {
        heading += turn;
    } else {
        heading -= turn;
    }
    heading += (rnd - 0.5) * uJitter;

    // Beat scatter: a hashed subset of agents re-aims outward from centre,
    // so a drop reads as a spore burst rather than a global twitch.
    if (uReaim > 0.0 && hash1(vUv * 313.0 + 7.7) < uReaim) {
        vec2 fromCentre = pos - 0.5;
        heading = atan(fromCentre.y, fromCentre.x) + (rnd - 0.5) * 0.8;
    }

    // Spore fall: a slice of the colony is reborn under a finger, aimed out of
    // the cloud it lands in, so the touch grows a network instead of stamping a
    // blob. The slice is a per-frame FRACTION and the scene keeps it small
    // (MycoScene.TOUCH_BIRTH_PER_SECOND) for a reason that is visible the
    // moment it is not: every relocated agent stops maintaining the road it was
    // on, and above roughly a third of the population per second the existing
    // network dies faster than the new one grows and the screen goes empty.
    if (uTouchCount > 0 && hash1(vUv * 613.0 + fract(uTime) * 53.3) < uTouchBirth) {
        // Pick a finger by hash rather than always slot 0, so several fingers
        // split one birth budget instead of each spending it.
        float pick = hash1(vUv * 271.0 + 5.9) * float(uTouchCount);
        vec4 tp = vec4(0.0);
        for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
            if (i >= uTouchCount) break;
            if (float(i) <= pick && pick < float(i) + 1.0) tp = uTouchPoints[i];
        }
        if (tp.z > 0.0) {
            vec2 off = hash2(vUv * 811.0 + fract(uTime) * 29.3) - 0.5;
            pos = fract(tp.xy * 0.5 + 0.5 + off * TOUCH_SPAWN_SPREAD);
            // Outward from where it landed in the cloud: the burst disperses
            // instead of every newborn setting off on the same bearing.
            heading = atan(off.y, off.x + 1e-5);
        }
    }

    if (uSnap > 0.0) heading = floor(heading / uSnap + 0.5) * uSnap;
    heading = mod(heading, TAU);

    pos = fract(pos + vec2(cos(heading), sin(heading)) * uMoveStep * texel);

    // Encode back to a turn: mod above keeps heading / TAU inside [0,1), so
    // the write survives a normalized RGBA8 target unclamped.
    fragColor = vec4(pos, heading / TAU, species);
}
