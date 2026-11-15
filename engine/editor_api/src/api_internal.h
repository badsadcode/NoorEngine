#pragma once

#include <memory>
#include <string>

struct noor_doc;
namespace noor::editor {
class SnapshotChannel;
}

namespace noor::editor_api {

// Records the calling thread's last error for noor_last_error().
void set_last_error(std::string message);

} // namespace noor::editor_api

// The render-snapshot channel a document publishes to (document_api.cpp), for viewports.
std::shared_ptr<noor::editor::SnapshotChannel> noor_doc_channel(noor_doc* doc);
