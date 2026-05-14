#include "trace_query_table.hpp"

#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"

#include <utility>
namespace tp = perfetto::trace_processor;

namespace cnperf {
namespace embed_demo {
namespace {

SqlCell sql_value_to_cell(const tp::SqlValue& v) {
  SqlCell cell;
  switch (v.type) {
    case tp::SqlValue::kNull:
      cell.kind = SqlCell::Kind::kNull;
      break;
    case tp::SqlValue::kLong:
      cell.kind = SqlCell::Kind::kInt64;
      cell.int64_value = v.long_value;
      break;
    case tp::SqlValue::kDouble:
      cell.kind = SqlCell::Kind::kDouble;
      cell.double_value = v.double_value;
      break;
    case tp::SqlValue::kString:
      cell.kind = SqlCell::Kind::kString;
      if (v.string_value != nullptr) {
        cell.text = v.string_value;
      }
      break;
    case tp::SqlValue::kBytes:
      cell.kind = SqlCell::Kind::kBytes;
      if (v.bytes_value != nullptr && v.bytes_count > 0) {
        const auto* p = static_cast<const uint8_t*>(v.bytes_value);
        cell.bytes.assign(p, p + v.bytes_count);
      }
      break;
  }
  return cell;
}

}  // namespace

QueryTable tp_query_to_table(tp::TraceProcessor* processor, const std::string& sql) {
  QueryTable out;
  auto it = processor->ExecuteQuery(sql);
  const uint32_t col_count = it.ColumnCount();
  out.columns.reserve(col_count);
  for (uint32_t i = 0; i < col_count; ++i) {
    out.columns.push_back(it.GetColumnName(i));
  }

  while (it.Next()) {
    std::vector<SqlCell> row;
    row.reserve(col_count);
    for (uint32_t i = 0; i < col_count; ++i) {
      row.push_back(sql_value_to_cell(it.Get(i)));
    }
    out.rows.push_back(std::move(row));
  }

  if (!it.Status().ok()) {
    out.ok = false;
    out.error_text = it.Status().c_message();
    out.columns.clear();
    out.rows.clear();
  }
  return out;
}

}  // namespace embed_demo
}  // namespace cnperf
