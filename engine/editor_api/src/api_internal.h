#pragma once

#include <string>

namespace noor::editor_api {

// Records the calling thread's last error for noor_last_error().
void set_last_error(std::string message);

} // namespace noor::editor_api
