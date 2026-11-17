#pragma once

#include "noor/runtime/input.h"

#include <memory>
#include <optional>
#include <string>

struct noor_doc;
struct noor_viewport;
namespace noor::editor {
class SnapshotChannel;
}

namespace noor::editor_api {

// Records the calling thread's last error for noor_last_error().
void set_last_error(std::string message);

} // namespace noor::editor_api

// The render-snapshot channel a document publishes to (document_api.cpp), for viewports.
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_channel(noor_doc* doc);
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_preview_channel(noor_doc* doc);

// Game input from a viewport (viewport.cpp), for play mode. Empty unless the viewport has the
// keyboard focus, and while the right button flies the editor camera. One caller at a time: the
// viewport's render thread while it ticks a document (noor_viewport_set_play), else noor_doc_tick.
noor::runtime::InputState noor_viewport_game_input(noor_viewport* viewport);

// One play-mode frame of `doc` (document_api.cpp); returns a script's mouse capture request, if any.
// Exceptions are caught and logged. Any thread (the document locks its world).
std::optional<bool> noor_doc_tick_internal(noor_doc* doc, double seconds, const noor::runtime::InputState& input);
