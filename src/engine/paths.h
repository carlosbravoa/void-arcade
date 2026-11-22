// paths.h — where persistent files (scores.json, settings.json) live, and a
// crash-safe way to write them.
#pragma once

#include <string>

// Directory for save data, with a trailing slash. Resolution order:
//   1. $RETRO_ARCADE_DATA            (the snap points this at $SNAP_USER_COMMON)
//   2. the executable's directory    (dev builds; kept if files already live there)
//   3. $XDG_DATA_HOME/void-arcade or ~/.local/share/void-arcade
//      (system installs, where the exe dir is read-only)
std::string data_dir();

// Write `text` to `path` atomically (temp file + rename) so a crash or power
// loss mid-save can never leave a truncated file. Returns false on failure.
bool write_file_atomic(const std::string& path, const std::string& text);
