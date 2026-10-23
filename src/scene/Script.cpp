// Frontier Engine - built-in scripts and the runtime host that drives them.
#include "scene/Script.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace frontier::scene {

namespace {

// ---------------------------------------------------------------- behaviours

// Rotates the entity around one axis. Axis is a float so every parameter can
// travel through the same slider/serialisation path.
class SpinScript final : public Script {
public:
    void OnUpdate(Scene& scene, Entity& entity, float dt) override {
        (void)scene;
        const float speed = entity.Param("speed", 45.0f);
        const int axis = static_cast<int>(entity.Param("axis", 1.0f)) % 3;
        entity.rotation[static_cast<glm::length_t>(axis)] += speed * dt;
        if (entity.rotation[static_cast<glm::length_t>(axis)] > 360.0f) {
            entity.rotation[static_cast<glm::length_t>(axis)] -= 360.0f;
        }
    }
};

// Gentle vertical float around the position the entity had when it started.
class BobScript final : public Script {
public:
    void OnStart(Scene& scene, Entity& entity) override {
        (void)scene;
        m_base = entity.position.y;
        m_elapsed = 0.0f;
    }

    void OnUpdate(Scene& scene, Entity& entity, float dt) override {
        (void)scene;
        const float amplitude = entity.Param("amplitude", 0.5f);
        const float frequency = std::max(entity.Param("frequency", 0.5f), 0.01f);
        const float phase = entity.Param("phase", 0.0f);
        m_elapsed += dt;
        entity.position.y = m_base + std::sin(m_elapsed * frequency * 6.2831853f + phase) * amplitude;
    }

    void OnStop(Scene& scene, Entity& entity) override {
        (void)scene;
        entity.position.y = m_base;
    }

private:
    float m_base = 0.0f;
    float m_elapsed = 0.0f;
};

// Circles the entity around the world origin (handy for markers and orbs).
class OrbitScript final : public Script {
public:
    void OnStart(Scene& scene, Entity& entity) override {
        (void)scene;
        m_radius = std::sqrt(entity.position.x * entity.position.x + entity.position.z * entity.position.z);
        m_angle = std::atan2(entity.position.z, entity.position.x);
        m_height = entity.position.y;
    }

    void OnUpdate(Scene& scene, Entity& entity, float dt) override {
        (void)scene;
        const float speed = entity.Param("speed", 20.0f);
        const float radius = entity.Param("radius", m_radius);
        const float height = entity.Param("height", m_height);
        m_angle += speed * dt * 0.0174533f;
        entity.position.x = std::cos(m_angle) * radius;
        entity.position.z = std::sin(m_angle) * radius;
        entity.position.y = height;
        entity.rotation.y += speed * dt;
    }

private:
    float m_radius = 4.0f;
    float m_angle = 0.0f;
    float m_height = 2.0f;
};

// Scales with a sine pulse, e.g. to make a pickup glow-shimmer.
class PulseScript final : public Script {
public:
    void OnStart(Scene& scene, Entity& entity) override {
        (void)scene;
        m_baseScale = entity.scale;
        m_elapsed = 0.0f;
    }

    void OnUpdate(Scene& scene, Entity& entity, float dt) override {
        (void)scene;
        const float amount = entity.Param("amount", 0.15f);
        const float frequency = std::max(entity.Param("frequency", 1.0f), 0.01f);
        m_elapsed += dt;
        const float factor = 1.0f + std::sin(m_elapsed * frequency * 6.2831853f) * amount;
        entity.scale = m_baseScale * factor;
    }

