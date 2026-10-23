#include "scene/Scene.h"

#include "core/Json.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace derecho::scene {

using core::JsonValue;

namespace {

JsonValue Vec3ToJson(const vec3& value) {
    JsonValue array = JsonValue::MakeArray();
    array.Push(JsonValue(static_cast<double>(value.x)));
    array.Push(JsonValue(static_cast<double>(value.y)));
    array.Push(JsonValue(static_cast<double>(value.z)));
    return array;
}

vec3 JsonToVec3(const JsonValue* value, const vec3& fallback) {
    if (!value || !value->IsArray() || value->Items().size() != 3) {
        return fallback;
    }
    const auto& items = value->Items();
    return vec3(static_cast<float>(items[0].NumberValue(fallback.x)),
                static_cast<float>(items[1].NumberValue(fallback.y)),
                static_cast<float>(items[2].NumberValue(fallback.z)));
}

} // namespace

float Entity::Param(const char* name, float fallback) const {
    for (const auto& entry : scriptParams) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    return fallback;
}

void Entity::SetParam(const std::string& name, float value) {
    for (auto& entry : scriptParams) {
        if (entry.first == name) {
            entry.second = value;
            return;
        }
    }
    scriptParams.emplace_back(name, value);
}

const char* PrimitiveName(Primitive primitive) {
    switch (primitive) {
        case Primitive::Box: return "box";
        case Primitive::Sphere: return "sphere";
        case Primitive::Cylinder: return "cylinder";
        case Primitive::Cone: return "cone";
        default: return "box";
    }
}

bool ParsePrimitive(const std::string& text, Primitive& out) {
    if (text == "box" || text == "cube") { out = Primitive::Box; return true; }
    if (text == "sphere" || text == "ball") { out = Primitive::Sphere; return true; }
    if (text == "cylinder" || text == "pillar") { out = Primitive::Cylinder; return true; }
    if (text == "cone") { out = Primitive::Cone; return true; }
    return false;
}

mat4 Entity::LocalMatrix() const {
    mat4 matrix = glm::translate(mat4(1.0f), position);
    matrix = glm::rotate(matrix, glm::radians(rotation.z), vec3(0.0f, 0.0f, 1.0f));
    matrix = glm::rotate(matrix, glm::radians(rotation.y), vec3(0.0f, 1.0f, 0.0f));
    matrix = glm::rotate(matrix, glm::radians(rotation.x), vec3(1.0f, 0.0f, 0.0f));
    return glm::scale(matrix, scale);
}

void Scene::Clear() {
    m_entities.clear();
    m_nextId = 1;
}

EntityId Scene::Create(const std::string& name, Primitive primitive, const vec3& position) {
    Entity entity;
    entity.id = m_nextId++;
    entity.alive = true;
    entity.name = name.empty() ? "Entity" : name;
    entity.primitive = primitive;
    entity.position = position;
    m_entities.push_back(entity);
    return entity.id;
}

bool Scene::Destroy(EntityId id) {
    Entity* entity = Find(id);
    if (!entity) {
        return false;
    }
    entity->alive = false;
    // Orphan children instead of leaving dangling parents behind.
    for (Entity& other : m_entities) {
        if (other.alive && other.parent == id) {
            other.parent = kInvalidEntity;
        }
    }
    return true;
}

Entity* Scene::Find(EntityId id) {
    for (Entity& entity : m_entities) {
        if (entity.id == id) {
            return entity.alive ? &entity : nullptr;
        }
    }
    return nullptr;
}

const Entity* Scene::Find(EntityId id) const {
    for (const Entity& entity : m_entities) {
        if (entity.id == id) {
            return entity.alive ? &entity : nullptr;
        }
    }
    return nullptr;
}

size_t Scene::AliveCount() const {
    return static_cast<size_t>(std::count_if(m_entities.begin(), m_entities.end(),
                                             [](const Entity& e) { return e.alive; }));
}

mat4 Scene::WorldMatrix(const Entity& entity) const {
    // Walk up at most 32 levels: guards against a cycle introduced by hand-edited
    // scene files, which would otherwise hang the frame.
    mat4 result = entity.LocalMatrix();
    EntityId parent = entity.parent;
    int depth = 0;
    while (parent != kInvalidEntity && depth < 32) {
        const Entity* parentEntity = Find(parent);
        if (!parentEntity) {
            break;
        }
        result = parentEntity->LocalMatrix() * result;
        parent = parentEntity->parent;
        ++depth;
    }
    return result;
}

std::string Scene::UniqueName(const std::string& base) const {
    int suffix = 1;
    std::string candidate = base + " " + std::to_string(suffix);
    bool collision = true;
    while (collision && suffix < 10000) {
        collision = false;
        for (const Entity& entity : m_entities) {
            if (entity.alive && entity.name == candidate) {
                collision = true;
                break;
            }
        }
        if (collision) {
            ++suffix;
            candidate = base + " " + std::to_string(suffix);
        }
    }
    return candidate;
}

