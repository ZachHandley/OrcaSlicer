#pragma once

#include "Export.hpp"
#include "Presets.hpp"
#include "Project.hpp"
#include "Slicer.hpp"

#include <filesystem>
#include <string>

namespace orca {

struct PresetChanged {
    PresetType  type;
    std::string name;
};

struct SlicingProgress {
    SliceHandle handle;
    float       progress;
    std::string message;
};

struct SlicingFinished {
    SliceHandle handle;
    bool        success;
    std::string error;
};

struct ExportBegan {
    ExportHandle          handle;
    std::filesystem::path path;
};

struct ExportFinished {
    ExportHandle handle;
    bool         success;
    std::size_t  line_count;
    std::string  error;
};

struct ProjectLoaded {
    LoadHandle            handle;
    std::filesystem::path path;
};

struct ObjectAdded {
    ObjectId id;
};

struct ObjectRemoved {
    ObjectId id;
};

} // namespace orca
