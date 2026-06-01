// Standalone test bench for the third-person directional leg-yaw.
// No engine/renderer needed — replicates localVelocityFromWorld + the leg-yaw
// math with plain vector math, drives synthetic strafe inputs, and checks that
// the legs end up pointing along the movement direction (in world space).
//
// Build:  cl /EHsc /O2 tools\test_legyaw.cpp /Fe:tools\test_legyaw.exe
// Run:    tools\test_legyaw.exe

#include <cmath>
#include <cstdio>
#include <string>

struct V3 { float x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float len(V3 a) { return std::sqrt(dot(a, a)); }
static V3 norm(V3 a) { float l = len(a); return l > 1e-6f ? V3{a.x / l, a.y / l, a.z / l} : a; }

// Rodrigues rotation of v about unit axis k by angle (rad).
static V3 rotateAxis(V3 v, V3 k, float ang) {
    float c = std::cos(ang), s = std::sin(ang);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0f - c));
}

// Yaw rotation about +Y (matches glm angleAxis(yaw, +Y) used by worldTransform).
static V3 rotY(V3 v, float yaw) { return rotateAxis(v, {0, 1, 0}, yaw); }

// EXACT copy of AnimationLocomotion::localVelocityFromWorld.
struct LV { float forward, right; };
static LV localVelocityFromWorld(V3 velWorld, float yawRad) {
    float cy = std::cos(yawRad), sy = std::sin(yawRad);
    V3 forward{sy, 0, cy};
    V3 right{cy, 0, -sy};
    return {velWorld.x * forward.x + velWorld.z * forward.z, velWorld.x * right.x + velWorld.z * right.z};
}

// Signed angle from a to b about axis up (a,b assumed ~perp to up).
static float signedAngle(V3 a, V3 b, V3 up) { return std::atan2(dot(cross(a, b), up), dot(a, b)); }

// The leg-yaw under test. `rightMode`: 0 = anatomical thigh right (the BUG, -X),
// 1 = velocity-frame right = cross(up, fwd) (the FIX, +X).
static V3 legWorldDir(V3 velWorld, float yaw, int rightMode) {
    // Posed-frame model vectors (from the in-game [legyaw] log).
    const V3 up{0, 1, 0};
    const V3 feetFwd{0, 0, 1};      // legs point model +Z
    const V3 bodyFwd = feetFwd;
    const V3 thighRight{-1, 0, 0};  // measured rThigh-lThigh direction = -X
    const V3 bodyRight = (rightMode == 0) ? thighRight : norm(cross(up, bodyFwd));

    LV lv = localVelocityFromWorld(velWorld, yaw);
    float spd = std::sqrt(lv.forward * lv.forward + lv.right * lv.right);
    V3 desired = bodyFwd;
    if (spd > 2.0f) { V3 d = bodyFwd * lv.forward + bodyRight * lv.right; if (len(d) > 1e-4f) desired = norm(d); }
    float tgt = signedAngle(feetFwd, desired, up);
    V3 legModel = rotateAxis(feetFwd, up, tgt);  // rotate the legs in model space
    return norm(rotY(legModel, yaw));            // -> world space
}

int main() {
    const float SP = 350.0f;
    const float yaws[] = {0.0f, 0.6f, 1.5708f, 2.5f, 3.5f};
    const char* names[] = {"forward", "back", "right", "left", "fwd-right", "fwd-left"};
    int fails[2] = {0, 0};
    for (int mode = 0; mode <= 1; ++mode) {
        printf("\n=== rightMode=%d (%s) ===\n", mode, mode == 0 ? "thigh -X (current)" : "cross(up,fwd) +X (fix)");
        for (float yaw : yaws) {
            // facing frame in world
            V3 fwdW{std::sin(yaw), 0, std::cos(yaw)};
            V3 rightW{std::cos(yaw), 0, -std::sin(yaw)};
            V3 moves[6] = {fwdW, fwdW * -1.0f, rightW, rightW * -1.0f,
                           norm(fwdW + rightW), norm(fwdW - rightW)};
            for (int i = 0; i < 6; ++i) {
                V3 mv = moves[i];
                V3 vel = mv * SP;
                V3 leg = legWorldDir(vel, yaw, mode);
                float align = dot(leg, norm(mv)); // 1 = legs point exactly along movement
                bool ok = align > 0.7f;            // within ~45deg
                if (!ok) ++fails[mode];
                printf("  yaw=%.2f %-10s move(%.2f,%.2f,%.2f) leg(%.2f,%.2f,%.2f) align=%+.2f %s\n",
                       yaw, names[i], mv.x, mv.y, mv.z, leg.x, leg.y, leg.z, align, ok ? "OK" : "  <-- WRONG");
            }
        }
    }
    printf("\nRESULT: mode0(thigh -X) fails=%d | mode1(cross +X) fails=%d\n", fails[0], fails[1]);
    printf("%s\n", fails[1] == 0 ? "FIX (mode1) PASSES all cases." : "mode1 still failing — investigate further.");
    return 0;
}
