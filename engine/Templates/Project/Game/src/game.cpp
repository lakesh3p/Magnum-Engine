// Sample game module. Edit this file (or add .cpp files next to it) while the editor is open:
// it rebuilds, hot reloads, and component values are kept.
//
// A component is a plain struct plus a property table. Press Play in the viewport to run on_begin/on_update.
#include "magnum_game_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static const MagnumHostApi* g_host = nullptr;

static void Log(const char* message) {
    g_host->log(MAGNUM_LOG_INFO, message);
}

// ---- Spinner: rotates its actor at a constant speed ----------------------------------------------------

struct Spinner {
    float speed[3]; // degrees per second around X (pitch), Y (yaw), Z (roll)
};

static const MagnumPropertyDesc kSpinnerProps[] = {
    MAGNUM_VEC3(Spinner, speed, "Spin"),
};

static void SpinnerConstruct(void* data) {
    Spinner* s = static_cast<Spinner*>(data);
    s->speed[0] = 0.0f;
    s->speed[1] = 90.0f;
    s->speed[2] = 0.0f;
}

static void SpinnerUpdate(MagnumActorId actor, void* data, float dt) {
    const Spinner* s = static_cast<const Spinner*>(data);
    MagnumTransform t;
    if (!g_host->get_transform(actor, &t)) return;
    for (int i = 0; i < 3; ++i) t.rotation[i] += s->speed[i] * dt;
    g_host->set_transform(actor, &t);
}

// ---- Bobber: moves up and down around where it started -------------------------------------------------

struct Bobber {
    float height;     // peak distance from the starting height
    float frequency;  // cycles per second
    float base_y;     // remembered on begin; hidden, but saved so a hot reload does not lose it
    float phase;
};

static const MagnumPropertyDesc kBobberProps[] = {
    MAGNUM_FLOAT(Bobber, height, "Bob", 0.0f, 3.0f, 0.01f),
    MAGNUM_FLOAT(Bobber, frequency, "Bob", 0.0f, 5.0f, 0.01f),
    MAGNUM_HIDDEN_FLOAT(Bobber, base_y),
    MAGNUM_HIDDEN_FLOAT(Bobber, phase),
};

static void BobberConstruct(void* data) {
    Bobber* b = static_cast<Bobber*>(data);
    b->height = 0.5f;
    b->frequency = 0.5f;
    b->base_y = 0.0f;
    b->phase = 0.0f;
}

static void BobberBegin(MagnumActorId actor, void* data) {
    Bobber* b = static_cast<Bobber*>(data);
    MagnumTransform t;
    if (g_host->get_transform(actor, &t)) b->base_y = t.position[1];
    b->phase = 0.0f;
}

static void BobberUpdate(MagnumActorId actor, void* data, float dt) {
    Bobber* b = static_cast<Bobber*>(data);
    MagnumTransform t;
    if (!g_host->get_transform(actor, &t)) return;
    b->phase += dt * b->frequency * 6.2831853f;
    t.position[1] = b->base_y + std::sin(b->phase) * b->height;
    g_host->set_transform(actor, &t);
}

static void BobberEnd(MagnumActorId actor, void* data) {
    // Put the actor back where it started; the editor restores the scene on Stop anyway.
    const Bobber* b = static_cast<const Bobber*>(data);
    MagnumTransform t;
    if (g_host->get_transform(actor, &t)) {
        t.position[1] = b->base_y;
        g_host->set_transform(actor, &t);
    }
}

// ---- Mover: WASD moves the actor on the ground plane while playing ---------------------------------------

struct Mover {
    float speed;       // meters per second
    bool sprint_with_shift;
    char label[32];    // example of a text property
};

static const MagnumPropertyDesc kMoverProps[] = {
    MAGNUM_FLOAT(Mover, speed, "Movement", 0.0f, 20.0f, 0.05f),
    MAGNUM_BOOL(Mover, sprint_with_shift, "Movement"),
    MAGNUM_STRING(Mover, label, "Info"),
};

static void MoverConstruct(void* data) {
    Mover* m = static_cast<Mover*>(data);
    m->speed = 3.0f;
    m->sprint_with_shift = true;
    std::snprintf(m->label, sizeof(m->label), "%s", "Player");
}

static void MoverUpdate(MagnumActorId actor, void* data, float dt) {
    const Mover* m = static_cast<const Mover*>(data);
    float dx = 0.0f, dz = 0.0f;
    if (g_host->key_down(MAGNUM_KEY_W)) dz -= 1.0f;
    if (g_host->key_down(MAGNUM_KEY_S)) dz += 1.0f;
    if (g_host->key_down(MAGNUM_KEY_A)) dx -= 1.0f;
    if (g_host->key_down(MAGNUM_KEY_D)) dx += 1.0f;
    if (dx == 0.0f && dz == 0.0f) return;

    const float length = std::sqrt(dx * dx + dz * dz);
    float speed = m->speed;
    if (m->sprint_with_shift && g_host->key_down(MAGNUM_KEY_LEFT_SHIFT)) speed *= 2.0f;

    MagnumTransform t;
    if (!g_host->get_transform(actor, &t)) return;
    t.position[0] += dx / length * speed * dt;
    t.position[2] += dz / length * speed * dt;
    g_host->set_transform(actor, &t);
}

// ---- Module entry points -------------------------------------------------------------------------------

template <class T, size_t N>
static void Register(const char* name, const MagnumPropertyDesc (&props)[N], void (*construct)(void*),
                     void (*begin)(MagnumActorId, void*), void (*update)(MagnumActorId, void*, float),
                     void (*end)(MagnumActorId, void*)) {
    MagnumComponentDesc d = {};
    d.name = name;
    d.size = sizeof(T);
    d.properties = props;
    d.property_count = static_cast<uint32_t>(N);
    d.construct = construct;
    d.on_begin = begin;
    d.on_update = update;
    d.on_end = end;
    g_host->register_component(&d);
}

extern "C" {

MAGNUM_EXPORT int32_t Magnum_OnLoad(const MagnumHostApi* host) {
    if (host->version != MAGNUM_GAME_API_VERSION) return 1; // built against a different API
    g_host = host;
    Register<Spinner>("Spinner", kSpinnerProps, SpinnerConstruct, nullptr, SpinnerUpdate, nullptr);
    Register<Bobber>("Bobber", kBobberProps, BobberConstruct, BobberBegin, BobberUpdate, BobberEnd);
    Register<Mover>("Mover", kMoverProps, MoverConstruct, nullptr, MoverUpdate, nullptr);
    Log("Sample game module loaded");
    return 0;
}

MAGNUM_EXPORT void Magnum_OnUpdate(float /*dt*/) {
    // Game-wide logic goes here (called once per frame while playing, after every component's on_update).
}

MAGNUM_EXPORT void Magnum_OnUnload(void) {
    g_host = nullptr;
}

} // extern "C"