    void OnStop(Scene& scene, Entity& entity) override {
        (void)scene;
        entity.scale = m_baseScale;
    }

private:
    vec3 m_baseScale{1.0f, 1.0f, 1.0f};
    float m_elapsed = 0.0f;
};

// ------------------------------------------------------------- registration

std::vector<ScriptType> BuildScriptTypes() {
    std::vector<ScriptType> types;

    types.push_back({"Spin", "Rotates the entity around one axis.",
                     {{"speed", "Degrees / second", 45.0f, -720.0f, 720.0f, "%.1f deg/s"},
                      {"axis", "Axis (0=X, 1=Y, 2=Z)", 1.0f, 0.0f, 2.0f, "%.0f"}},
                     [] { return std::make_unique<SpinScript>(); }});

    types.push_back({"Bob", "Floats the entity up and down around its start height.",
                     {{"amplitude", "Amplitude (m)", 0.5f, 0.0f, 5.0f, "%.2f m"},
                      {"frequency", "Frequency (Hz)", 0.5f, 0.01f, 4.0f, "%.2f Hz"},
                      {"phase", "Phase (rad)", 0.0f, -3.15f, 3.15f, "%.2f"}},
                     [] { return std::make_unique<BobScript>(); }});

    types.push_back({"Orbit", "Circles the world origin at a fixed radius and height.",
                     {{"speed", "Degrees / second", 20.0f, -360.0f, 360.0f, "%.1f deg/s"},
                      {"radius", "Radius (m)", 4.0f, 0.0f, 60.0f, "%.2f m"},
                      {"height", "Height (m)", 2.0f, 0.0f, 60.0f, "%.2f m"}},
                     [] { return std::make_unique<OrbitScript>(); }});

    types.push_back({"Pulse", "Scales the entity with a sine wave.",
                     {{"amount", "Amount", 0.15f, 0.0f, 1.0f, "%.2f"},
                      {"frequency", "Frequency (Hz)", 1.0f, 0.01f, 5.0f, "%.2f Hz"}},
                     [] { return std::make_unique<PulseScript>(); }});

    return types;
}

} // namespace

const std::vector<ScriptType>& ScriptTypes() {
    static const std::vector<ScriptType> types = BuildScriptTypes();
    return types;
}

const ScriptType* FindScriptType(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    for (const ScriptType& type : ScriptTypes()) {
        if (name == type.name) {
            return &type;
        }
    }
    return nullptr;
}

float ScriptParamValue(const Entity& entity, const ScriptParam& param) {
    return entity.Param(param.name, param.defaultValue);
}

void SetScriptParam(Entity& entity, const std::string& name, float value) {
    entity.SetParam(name, value);
}

void ScriptHost::Update(Scene& scene, float deltaSeconds) {
    // Guard against a long stall turning into a physics-defying leap.
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.1f);

    for (Instance& instance : m_instances) {
        Entity* entity = scene.Find(instance.entity);
        if (!entity || !entity->alive || entity->script != instance.type) {
            if (entity) {
                instance.script->OnStop(scene, *entity);
            }
            instance.script.reset();
        }
    }
    m_instances.erase(std::remove_if(m_instances.begin(), m_instances.end(),
                                     [](const Instance& instance) { return !instance.script; }),
                      m_instances.end());

    // Start scripts that are newly attached (or whose scene was just loaded).
    for (Entity& entity : scene.Entities()) {
        if (!entity.alive || entity.script.empty()) {
            continue;
        }
        const bool running = std::any_of(m_instances.begin(), m_instances.end(),
                                         [&entity](const Instance& instance) {
                                             return instance.entity == entity.id;
                                         });
        if (running) {
            continue;
        }
        const ScriptType* type = FindScriptType(entity.script);
        if (!type) {
            if (!m_reportedMissingType) {
                FR_WARN("Unknown script '%s' on entity '%s'; ignoring it.",
                        entity.script.c_str(), entity.name.c_str());
                m_reportedMissingType = true;
            }
            continue;
        }
        Instance instance;
        instance.entity = entity.id;
        instance.type = entity.script;
        instance.script = type->create();
        if (!instance.script) {
            continue;
        }
        instance.script->OnStart(scene, entity);
        m_instances.push_back(std::move(instance));
    }

    for (Instance& instance : m_instances) {
        Entity* entity = scene.Find(instance.entity);
        if (entity && entity->alive) {
            instance.script->OnUpdate(scene, *entity, dt);
        }
    }
}

void ScriptHost::Clear(Scene& scene) {
    for (Instance& instance : m_instances) {
        if (Entity* entity = scene.Find(instance.entity)) {
            instance.script->OnStop(scene, *entity);
        }
    }
    m_instances.clear();
    m_reportedMissingType = false;
}

} // namespace frontier::scene
