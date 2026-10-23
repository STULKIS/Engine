// Derecho Engine - generated placeholder art.
//
// The repository cannot ship third-party game art, so the engine writes a small
// set of original assets on first run: a low-poly character, a crystal, a rock,
// and hand-painted-looking textures (PNG and TGA, to exercise both decoders).
// They are ordinary files in assets/models and assets/textures afterwards, so
// they can be edited, deleted, or replaced with real art.
#pragma once

#include <string>

namespace derecho::asset {

// Writes any missing placeholder files under `root`. Returns false only when a
// file exists but cannot be written; existing files are never overwritten.
bool EnsurePlaceholderAssets(const std::string& root, std::string& error);

} // namespace derecho::asset
