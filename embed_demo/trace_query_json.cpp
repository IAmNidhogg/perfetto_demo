#include "trace_query_json.hpp"
#include "trace_query_table.hpp"

#include "perfetto/trace_processor/trace_processor.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace tp = perfetto::trace_processor;

namespace cnperf {
namespace embed_demo {
namespace {

std::string escape_json_string(std::string_view in) {
  std::string out;
  out.reserve(in.size() + 2);
  out.push_back('"');
  for (char ch : in) {
    const unsigned char uc = static_cast<unsigned char>(ch);
    const char c = static_cast<char>(uc);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (uc < 0x20U) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(uc));
          out += buf;
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string sql_cell_to_json(const SqlCell& cell) {
  switch (cell.kind) {
    case SqlCell::Kind::kNull:
      return "null";
    case SqlCell::Kind::kInt64: {
      char buf[32];
      const int n = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(cell.int64_value));
      if (n <= 0) {
        return "0";
      }
      return std::string(buf, static_cast<size_t>(n));
    }
    case SqlCell::Kind::kDouble: {
      char buf[64];
      const int n = std::snprintf(buf, sizeof(buf), "%.17g", cell.double_value);
      if (n <= 0) {
        return "0";
      }
      return std::string(buf, static_cast<size_t>(n));
    }
    case SqlCell::Kind::kString:
      return escape_json_string(cell.text);
    case SqlCell::Kind::kBytes: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "\"<binary %zu bytes>\"", cell.bytes.size());
      return std::string(buf);
    }
  }
  return "null";
}

std::string query_table_to_json(const QueryTable& table) {
  if (!table.ok) {
    return std::string("{\"error\":") + escape_json_string(table.error_text) + "}";
  }
  std::string json = "{\"columns\":[";
  for (size_t i = 0; i < table.columns.size(); ++i) {
    if (i) {
      json += ',';
    }
    json += escape_json_string(table.columns[i]);
  }
  json += "],\"rows\":[";
  for (size_t r = 0; r < table.rows.size(); ++r) {
    if (r) {
      json += ',';
    }
    json += '[';
    const auto& row = table.rows[r];
    for (size_t c = 0; c < row.size(); ++c) {
      if (c) {
        json += ',';
      }
      json += sql_cell_to_json(row[c]);
    }
    json += ']';
  }
  json += "]}";
  return json;
}

}  // namespace

std::string tp_query_to_json(tp::TraceProcessor* processor, const std::string& sql) {
  return query_table_to_json(tp_query_to_table(processor, sql));
}

std::string tp_error_json_response(std::string_view message) {
  return std::string("{\"error\":") + escape_json_string(message) + "}";
}

}  // namespace embed_demo
}  // namespace cnperf
