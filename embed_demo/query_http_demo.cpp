// Minimal demo: embed Perfetto Trace Processor, load one trace file, expose
// POST /query with raw SQL body, return JSON { "columns", "rows", "error" }.
//
// Usage: query_http_demo <trace_file> [listen_port]
// Example:
//   ./query_http_demo ../my_trace.perfetto-trace 8765
//   curl -sS -X POST http://127.0.0.1:8765/query --data-binary \
//     "SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 5"

#include "trace_query_json.hpp"

#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace tp = perfetto::trace_processor;

namespace {

constexpr int k_backlog = 8;
constexpr size_t k_max_header = 65536;
constexpr size_t k_max_sql = 1024 * 1024;

bool recv_headers_and_body(int client_fd, std::string* out_sql, std::string* err) {
  std::string buf;
  buf.reserve(k_max_header);
  char chunk[4096];
  while (buf.size() < k_max_header) {
    const ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      *err = "short read while reading headers";
      return false;
    }
    buf.append(chunk, static_cast<size_t>(n));
    const size_t pos = buf.find("\r\n\r\n");
    if (pos != std::string::npos) {
      const std::string headers = buf.substr(0, pos);
      size_t content_length = 0;
      bool have_content_length = false;
      size_t line_start = 0;
      while (line_start < headers.size()) {
        const size_t line_end = headers.find("\r\n", line_start);
        if (line_end == std::string::npos) {
          break;
        }
        const std::string_view line(headers.data() + line_start, line_end - line_start);
        if (line.size() > 16 && (line[0] == 'C' || line[0] == 'c') &&
            line.substr(0, 15) == "Content-Length:") {
          size_t i = 15;
          while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
          }
          content_length = 0;
          for (; i < line.size(); ++i) {
            const char c = line[i];
            if (c < '0' || c > '9') {
              break;
            }
            content_length = content_length * 10U + static_cast<size_t>(c - '0');
          }
          have_content_length = true;
        }
        line_start = line_end + 2;
      }

      const size_t body_start = pos + 4;
      std::string body = buf.substr(body_start);
      if (have_content_length) {
        if (content_length > k_max_sql) {
          *err = "Content-Length too large";
          return false;
        }
        if (body.size() > content_length) {
          body.resize(content_length);
        }
        while (body.size() < content_length) {
          const size_t need = content_length - body.size();
          const ssize_t r = ::recv(client_fd, chunk, std::min(sizeof(chunk), need), 0);
          if (r <= 0) {
            *err = "short read while reading body";
            return false;
          }
          body.append(chunk, static_cast<size_t>(r));
        }
      } else {
        if (body.size() > k_max_sql) {
          *err = "body too large (send Content-Length)";
          return false;
        }
      }

      if (headers.size() < 12) {
        *err = "request too short";
        return false;
      }
      const std::string_view req_line(headers.data(), headers.find("\r\n"));
      if (req_line.find("POST ") != 0 && req_line.find("post ") != 0) {
        *err = "only POST supported";
        return false;
      }
      if (req_line.find(" /query") == std::string::npos && req_line.find("\t/query") == std::string::npos) {
        *err = "path must be /query";
        return false;
      }
      *out_sql = std::move(body);
      while (!out_sql->empty() && std::isspace(static_cast<unsigned char>((*out_sql)[out_sql->size() - 1]))) {
        out_sql->pop_back();
      }
      return true;
    }
  }
  *err = "headers too large or no end of headers";
  return false;
}

void send_http_json(int client_fd, int status, const std::string& json_body) {
  const char* status_text = (status == 200) ? "200 OK" : "400 Bad Request";
  char header[256];
  const int hn = std::snprintf(header, sizeof(header),
                               "HTTP/1.1 %s\r\n"
                               "Content-Type: application/json; charset=utf-8\r\n"
                               "Content-Length: %zu\r\n"
                               "Connection: close\r\n"
                               "\r\n",
                               status_text, json_body.size());
  if (hn > 0) {
    ::send(client_fd, header, static_cast<size_t>(hn), MSG_NOSIGNAL);
  }
  ::send(client_fd, json_body.data(), json_body.size(), MSG_NOSIGNAL);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <trace_file> [listen_port]\n"
                 "  Serves POST /query with raw SQL in body (Content-Length required).\n"
                 "  Example: curl -sS -X POST http://127.0.0.1:8765/query --data-binary \\\n"
                 "    \"SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 3\"\n",
                 argv[0]);
    return 1;
  }
  const char* trace_path = argv[1];
  const int port = (argc >= 3) ? std::atoi(argv[2]) : 8765;
  if (port <= 0 || port > 65535) {
    std::fprintf(stderr, "invalid port\n");
    return 1;
  }

  tp::Config config;
  std::unique_ptr<tp::TraceProcessor> processor = tp::TraceProcessor::CreateInstance(config);
  const auto load_status = tp::ReadTrace(processor.get(), trace_path);
  if (!load_status.ok()) {
    std::fprintf(stderr, "ReadTrace failed: %s\n", load_status.c_message());
    return 2;
  }

  const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    std::perror("socket");
    return 3;
  }
  int one = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::perror("bind");
    return 3;
  }
  if (::listen(listen_fd, k_backlog) != 0) {
    std::perror("listen");
    return 3;
  }

  std::fprintf(stderr, "query_http_demo: loaded %s\n", trace_path);
  std::fprintf(stderr, "listening on http://127.0.0.1:%d/query (POST, SQL body)\n", port);

  for (;;) {
    const int client_fd = ::accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
      std::perror("accept");
      continue;
    }
    std::string sql;
    std::string parse_err;
    if (!recv_headers_and_body(client_fd, &sql, &parse_err)) {
      send_http_json(client_fd, 400, cnperf::embed_demo::tp_error_json_response(parse_err));
      ::close(client_fd);
      continue;
    }
    if (sql.empty()) {
      send_http_json(client_fd, 400, "{\"error\":\"empty SQL body\"}");
      ::close(client_fd);
      continue;
    }

    const std::string json = cnperf::embed_demo::tp_query_to_json(processor.get(), sql);
    const int http_status = (json.find("\"error\"") != std::string::npos && json.find("\"columns\"") == std::string::npos)
                                ? 400
                                : 200;
    send_http_json(client_fd, http_status, json);
    ::close(client_fd);
  }
}
