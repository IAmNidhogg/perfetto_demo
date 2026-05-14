// Local demo: same Trace Processor embed as embed_demo, no HTTP — load trace,
// run one SQL from argv or stdin, print JSON to stdout (subprocess / CLI).
//
// Usage:
//   query_local_demo <trace_file> "<sql>"
//   query_local_demo <trace_file>   # SQL read from stdin (until EOF)

#include "trace_query_json.hpp"

#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace tp = perfetto::trace_processor;

namespace {

std::string read_all_stdin() {
  std::string s;
  char buf[4096];
  for (;;) {
    const size_t n = std::fread(buf, 1, sizeof(buf), stdin);
    if (n == 0) {
      break;
    }
    s.append(buf, n);
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage:\n"
                 "  %s <trace_file> \"<sql>\"\n"
                 "  %s <trace_file>   # SQL from stdin until EOF\n",
                 argv[0], argv[0]);
    return 1;
  }

  const char* trace_path = argv[1];
  std::string sql;
  if (argc >= 3) {
    sql = argv[2];
  } else {
    sql = read_all_stdin();
  }

  while (!sql.empty() && (sql.back() == '\n' || sql.back() == '\r' || sql.back() == ' ' || sql.back() == '\t')) {
    sql.pop_back();
  }
  if (sql.empty()) {
    std::fprintf(stderr, "empty SQL (pass as argv or stdin)\n");
    return 1;
  }

  tp::Config config;
  std::unique_ptr<tp::TraceProcessor> processor = tp::TraceProcessor::CreateInstance(config);
  const auto load_status = tp::ReadTrace(processor.get(), trace_path);
  if (!load_status.ok()) {
    std::fprintf(stderr, "ReadTrace failed: %s\n", load_status.c_message());
    return 2;
  }

  const std::string json = cnperf::embed_demo::tp_query_to_json(processor.get(), sql);
  std::printf("%s\n", json.c_str());

  if (json.find("\"error\"") != std::string::npos && json.find("\"columns\"") == std::string::npos) {
    return 3;
  }
  return 0;
}
