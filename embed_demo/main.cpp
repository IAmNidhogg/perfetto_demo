// main.cpp — 嵌入 trace_processor 做 SQL 查询
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"

#include <cstdio>
#include <memory>
#include <string>

namespace tp = perfetto::trace_processor;

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s trace_file [sql]\n", argv[0]);
    return 1;
  }
  const char *path = argv[1];
  const std::string sql =
      argc >= 3 ? argv[2]
                : "SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 50";

  tp::Config config;
  std::unique_ptr<tp::TraceProcessor> processor =
      tp::TraceProcessor::CreateInstance(config);

  // ReadTrace() reads the file, calls Parse() on chunks, and internally
  // calls NotifyEndOfFile() once it sees EOF, so we don't need to do it.
  auto status = tp::ReadTrace(processor.get(), path);
  if (!status.ok()) {
    fprintf(stderr, "load failed: %s\n", status.c_message());
    return 2;
  }

  auto it = processor->ExecuteQuery(sql);
  // Header
  for (uint32_t i = 0; i < it.ColumnCount(); ++i) {
    printf("%s%s", i ? "\t" : "", it.GetColumnName(i).c_str());
  }
  printf("\n");

  // Rows
  while (it.Next()) {
    for (uint32_t i = 0; i < it.ColumnCount(); ++i) {
      tp::SqlValue v = it.Get(i);
      switch (v.type) {
      case tp::SqlValue::kLong:
        printf("%s%lld", i ? "\t" : "", static_cast<long long>(v.AsLong()));
        break;
      case tp::SqlValue::kDouble:
        printf("%s%g", i ? "\t" : "", v.AsDouble());
        break;
      case tp::SqlValue::kString:
        printf("%s%s", i ? "\t" : "", v.AsString());
        break;
      case tp::SqlValue::kBytes:
        printf("%s<%zu bytes>", i ? "\t" : "", v.bytes_count);
        break;
      case tp::SqlValue::kNull:
        printf("%sNULL", i ? "\t" : "");
        break;
      }
    }
    printf("\n");
  }
  if (!it.Status().ok()) {
    fprintf(stderr, "query failed: %s\n", it.Status().c_message());
    return 3;
  }
  return 0;
}
