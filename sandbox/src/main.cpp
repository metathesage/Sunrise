// main.cpp - test harness + scripted demo for the animation sandbox.
//
//   anim_sandbox test    -> run the invariant tests from doc section 8.2
//   anim_sandbox demo    -> print a deterministic scripted trace
#include "anim_sim.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace anim;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const std::string& name, const std::string& detail = "") {
    if (cond) { g_pass++; std::printf("  [PASS] %s\n", name.c_str()); }
    else { g_fail++; std::printf("  [FAIL] %s   %s\n", name.c_str(), detail.c_str()); }
}

static void run(Sim& s, float seconds, float dt = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds - 1e-6f; t += dt) s.step(dt);
}

// ---------------------------------------------------------------------------
// Tests (mirror the invariants in the documentation, section 8.2)
// ---------------------------------------------------------------------------
static void test_reload_refill() {
    Sim s; s.init(); s.ammo = 0;
    s.in.reload.press(s.now);
    run(s, 1.5f);
    check(s.ammo == 0, "reload: ammo unchanged before ammo_refill notify");
    run(s, 0.2f);
    check(s.ammo == s.weapon.mag_size, "reload: refilled AT ammo_refill (not clip end)");
}

static void test_reload_cancel() {
    Sim s; s.init(); s.ammo = 0;
    s.in.reload.press(s.now);
    run(s, 1.0f);
    s.interrupt_reload();
    check(s.ammo == 0, "cancel: ammo unchanged after interrupt before refill");
    check(s.state == WeaponState::Idle, "cancel: returns to Idle");
}

static void test_no_dupe_fire() {
    Sim s; s.init(); s.ammo = 2;
    s.in.reload.press(s.now);
    run(s, 0.2f);
    check(s.state == WeaponState::Reload, "no-dupe: entered Reload");
    s.in.fire.press(s.now);
    run(s, 0.5f);
    check(s.ammo == 2, "no-dupe: fire during reload consumes nothing");
}

static void test_input_buffer() {
    Sim s; s.init(); s.ammo = 3;
    s.in.fire.press(s.now);
    run(s, 0.15f);
    s.in.reload.press(s.now);       // pressed mid-fire, buffered
    run(s, 0.25f);                  // fire ends at ~0.28s
    check(s.state == WeaponState::Reload, "buffer: reload pressed during fire plays after");
}

static void test_emotes() {
    Sim s; s.init();
    s.in.emote_dance.press(s.now); run(s, 0.02f);
    check(s.emote_active && !s.can_move(), "emote: full-body blocks movement");

    Sim s2; s2.init();
    s2.in.emote_wave.press(s2.now); run(s2, 0.02f);
    check(s2.emote_active && s2.can_move(), "emote: upper-body allows movement");

    Sim s3; s3.init();
    s3.in.emote_dance.press(s3.now); run(s3, 0.02f);
    s3.apply_damage();
    check(!s3.emote_active, "emote: damage always cancels");
}

static void test_recoil() {
    Sim s; s.init(); s.ammo = 8;
    const float expected = s.weapon.recoil_pitch_deg * s.weapon.cam_split;
    s.in.fire.press(s.now); s.step(1.0f / 60.0f);
    check(std::fabs(s.cam_pitch.spring.x - expected) < 1e-3f,
          "recoil: peak equals configured kick", "x=" + std::to_string(s.cam_pitch.spring.x));
    run(s, 1.0f);
    check(s.cam_pitch.spring.settled(0.05f), "recoil: settles within 1s");

    Sim s2; s2.init();
    for (int i = 0; i < 20; ++i) { s2.in.fire.press(s2.now); s2.step(0.2f); }
    check(std::fabs(s2.cam_pitch.spring.x) < 50.0f,
          "recoil: bounded under dt spikes (sub-stepping)");
}

static void test_ads() {
    Sim s; s.init();
    s.in.ads.press(s.now);
    int steps = 0;
    while (s.aim < 0.999f && steps < 2000) { s.step(1.0f / 60.0f); steps++; }
    const float secs = steps / 60.0f;
    check(std::fabs(secs - s.weapon.ads_in) < 0.05f, "ads: blend time ~ ads_in",
          "t=" + std::to_string(secs));
}

static void scenario(Sim& s) {
    s.init();
    s.in.fire.press(s.now); s.step(1.0f / 60.0f);
    s.step(1.0f / 60.0f); s.step(1.0f / 60.0f);
    s.in.fire.press(s.now); s.step(1.0f / 60.0f);
    s.in.reload.press(s.now);
    for (int i = 0; i < 200; ++i) s.step(1.0f / 60.0f);
    s.in.ads.press(s.now);
    for (int i = 0; i < 30; ++i) s.step(1.0f / 60.0f);
}

static void test_determinism() {
    Sim a; Sim b; scenario(a); scenario(b);
    check(a.transitions == b.transitions && a.events == b.events,
          "determinism: identical transition/event logs across runs");
}

static int run_all_tests() {
    std::printf("Running animation-sandbox invariant tests\n");
    std::printf("------------------------------------------\n");
    test_reload_refill();
    test_reload_cancel();
    test_no_dupe_fire();
    test_input_buffer();
    test_emotes();
    test_recoil();
    test_ads();
    test_determinism();
    std::printf("------------------------------------------\n");
    std::printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Demo - a scripted, deterministic trace
// ---------------------------------------------------------------------------
static void demo() {
    Sim s; s.init();
    std::printf("t      state     ammo aim  cam_pitch  event\n");
    std::printf("-----  --------  ----  ---  ---------  -------------------------------\n");

    float t = 0.0f;
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < 240; ++i) {   // 8 seconds
        if (i == 3)   s.in.fire.press(s.now);
        if (i == 9)   s.in.fire.press(s.now);
        if (i == 15)  s.in.fire.press(s.now);
        if (i == 30)  s.in.reload.press(s.now);
        if (i == 120) s.in.emote_wave.press(s.now);
        if (i == 150) s.apply_damage();
        if (i == 180) s.in.ads.press(s.now);

        s.step(dt);
        t += dt;

        const int w = 20;
        const float lo = -3.0f, hi = 3.0f;
        int pos = static_cast<int>((s.camera_pitch() - lo) / (hi - lo) * w);
        if (pos < 0) pos = 0;
        if (pos > w) pos = w;
        std::string bar(w + 1, ' ');
        bar[pos] = '|';

        std::string ev;
        if (!s.events.empty()) { ev = s.events.back(); s.events.clear(); }

        std::printf("%5.2f  %-8s  %4d  %.2f  [%s]  %s\n",
                    t, to_string(s.state), s.ammo, s.aim, bar.c_str(), ev.c_str());
    }
    std::printf("\nFinal: state=%s ammo=%d fov=%.1f cam_pitch=%.3f\n",
                to_string(s.state), s.ammo, s.fov(), s.camera_pitch());
}

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "test";
    if (mode == "test") return run_all_tests();
    if (mode == "demo") { demo(); return 0; }
    std::printf("usage: %s [test|demo]\n", argv[0]);
    return 2;
}

