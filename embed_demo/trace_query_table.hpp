#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace perfetto::trace_processor {
class TraceProcessor;
}

namespace cnperf {
namespace embed_demo {

// --- Primary API for cnperf-gui (in-process): no JSON parse step ---
// After linking Trace Processor + this TU, call |tp_query_to_table| and map
// |SqlCell| / column names into CNPerfTimelineModel (or equivalent) with a switch
// on |SqlCell::kind|.

// One cell copied out of Trace Processor's SqlValue (safe to hold after Next()).
struct SqlCell {
  enum class Kind { kNull, kInt64, kDouble, kString, kBytes };
  Kind kind = Kind::kNull;
  int64_t int64_value = 0;
  double double_value = 0;
  std::string text;
  std::vector<uint8_t> bytes;
};

// Typed query result for direct mapping into a UI model (no JSON parse step).
// If |ok| is false, |error_text| is set and |columns|/|rows| are empty.
struct QueryTable {
  bool ok = true;
  std::string error_text;
  std::vector<std::string> columns;
  std::vector<std::vector<SqlCell>> rows;
};

// Preferred "query API" for GUI / in-process integration: fills |QueryTable| from
// Trace Processor iterator (copies strings/bytes immediately per row).
QueryTable tp_query_to_table(perfetto::trace_processor::TraceProcessor* processor,
                             const std::string& sql);

}  // namespace embed_demo
}  // namespace cnperf
