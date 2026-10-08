#include "anim_sim.hpp"

#include <algorithm>
#include <cmath>

namespace anim {

// ---------------------------------------------------------------------------
// Spring / RecoilAxis
// ---------------------------------------------------------------------------
void Spring::update(float dt) {
    if (dt <= 0.0f) return;
    // Closed-form solution of x'' + 2*zeta*omega*x' + omega^2*x = 0.
    // Analytic integration is exact and unconditionally stable, unlike Euler
    // (which blows up for stiff springs like a 9 Hz recoil spring at 60 fps).
    const float w = 2.0f * kPi * freq_hz;
    const float x0 = x, v0 = v;

    if (std::fabs(zeta - 1.0f) < 1e-4f) {
        // Critically damped: x = (x0 + (v0 + w*x0) t) e^{-w t}
        const float e = std::exp(-w * dt);
        const float c1 = v0 + w * x0;
        x = (x0 + c1 * dt) * e;
        v = (v0 - w * c1 * dt) * e;
    } else if (zeta < 1.0f) {
        // Underdamped.
        const float wd = w * std::sqrt(1.0f - zeta * zeta);
        const float e = std::exp(-zeta * w * dt);
        const float a = x0;
        const float b = (v0 + zeta * w * x0) / wd;
        const float ct = std::cos(wd * dt), st = std::sin(wd * dt);
        x = e * (a * ct + b * st);
        v = e * ((-zeta * w * a + b * wd) * ct + (-zeta * w * b - a * wd) * st);
    } else {
        // Overdamped: sum of two real exponentials.
        const float s = std::sqrt(zeta * zeta - 1.0f);
        const float r1 = w * (-zeta + s);
        const float r2 = w * (-zeta - s);
        const float c1 = (v0 - r2 * x0) / (r1 - r2);
        const float c2 = (r1 * x0 - v0) / (r1 - r2);
        const float e1 = std::exp(r1 * dt), e2 = std::exp(r2 * dt);
        x = c1 * e1 + c2 * e2;
        v = c1 * r1 * e1 + c2 * r2 * e2;
    }
    if (std::fabs(x) < 1e-4f && std::fabs(v) < 1e-3f) { x = 0.0f; v = 0.0f; }
}

bool Spring::settled(float eps) const {
    return std::fabs(x) < eps && std::fabs(v) < 1e-2f;
}

void RecoilAxis::fire(float kick_deg) {
    spring.kick(kick_deg);
    hold_left = hold_s;
}

void RecoilAxis::update(float dt) {
    if (hold_left > 0.0f) {
        hold_left -= dt;
        if (hold_left > 0.0f) return;   // recovery frozen during hold
    }
    spring.update(dt);
}

const char* to_string(WeaponState s) {
    switch (s) {
        case WeaponState::Idle:    return "Idle";
        case WeaponState::Fire:    return "Fire";
        case WeaponState::Reload:  return "Reload";
        case WeaponState::Ads:     return "Ads";
        case WeaponState::AdsFire: return "AdsFire";
    }
    return "?";
}

std::vector<Notify> ReloadTimeline::notifies() const {
    return {
        {mag_eject,    "mag_eject",    false},
        {mag_insert,   "mag_insert",   false},
        {ammo_refill,  "ammo_refill",  true},
        {reload_end,   "reload_end",   true},
    };
}

// ---------------------------------------------------------------------------
// Sim - setup & helpers
// ---------------------------------------------------------------------------
void Sim::init() {
    ammo = weapon.mag_size;
    spare = 999;
    state = WeaponState::Idle;
    now = 0.0f;
    state_t = 0.0f;
    fire_cd = 0.0f;
    aim = 0.0f;

    cam_pitch.spring.configure(weapon.recoil_freq, weapon.recoil_zeta);
    cam_yaw.spring.configure(weapon.recoil_freq, weapon.recoil_zeta);
    weapon_kick.spring.configure(weapon.recoil_freq, weapon.recoil_zeta);
    cam_pitch.hold_s = cam_yaw.hold_s = weapon_kick.hold_s = weapon.recovery_hold;
    cam_pitch.spring.reset();
    cam_yaw.spring.reset();
    weapon_kick.spring.reset();

    emote_active = false;
    emote_t = 0.0f;
    transitions.clear();
    events.clear();
    fired.clear();
    rng_state = 0x1234abcd;
}

float Sim::rand_sym() {
    // xorshift32 -> [-1, 1]; deterministic so replays match.
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    const float u = static_cast<float>(rng_state & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
    return u * 2.0f - 1.0f;
}

void Sim::log_transition(const std::string& msg) { transitions.push_back(msg); }
void Sim::log_event(const std::string& msg) { events.push_back(msg); }

void Sim::set_state(WeaponState s) {
    if (s == state) return;
    log_transition(std::string(to_string(state)) + " -> " + to_string(s));
    state = s;
    state_t = 0.0f;
}

bool Sim::can_move() const {
    // Full-body emotes suspend locomotion; upper-body emotes do not.
    if (emote_active && emote.mask == EmoteMask::Full) return false;
    return true;
}

void Sim::apply_damage() {
    // Damage has the highest social-cancel priority: it always kills an emote.
    if (emote_active) {
        log_event("damage cancels emote(" + emote.name + ")");
        emote_active = false;
        emote_t = 0.0f;
    }
}

void Sim::interrupt_reload() {
    if (state == WeaponState::Reload) {
        log_event("reload interrupted (weapon swap)");
        set_state(WeaponState::Idle);
        // ammo intentionally untouched: only the ammo_refill notify may change it
    }
}

bool Sim::start_emote(const EmoteDef& def) {
    if (state != WeaponState::Idle) return false;   // no emotes mid weapon-action
    if (emote_active) return false;                 // already emoting (toggle handled elsewhere)
    emote = def;
    emote_active = true;
    emote_t = 0.0f;
    log_event("emote start: " + def.name);
    return true;
}

float Sim::camera_pitch() const {
    const float breath = sway.breath_amp_deg *
        std::sin(2.0f * kPi * sway.breath_hz * now) *
        (1.0f - aim * (1.0f - sway.ads_scale));
    return cam_pitch.spring.x + breath;
}

float Sim::fov() const {
    return weapon.fov_hip + (weapon.fov_aim - weapon.fov_hip) * aim;
}

void Sim::step(float dt) {
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.25f);          // clamp pathological frames
    const float h_max = 1.0f / 60.0f;  // fixed sub-step keeps springs stable
    while (dt > 1e-6f) {
        const float h = std::min(dt, h_max);
        step_inner(h);
        dt -= h;
    }
}

void Sim::step_inner(float h) {
    fired.clear();

    const WeaponState s0 = state;
    const float prev_t = state_t;

    now += h;
    state_t += h;
    fire_cd -= h;
    if (emote_active) emote_t += h;

    // ---- ADS blend (single scalar drives FOV + pose + sway) -------------
    const bool ads_allowed = (state == WeaponState::Idle) ||
                             (state == WeaponState::Ads) ||
                             (state == WeaponState::AdsFire) ||
                             (state == WeaponState::Fire);
    const float target = (ads_allowed && in.ads.held) ? 1.0f : 0.0f;
    const float rate = (target > aim) ? (1.0f / weapon.ads_in) : (1.0f / weapon.ads_out);
    const float d = target - aim;
    const float stepv = rate * h;
    if (std::fabs(d) <= stepv) aim = target;
    else aim += (d > 0.0f ? stepv : -stepv);
    aim = std::clamp(aim, 0.0f, 1.0f);

    // ---- Emote toggles (edge, buffered) --------------------------------
    if (in.emote_wave.buffered(now, in.buffer_window)) {
        in.emote_wave.consume();
        if (emote_active) { emote_active = false; log_event("emote cancel (toggle)"); }
        else { const EmoteDef w{"wave", EmoteMask::Upper, false, 40, true, true}; start_emote(w); }
    }
    if (in.emote_dance.buffered(now, in.buffer_window)) {
        in.emote_dance.consume();
        if (emote_active) { emote_active = false; log_event("emote cancel (toggle)"); }
        else { const EmoteDef e{"dance", EmoteMask::Full, true, 50, false, true}; start_emote(e); }
    }

    // ---- Reload command (edge, buffered) -------------------------------
    if (in.reload.buffered(now, in.buffer_window)) {
        const bool can_start = (state == WeaponState::Idle || state == WeaponState::Ads) &&
                               ammo < weapon.mag_size && spare > 0;
        if (can_start) {
            in.reload.consume();
            log_event("reload begin");
            set_state(WeaponState::Reload);
        }
    }

    // ---- Fire command (edge, buffered) ---------------------------------
    if (in.fire.buffered(now, in.buffer_window)) {
        const bool can_fire = (state == WeaponState::Idle || state == WeaponState::Ads) &&
                              fire_cd <= 0.0f;
        if (can_fire) {
            in.fire.consume();
            if (emote_active && emote.cancel_on_fire) {
                emote_active = false;
                log_event("fire cancels emote");
            }
            if (ammo > 0) {
                ammo -= 1;
                const bool ads = (state == WeaponState::Ads);
                set_state(ads ? WeaponState::AdsFire : WeaponState::Fire);
                cam_pitch.fire(weapon.recoil_pitch_deg * weapon.cam_split);
                weapon_kick.fire(weapon.recoil_pitch_deg * (1.0f - weapon.cam_split));
                cam_yaw.fire(rand_sym() * weapon.recoil_yaw_deg);
                fire_cd = weapon.fire_interval;
                log_event("fire (ammo=" + std::to_string(ammo) + ")");
            } else {
                log_event("dry fire");
            }
        }
    }

    // ---- End-of-state transitions (only if state did not just change) ---
    if (state == s0 && s0 == WeaponState::Reload) {
        for (const Notify& nt : reload.notifies()) {
            if (prev_t < nt.t && state_t >= nt.t) {
                fired.push_back(nt);
                if (nt.name == "ammo_refill") {
                    ammo = weapon.mag_size;   // gameplay truth on the notify
                    log_event("NOTIFY ammo_refill -> mag=" + std::to_string(ammo));
                } else {
                    log_event("NOTIFY " + nt.name);
                }
            }
        }
        if (state_t >= reload.reload_end) {
            log_event("reload end");
            set_state(in.ads.held ? WeaponState::Ads : WeaponState::Idle);
        }
    }
    if (state == s0 && (s0 == WeaponState::Fire || s0 == WeaponState::AdsFire)) {
        if (state_t >= weapon.fire_clip) {
            set_state(in.ads.held ? WeaponState::Ads : WeaponState::Idle);
        }
    }

    // ---- Springs --------------------------------------------------------
    cam_pitch.update(h);
    cam_yaw.update(h);
    weapon_kick.update(h);

    // ---- Expire stale buffered edges -----------------------------------
    if (!in.fire.buffered(now, in.buffer_window)) in.fire.consume();
    if (!in.reload.buffered(now, in.buffer_window)) in.reload.consume();
    if (!in.emote_wave.buffered(now, in.buffer_window)) in.emote_wave.consume();
    if (!in.emote_dance.buffered(now, in.buffer_window)) in.emote_dance.consume();
}


} // namespace anim
