// anim_sim.hpp - deterministic, dependency-free model of a Destiny-style
// character/weapon animation stack: state machine + notify/event timeline +
// additive recoil springs + sway/breathing + ADS blend + emote priority +
// buffered input commands.
//
// This is the *runnable* companion to documentation/animation-character-systems.md.
// It contains no game data; it is a synthetic model meant to be tested and tuned.
#pragma once

#include <string>
#include <vector>

namespace anim {

constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Spring - a damped harmonic oscillator used for recoil recovery.
//   omega = 2*pi*f ; k = omega^2 ; c = 2*zeta*omega
// ---------------------------------------------------------------------------
struct Spring {
    float x = 0.0f;         // current offset (degrees)
    float v = 0.0f;         // velocity (deg/s)
    float freq_hz = 9.0f;   // natural frequency
    float zeta = 1.0f;      // damping ratio (1 = critically damped)

    void reset() { x = 0.0f; v = 0.0f; }
    void configure(float f, float z) { freq_hz = f; zeta = z; }
    void kick(float amount_deg) { x += amount_deg; v = 0.0f; }  // instant peak
    void update(float dt);
    bool settled(float eps = 1e-3f) const;
};

// Recoil axis = spring + a short "hold" during which recovery is frozen so the
// kick reads before it springs back.
struct RecoilAxis {
    Spring spring;
    float hold_s = 0.12f;
    float hold_left = 0.0f;

    void fire(float kick_deg);
    void update(float dt);
};

struct SwayConfig {
    float breath_hz = 0.25f;
    float breath_amp_deg = 0.6f;
    float ads_scale = 0.35f;   // amplitude multiplier while aiming
};

enum class WeaponState { Idle, Fire, Reload, Ads, AdsFire };
const char* to_string(WeaponState s);

enum class EmoteMask { Full, Upper };

struct EmoteDef {
    std::string name = "wave";
    EmoteMask mask = EmoteMask::Upper;
    bool loop = false;
    int priority = 40;
    bool allow_move = true;
    bool cancel_on_fire = true;
};

struct Notify {
    float t = 0.0f;
    std::string name;
    bool gameplay = false;
};

struct ReloadTimeline {
    float mag_eject = 0.35f;
    float mag_insert = 0.90f;
    float ammo_refill = 1.60f;   // <-- gameplay truth lives here, not clip end
    float reload_end = 2.30f;
    std::vector<Notify> notifies() const;
};

struct WeaponDef {
    std::string name = "hand_cannon";
    int mag_size = 8;
    float fire_interval = 0.30f;
    float fire_clip = 0.28f;
    float reload_time = 2.30f;
    float recoil_pitch_deg = -2.4f;
    float recoil_yaw_deg = 0.35f;
    float recoil_freq = 9.0f;
    float recoil_zeta = 1.0f;
    float recovery_hold = 0.12f;
    float cam_split = 0.6f;      // camera vs weapon share
    float ads_in = 0.18f;
    float ads_out = 0.14f;
    float fov_hip = 90.0f;
    float fov_aim = 55.0f;
};

// A buffered input command: edge-triggered with a forgiveness window.
struct Command {
    bool held = false;
    float pressed_at = -1e9f;

    void press(float now) { held = true; pressed_at = now; }
    void release() { held = false; }
    bool buffered(float now, float window) const {
        return (now - pressed_at) <= window;
    }
    void consume() { pressed_at = -1e9f; }
    void clear() { pressed_at = -1e9f; held = false; }
};

struct Input {
    Command fire, reload, ads, emote_wave, emote_dance;
    float buffer_window = 0.20f;
};

// ---------------------------------------------------------------------------
// Sim - the deterministic simulation. Always advance it with step(dt); it
// internally sub-steps to keep springs stable.
// ---------------------------------------------------------------------------
struct Sim {
    WeaponDef weapon;
    ReloadTimeline reload;
    Input in;
    SwayConfig sway;

    WeaponState state = WeaponState::Idle;
    int ammo = 0;
    int spare = 999;
    float now = 0.0f;
    float state_t = 0.0f;
    float fire_cd = 0.0f;
    float aim = 0.0f;

    RecoilAxis cam_pitch, cam_yaw, weapon_kick;

    bool emote_active = false;
    EmoteDef emote;
    float emote_t = 0.0f;

    std::vector<std::string> transitions;
    std::vector<std::string> events;
    std::vector<Notify> fired;   // notifies fired during the last step()

    unsigned rng_state = 0x1234abcd;  // deterministic yaw jitter

    void init();
    void step(float dt);

    void set_state(WeaponState s);
    bool can_move() const;
    void apply_damage();
    void interrupt_reload();     // simulate weapon swap / hard interrupt
    bool start_emote(const EmoteDef& def);

    float camera_pitch() const;  // recoil + sway (degrees)
    float fov() const;

private:
    void step_inner(float h);
    float rand_sym();            // deterministic in [-1, 1]
    void log_transition(const std::string& msg);
    void log_event(const std::string& msg);
};

} // namespace anim
