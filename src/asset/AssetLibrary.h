// Derecho Engine - the asset database.
//
// Scans a directory of models (*.obj) and textures (*.png, *.tga), loads them
// once, and hands them out by name. The renderer uploads them on demand; the
// editor lists them so any entity can point at a real asset instead of a
// built-in primitive.
#pragma once

#include "asset/Image.h"
#include "asset/Mesh.h"

#include <string>
#include <vector>

namespace derecho::asset {

struct MeshAsset {
    std::string name; // stem of the file, e.g. "character"
    std::string path; // path relative to the asset root
    MeshData mesh;
};

struct TextureAsset {
    std::string name;
    std::string path;
    Image image;
};

class AssetLibrary {
public:
    // Loads <root>/models and <root>/textures. Missing directories are fine;
    // files that fail to load are counted in Failures() and reported in `error`.
    bool Scan(const std::string& root, std::string& error);

    const std::vector<MeshAsset>& Meshes() const { return m_meshes; }
    const std::vector<TextureAsset>& Textures() const { return m_textures; }

    const MeshAsset* FindMesh(const std::string& name) const;
    const TextureAsset* FindTexture(const std::string& name) const;

    const std::string& Root() const { return m_root; }
    int Failures() const { return m_failures; }
    void Clear();

private:
    std::string m_root;
    std::vector<MeshAsset> m_meshes;
    std::vector<TextureAsset> m_textures;
    int m_failures = 0;
};

} // namespace derecho::asset
