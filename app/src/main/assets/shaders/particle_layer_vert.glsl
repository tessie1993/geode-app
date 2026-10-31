#version 300 es
precision highp float;
precision highp sampler2D;

// core/viz/ParticleLayer: particle gl_VertexID's place, drift and life are hashes of its id and its
// cycle; its age is the music clock, so the particles move only while the music does.
uniform sampler2D uScene;
uniform float uClock;  // music time, seconds-ish
uniform float uFall;   // -1 fly up .. 1 fall
uniform float uSize;   // pixels
uniform float uRatio;  // width / height

out vec3 vColor;
out float vAlpha;

float hash(float n) { return fract(sin(n * 12.9898) * 43758.5453); }

void main() {
    float id = float(gl_VertexID);
    float life = 1.6 + 2.2 * hash(id * 1.7 + 0.3);
    float t = uClock + hash(id * 3.1 + 0.7) * life;
    float cycle = floor(t / life);
    float age = fract(t / life);
    float s = id * 7.13 + cycle * 1.37;
    vec2 origin = vec2(hash(s), hash(s + 1.1));
    vec2 drift = (vec2(hash(s + 2.3), hash(s + 3.7)) - 0.5) * vec2(0.3 / max(uRatio, 0.1), 0.3);
    float a = age * life;
    vec2 pos = origin + drift * a + vec2(0.0, -uFall * 0.12) * a * a;
    vColor = texture(uScene, origin).rgb;
    // Born and gone softly; bright spots of the picture throw brighter particles.
    vAlpha = sin(3.14159265 * age) * (0.4 + 0.6 * clamp(dot(vColor, vec3(0.333)) * 1.5, 0.0, 1.0));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = uSize * (0.5 + hash(s + 5.0)) * (1.0 - 0.5 * age);
}
