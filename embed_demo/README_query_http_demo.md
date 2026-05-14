# Trace Processor query demo（HTTP 可选 + **纯本地**）

## 纯本地（无 HTTP）：`query_local_demo`

不监听端口、不走套接字协议：进程内加载 trace，执行一条 SQL，**把 JSON 打到 stdout** 后退出。适合：

- **CLI / 脚本调试**、快速验证 SQL；
- **cnperf-cli 子命令**（若接受「stdout 为 JSON」形态）。

若目标是 **cnperf-gui 不解析 JSON 就填 model**，请用下面 **「与 cnperf-gui」** 小节的 **进程内 `tp_query_to_table`**，而不是依赖本可执行文件的 stdout。

```bash
cd perfetto_demo/embed_demo
./build.sh
./query_local_demo ../my_trace.perfetto-trace "SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 5"
# 或 SQL 来自 stdin:
echo "SELECT COUNT(*) AS n FROM slice" | ./query_local_demo ../my_trace.perfetto-trace
./demo_local_client.py ../my_trace.perfetto-trace "SELECT 1"
```

退出码：0 成功；2 `ReadTrace` 失败；3 SQL 执行失败（返回体为 `{"error":...}` 且 stdout 仍会打印该 JSON）。

### 与 cnperf-gui：用结构化结果填 model（推荐）

- **进程内链接**（与 Trace Processor 同进程）：包含 `trace_query_table.hpp` / `trace_query_table.cpp`，调用
  **`cnperf::embed_demo::tp_query_to_table(processor, sql)`**，得到 **`QueryTable`**
 （`columns` + `rows`，每个 **`SqlCell`** 带 `kind` 与已拷贝的 `text`/`bytes`/数值）。在 C++ 里按列名与 `SqlCell::Kind`
  写入 `CNPerfTimelineModel` 等，**不需要**把查询结果再走 JSON parse。
- **`tp_query_to_json` / `query_local_demo` 的 stdout JSON**：仅适合 **CLI、子进程管道、HTTP demo**；若 GUI 用
  `QProcess` 读 stdout，就**仍然**要解析 JSON——这与「免解析填 model」目标不一致；要免解析应 **链接同一套
  `tp_query_to_table` 代码进 GUI 或 cnperf 进程**（或以后换 protobuf 等行协议，而不是手写 trace JSON）。

---

## HTTP 版：`query_http_demo`（可选）

本目录的 `query_http_demo.cpp` 在**一个进程**里：

1. 用 Perfetto **Trace Processor** 加载一个 trace 文件（与 `embed_demo` 相同，支持
   `.perfetto-trace`、`timechart_data.json` 等 TP 能识别的格式）；
2. 在 **localhost** 上起一个极简 **HTTP** 服务；
3. 对 **`POST /query`**（body 为**原始 SQL**，需带 `Content-Length`）执行查询，返回
   **`application/json`**：`{"columns":[...],"rows":[[...],...]}`；失败时为
   `{"error":"..."}`。

这与「将来在 **cnperf** 里嵌 TP、再交给 **cnperf-gui**」的两种常见接法一致：

- **子进程 + stdout**：`query_local_demo` 形态（无 HTTP，全本地）；
- **常驻 + HTTP**：`query_http_demo` 形态（仍是本机 `127.0.0.1`，仅协议为 HTTP）。

当前 demo 放在 `perfetto_demo` 里，避免动主工程 CMake。

## 构建

在仓库根下（或本目录）执行：

```bash
cd perfetto_demo/embed_demo
./build.sh
```

产物：`perfetto/out/release/query_http_demo`、`query_local_demo`，本目录下符号链接
`./query_http_demo`、`./query_local_demo`。

## 运行

终端 1（加载 trace 并监听，默认端口 **8765**）：

```bash
cd perfetto_demo/embed_demo
./query_http_demo ../my_trace.perfetto-trace 8765
```

终端 2 — **curl**（模拟 GUI 发 SQL）：

```bash
curl -sS -X POST http://127.0.0.1:8765/query --data-binary \
  "SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 5" | jq .
```

终端 2 — **Python**（同上，带默认 SQL）：

```bash
./demo_gui_client.py --url http://127.0.0.1:8765/query \
  "SELECT COUNT(*) AS n FROM slice"
```

## 与正式 cnperf 集成的关系

- 本 demo **不包含** cnperf 的 SQLite（`cnperf_data*.db`）导入；进 TP 的输入仍是 **TP 支持的 trace 文件**。
- 下一步若落在 cnperf：可把 `tp_query_to_json` 所在逻辑迁到库内供 **同一进程** 直接调用；或保留
  `query_local_demo` 式 **子进程边界**；HTTP 版仅在你需要 **多语言客户端 / 与浏览器同源** 时再引入。

## 限制（刻意保持简单）

- 仅 **POST /query**；未实现 Perfetto UI 使用的 **protobuf-over-HTTP** RPC。
- 单线程顺序处理连接；无 TLS。
- 未支持 `chunked` 编码；请使用带 **`Content-Length`** 的客户端（`curl --data-binary`、本仓库的 `demo_gui_client.py` 均可）。