std::string Scene::Serialize() const {
    JsonValue root = JsonValue::MakeObject();
    root["format"] = JsonValue("derecho.scene");
    root["version"] = JsonValue(1);
    root["nextEntityId"] = JsonValue(static_cast<int>(m_nextId));

    JsonValue entities = JsonValue::MakeArray();
    for (const Entity& entity : m_entities) {
        if (!entity.alive) {
            continue;
        }
        JsonValue item = JsonValue::MakeObject();
        item["id"] = JsonValue(static_cast<int>(entity.id));
        item["name"] = JsonValue(entity.name);
        item["primitive"] = JsonValue(PrimitiveName(entity.primitive));
        item["position"] = Vec3ToJson(entity.position);
        item["rotation"] = Vec3ToJson(entity.rotation);
        item["scale"] = Vec3ToJson(entity.scale);
        item["color"] = Vec3ToJson(entity.color);
        item["castShadow"] = JsonValue(entity.castShadow);
        if (!entity.visible) {
            item["visible"] = JsonValue(false);
        }
        if (!entity.mesh.empty()) {
            item["mesh"] = JsonValue(entity.mesh);
        }
        if (!entity.texture.empty()) {
            item["texture"] = JsonValue(entity.texture);
        }
        if (!entity.script.empty()) {
            item["script"] = JsonValue(entity.script);
            if (!entity.scriptParams.empty()) {
                JsonValue params = JsonValue::MakeObject();
                for (const auto& entry : entity.scriptParams) {
                    params[entry.first] = JsonValue(static_cast<double>(entry.second));
                }
                item["scriptParams"] = std::move(params);
            }
        }
        if (entity.parent != kInvalidEntity) {
            item["parent"] = JsonValue(static_cast<int>(entity.parent));
        }
        entities.Push(std::move(item));
    }
    root["entities"] = std::move(entities);
    return root.Serialize(2);
}

bool Scene::Save(const std::string& path, std::string& error) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open '" + path + "' for writing";
        return false;
    }
    const std::string text = Serialize();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        error = "failed while writing '" + path + "'";
        return false;
    }
    DE_INFO("Scene saved: %s (%zu entities).", path.c_str(), AliveCount());
    return true;
}

bool Scene::Deserialize(const std::string& text, std::string& error) {
    JsonValue root;
    if (!root.Parse(text, error)) {
        return false;
    }
    const std::string format = root.GetString("format", "");
    // Scenes written before the engine was renamed used "frontier.scene"; keep
    // reading them so existing work is not stranded by a branding change.
    if (format != "derecho.scene" && format != "frontier.scene") {
        error = "not a Derecho scene file (format = '" + format + "')";
        return false;
    }

    const JsonValue* entities = root.Find("entities");
    if (!entities || !entities->IsArray()) {
        error = "scene has no entities array";
        return false;
    }

    Clear();
    EntityId highest = 0;
    for (const JsonValue& item : entities->Items()) {
        if (!item.IsObject()) {
            continue;
        }
        Entity entity;
        entity.id = static_cast<EntityId>(item.GetInt("id", 0));
        if (entity.id == kInvalidEntity) {
            entity.id = m_nextId++;
        }
        entity.alive = true;
        entity.name = item.GetString("name", "Entity");
        ParsePrimitive(item.GetString("primitive", "box"), entity.primitive);
        entity.position = JsonToVec3(item.Find("position"), vec3(0.0f));
        entity.rotation = JsonToVec3(item.Find("rotation"), vec3(0.0f));
        entity.scale = JsonToVec3(item.Find("scale"), vec3(1.0f));
        entity.color = JsonToVec3(item.Find("color"), vec3(0.75f));
        entity.castShadow = item.GetBool("castShadow", true);
        entity.visible = item.GetBool("visible", true);
        entity.mesh = item.GetString("mesh", "");
        entity.texture = item.GetString("texture", "");
        entity.script = item.GetString("script", "");
        if (const JsonValue* params = item.Find("scriptParams")) {
            for (const auto& entry : params->Members()) {
                entity.scriptParams.emplace_back(entry.first,
                                                 static_cast<float>(entry.second.NumberValue(0.0)));
            }
        }
        entity.parent = static_cast<EntityId>(item.GetInt("parent", 0));
        highest = std::max(highest, entity.id);
        m_entities.push_back(std::move(entity));
    }

    // Validate parent links against the entities that actually exist.
    for (Entity& entity : m_entities) {
        if (entity.parent != kInvalidEntity && !Find(entity.parent)) {
            entity.parent = kInvalidEntity;
        }
    }

    m_nextId = std::max(m_nextId, static_cast<EntityId>(highest + 1));
    m_nextId = std::max(m_nextId, static_cast<EntityId>(root.GetInt("nextEntityId", 1)));
    return true;
}

bool Scene::Load(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "could not open '" + path + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!Deserialize(text, error)) {
        error = "'" + path + "': " + error;
        return false;
    }
    SetPath(path);
    DE_INFO("Scene loaded: %s (%zu entities).", path.c_str(), AliveCount());
    return true;
}

EntityId Scene::Duplicate(EntityId id) {
    const Entity* source = Find(id);
    if (!source) {
        return kInvalidEntity;
    }
    // Copy without the identity, then let Create assign the id and defer the
    // parent fix-up until the copy exists.
    const EntityId copyId = Create(UniqueName(source->name), source->primitive, source->position);
    Entity* copy = Find(copyId);
    if (!copy) {
        return kInvalidEntity;
    }
    const std::string name = copy->name;
    *copy = *source;
    copy->id = copyId;
    copy->alive = true;
    copy->name = name;
    copy->parent = source->parent;

    // Children follow their parent into the copy.
    const std::vector<Entity> snapshot = m_entities;
    for (const Entity& child : snapshot) {
        if (!child.alive || child.parent != id) {
            continue;
        }
        const EntityId childCopy = Create(UniqueName(child.name), child.primitive, child.position);
        if (Entity* target = Find(childCopy)) {
            const std::string childName = target->name;
            *target = child;
            target->id = childCopy;
            target->alive = true;
            target->name = childName;
            target->parent = copyId;
        }
    }
    return copyId;
}

} // namespace derecho::scene
