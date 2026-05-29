#pragma once

#include "Result.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace orca {

using SliceHandle = std::uint64_t;

enum class SliceTechnology {
    FFF,
    SLA,
};

enum class SliceState {
    NotStarted,
    Queued,
    Running,
    Completed,
    Cancelled,
    Failed,
};

struct SliceParams {
    int             plate_id   = 0;
    SliceTechnology technology = SliceTechnology::FFF;
};

struct SliceStatus {
    SliceState  state    = SliceState::NotStarted;
    float       progress = 0.0f;
    std::string message;
    std::string error;
};

class Slicer {
public:
    ~Slicer();

    Slicer(const Slicer&)            = delete;
    Slicer& operator=(const Slicer&) = delete;
    Slicer(Slicer&&)                 = delete;
    Slicer& operator=(Slicer&&)      = delete;

    Result<SliceHandle> request_slice(SliceParams params);
    void                cancel(SliceHandle handle);
    SliceStatus         status(SliceHandle handle) const;

    bool                is_busy() const;
    void                cancel_all();

private:
    friend class Session;
    Slicer();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orca
