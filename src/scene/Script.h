// Derecho Engine - components that run code on scene entities.
//
// The shape is deliberately Unity-like: a script is attached to an entity by
// name, receives OnStart once, then OnUpdate every frame, and can be tuned with
// named float parameters that are saved into the scene file. Native C++ scripts
// register themselves in ScriptTypes(), so adding a behaviour is a single file.
#pragma once

#include "scene/Scene.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace derecho::scene {

class Script {
public:
    virtual ~Script() = default;

    // Runs once when the entity first gets this script, and again after a reload.
    virtual void OnStart(Scene& scene, Entity& entity) {
        (void)scene;
        (void)entity;
    }

    // Runs every frame. `deltaSeconds` is clamped by the host.
    virtual void OnUpdate(Scene& scene, Entity& entity, float deltaSeconds) = 0;

    // Runs when the script is detached or the entity is destroyed.
    virtual void OnStop(Scene& scene, Entity& entity) {
        (void)scene;
        (void)entity;
    }
};

// A tunable script parameter, shown as a slider in the inspector.
struct ScriptParam {
    const char* name = "";
    const char* label = "";
    float defaultValue = 0.0f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    const char* format = "%.2f";
};

struct ScriptType {
    const char* name = "";
    const char* description = "";
    std::vector<ScriptParam> params;
    std::function<std::unique_ptr<Script>()> create;
};

// Registered script types in a stable order (the inspector shows this order).
const std::vector<ScriptType>& ScriptTypes();
const ScriptType* FindScriptType(const std::string& name);

// Parameter helpers used by both the scripts and the inspector.
float ScriptParamValue(const Entity& entity, const ScriptParam& param);
void SetScriptParam(Entity& entity, const std::string& name, float value);

// Owns the live script instances and drives their lifecycle. One host per scene.
class ScriptHost {
public:
    // Instantiates new scripts, drops removed ones, then updates everything.
    void Update(Scene& scene, float deltaSeconds);

    // Destroys all instances (call after the scene is replaced or reloaded).
    void Clear(Scene& scene);

    size_t ActiveCount() const { return m_instances.size(); }

private:
    struct Instance {
        EntityId entity = kInvalidEntity;
        std::string type;
        std::unique_ptr<Script> script;
    };

    std::vector<Instance> m_instances;
    bool m_reportedMissingType = false;
};

} // namespace derecho::scene
