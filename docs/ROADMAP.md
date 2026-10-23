# Frontier Engine — roadmap to a general purpose engine

This document is the plan for turning the current vertical slice (procedural
world + renderer + FPS controller + debug panel) into an engine you can build
*any* game in, in the spirit of Unity/Unreal but small enough to read.

Guiding rules:

1. **Every milestone ships runnable.** No milestone leaves the engine in a state
   where it cannot be launched and played.
2. **One scene model.** Editor, save files, scripting and physics all operate on
   the same entity/component data. No feature gets its own private world state.
3. **Original code and assets only.** No CryEngine/Unreal/Unity code, shaders,
   brand assets or UI. Third-party libraries stay permissively licensed and
   vendored (SDL2, GLM, Dear ImGui).
4. **The engine must stay fast on integrated graphics** (the reference target is
   a Ryzen 5 5600G / Vega 7 iGPU at 1080p).

## Milestones

| # | Milestone | Ships | Status |
|---|---|---|---|
| 0 | Render styles (NPR/anime) + quality presets | cel shading, ink outlines, painted sky, `--quality` presets, `F7` toggle | **done** |
| 1 | Scene foundation | `Scene`, `Entity`, component storage (Transform, Mesh, Material, Light, Camera, Script), hierarchy, prefabs, JSON scene save/load | next |
| 2 | Editor | docking layout, outliner, inspector, viewport picking, translate/rotate/scale gizmos, undo/redo, play-in-editor | |
| 3 | Assets | OBJ + glTF import, PNG/TGA/JPG textures (vendored stb), material assets, drag-and-drop, hot reload | |
| 4 | Scripting | C#-style language (below), component scripts, hot reload, editor script editor | |
| 5 | Simulation | colliders (capsule/box/sphere), swept collision, rigid bodies, raycasts, triggers, character controller, audio | |
| 6 | Rendering II | PBR + IBL from the procedural sky, cascaded shadows, SSAO, LOD/impostor tuning, GPU timers | |
| 7 | Project & packaging | project format, asset registry, scene list, settings, export a runnable folder | |

Rendering-first (milestone 0) was chosen because the visual style defines the
engine's identity, and because the same depth/colour targets are needed by the
editor viewport later.

## Milestone 1 — scene foundation (why it is first)

Today the world is assembled in C++ (`world::World` builds terrain, water and
vegetation procedurally). That is fine for a demo but it means every new object
requires a recompile. The scene layer introduces:

```cpp
struct Transform { vec3 position, rotation, scale; EntityId parent; };
struct MeshRenderer { MeshHandle mesh; MaterialHandle material; };
struct Light { LightType type; vec3 color; float intensity, range; };
struct CameraComponent { float fov, nearPlane, farPlane; };

class Scene {
    EntityId Create(const std::string& name, EntityId parent = kInvalidEntity);
    void Destroy(EntityId id);
    template <class T> T* Get(EntityId id);
    template <class T> void Add(EntityId id, T component);
    // Serialised as <scene>.fscene (JSON): entities, components, asset refs.
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);
};
```

The procedural world becomes a **generator** that writes into a `Scene` instead
of owning its data, so generated and hand-placed content coexist and both save.

## Milestone 4 — the scripting language

"Like C#" is taken to mean **C#-shaped syntax and semantics for gameplay code**,
not a .NET runtime. Embedding the CLR would add a multi-hundred-megabyte runtime
dependency, a second toolchain, and would break the one-command build. Instead
the engine gets its own small, statically typed, bytecode-compiled language with
C# syntax, implemented in this repository.

```csharp
// assets/scripts/PlayerController.fs
using Frontier;

class PlayerController : Script
{
    public float speed = 6.5f;
    public float sprintMultiplier = 1.9f;
    public Entity camera;

    private float yaw;
    private bool grounded;

    void Start()
    {
        Debug.Log("Player ready at " + Transform.position);
    }

    void Update(float dt)
    {
        float x = Input.GetAxis("Horizontal");
        float z = Input.GetAxis("Vertical");
        float speedNow = Input.GetKey("Shift") ? speed * sprintMultiplier : speed;

        Vector3 move = Transform.forward * z + Transform.right * x;
        Transform.position = Transform.position + move * speedNow * dt;

        if (Input.GetKeyDown("Space") && grounded)
        {
            Rigidbody.AddForce(Vector3.up * 6.0f);
        }
    }

    void OnTriggerEnter(Entity other)
    {
        if (other.name == "Pickup") { Scene.Destroy(other); Score.Add(1); }
    }
}
```

Language subset (deliberately small, but enough for real gameplay):

- Types: `int`, `float`, `bool`, `string`, `Vector3`, `Entity`, arrays, `class`.
- Expressions with C# precedence, `new`, field/method access, string concatenation.
- Statements: `if/else`, `while`, `for`, `foreach`, `return`, `break`, `continue`.
- Functions with typed parameters/returns; `class` with fields and methods.
- No exceptions, no generics, no delegates yet, no operator overloading.

Implementation plan (self-contained, ~4 files):

1. `script/Lexer` — tokens, no regex, precise error positions.
2. `script/Parser` — recursive descent → AST with a small arena.
3. `script/Compiler` — AST → stack bytecode, constants pool, symbol table.
4. `script/Vm` — typed register-less stack machine, calls host functions by ID.
5. `script/Bindings` — explicit registration table (`Input`, `Time`, `Debug`,
   `Transform`, `Rigidbody`, `Scene`, `Audio`), so the host API is documented in
   one file and cannot drift from the runtime.
6. `script/ScriptComponent` — owns a VM instance per entity, dispatches
   `Start/Update/FixedUpdate/OnTriggerEnter`; scripts reload when the file
   changes (watch mtime) without restarting the engine.

Determinism and safety: no raw pointers in script land, every host call validates
its entity handle, and a per-frame instruction budget kills runaway `while(true)`
loops with a logged error instead of freezing the editor.

## Milestone 5 — simulation

- Collision against the existing heightfield (already used by the player) plus
  convex primitives.
- Character controller shared by the player and scripts.
- Fixed 120 Hz physics tick decoupled from rendering, with interpolation.
- Raycasts for interaction/weapons, trigger volumes for gameplay events.
- Audio: SDL audio device, mixer with 3D attenuation, procedural + WAV sources.

## Milestone 6 — rendering II

- PBR material model (albedo/roughness/metallic/normal) alongside the current
  stylised path, so both looks share one material description.
- Image-based lighting from the procedural sky (prefiltered cube map).
- Cascaded shadow maps with per-cascade filtering, then SSAO and a GPU timer
  breakdown in the debug panel.
- Impostor/billboard LOD for distant trees and density scaling for grass, aimed
  at 60 FPS on Vega-class integrated graphics.

## Performance notes for integrated GPUs

The reference machine (Ryzen 5 5600G, Vega 7) is fill-rate limited at 1600×900
with 4× MSAA + HDR + bloom. Useful starting points:

| Setting | iGPU-friendly |
| --- | --- |
| `--quality=low` | 1× MSAA, 0.85 render scale, 1024 shadow map, 520 m view |
| `--msaa=2` | MSAA is the single most expensive toggle at this resolution |
| `--render-scale=0.8` | renders 3D at 80 %, UI stays native |
| `--grass=25000` | grass is the dominant vertex/overdraw cost |
| `--view-distance=600` | shrinks terrain, shadow and foliage work |
| `--style=anime --posterize=0` | the NPR pass is one full-screen shader; keep bloom off on weak GPUs |
