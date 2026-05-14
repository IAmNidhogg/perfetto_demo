#pragma once

#include <string>
#include <string_view>

namespace perfetto::trace_processor {
class TraceProcessor;
}

namespace cnperf {
namespace embed_demo {

// JSON encoding of query results — for CLI / HTTP demos / debugging. For filling a
// UI model without parsing JSON, use |tp_query_to_table| in trace_query_table.hpp.
std::string tp_query_to_json(perfetto::trace_processor::TraceProcessor* processor,
                             const std::string& sql);

// {"error":"<escaped message>"} for transport-level errors (HTTP demo, etc.).
std::string tp_error_json_response(std::string_view message);

}  // namespace embed_demo
}  // namespace cnperf
