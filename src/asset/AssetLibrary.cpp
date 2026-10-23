// Frontier Engine - asset scanning and lookup.
#include "asset/AssetLibrary.h"

#include "core/Log.h"

#include <algorithm>
#include <filesystem>

namespace frontier::asset {

namespace fs = std::filesystem;

namespace {

std::string LowerExtension(const fs::path& path) {
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension;
}

// Collects files from `directory` (recursively) whose extension is in `wanted`,
// sorted so the editor and the scene files see a stable order.
std::vector<fs::path> CollectFiles(const fs::path& directory, const std::vector<std::string>& wanted) {
    std::vector<fs::path> found;
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return found;
    }
    for (fs::recursive_directory_iterator it(directory, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string extension = LowerExtension(it->path());
        if (std::find(wanted.begin(), wanted.end(), extension) != wanted.end()) {
            found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

} // namespace

void AssetLibrary::Clear() {
    m_meshes.clear();
    m_textures.clear();
    m_failures = 0;
}

bool AssetLibrary::Scan(const std::string& root, std::string& error) {
    Clear();
    m_root = root;
    error.clear();

    std::vector<std::string> problems;

    for (const fs::path& path : CollectFiles(fs::path(root) / "models", {".obj"})) {
        MeshAsset asset;
        asset.name = path.stem().string();
        asset.path = path.lexically_relative(fs::path(root)).generic_string();
        std::string message;
        if (!LoadObj(path.string(), asset.mesh, message)) {
            ++m_failures;
            problems.push_back(message);
            continue;
        }
        m_meshes.push_back(std::move(asset));
    }

    for (const fs::path& path : CollectFiles(fs::path(root) / "textures",
                                             {".png", ".tga"})) {
        TextureAsset asset;
        asset.name = path.stem().string();
        asset.path = path.lexically_relative(fs::path(root)).generic_string();
        std::string message;
        if (!LoadImage(path.string(), asset.image, message)) {
            ++m_failures;
            problems.push_back(message);
            continue;
        }
        m_textures.push_back(std::move(asset));
    }

    FR_INFO("Assets: %zu models, %zu textures from '%s'%s.", m_meshes.size(), m_textures.size(),
            root.c_str(), m_failures > 0 ? " (with failures)" : "");
    for (const MeshAsset& mesh : m_meshes) {
        FR_INFO("  model   %-18s %zu verts  %zu tris", mesh.name.c_str(), mesh.mesh.vertices.size(),
                mesh.mesh.indices.size() / 3);
    }
    for (const TextureAsset& texture : m_textures) {
        FR_INFO("  texture %-18s %dx%d", texture.name.c_str(), texture.image.width,
                texture.image.height);
    }

    if (!problems.empty()) {
        for (const std::string& problem : problems) {
            FR_WARN("Asset problem: %s", problem.c_str());
        }
        error = problems.front();
        return false;
    }
    return true;
}

const MeshAsset* AssetLibrary::FindMesh(const std::string& name) const {
    for (const MeshAsset& asset : m_meshes) {
        if (asset.name == name) {
            return &asset;
        }
    }
    return nullptr;
}

const TextureAsset* AssetLibrary::FindTexture(const std::string& name) const {
    for (const TextureAsset& asset : m_textures) {
        if (asset.name == name) {
            return &asset;
        }
    }
    return nullptr;
}

} // namespace frontier::asset
