# Perfetto Trace Processor 学习与嵌入工作区

本工作区记录了一次完整的 [Perfetto Trace Processor](https://perfetto.dev/docs/analysis/trace-processor)
上手过程：从手写一个 `.perfetto-trace` 文件、用官方 CLI 跑 SQL 查询，到把
`trace_processor` 作为 C++ 静态库**嵌入到自己的程序**里，全链路打通。

## 目录结构

```
.
├── README.md                       本文件
│
├── 工具与脚本
│   ├── trace_processor             官方 CLI 的 Python 启动器（首次运行会下载真二进制）
│   ├── traceconv                   trace 格式转换器（同样是启动器）
│   ├── gen_trace.py                手写 protobuf 字节，生成 my_trace.perfetto-trace
│   ├── fix_json_trace.py           将带字符串 pid/tid 的 Chrome JSON 规整化
│   └── check.sql                   一组用来验证 my_trace.perfetto-trace 的 SQL
│
├── 示例 trace 文件
│   ├── my_trace.perfetto-trace     由 gen_trace.py 生成的原生 protobuf trace
│   ├── timechart_data.json         真实的 cnperf-cli 输出（Chrome Trace Event Format）
│   └── timechart_data.fixed.json   fix_json_trace.py 处理后的干净版本
│
├── embed_demo/                     自己写的 C++ 嵌入示例（重点）
│   ├── main.cpp                    用 trace_processor 库做"加载 + SQL + 打印"
│   ├── build.sh                    一键构建脚本（gn gen + ninja + symlink）
│   └── embed_demo -> ../perfetto/out/release/embed_demo   构建产物的便捷符号链接
│
└── perfetto/                       Perfetto 源码克隆，用于构建静态库与 demo
    ├── BUILD.gn                    已 patch：把 //embed_demo 注册到 all_targets
    ├── embed_demo/
    │   ├── BUILD.gn                executable("embed_demo")，依赖 :trace_processor
    │   └── main.cpp -> ../../embed_demo/main.cpp
    └── out/release/                构建产物
        ├── embed_demo              ~131 MB 可执行（静态链接全部依赖）
        ├── libtrace_processor.a    ~293 MB 完整静态库
        └── trace_processor_shell   官方 shell 的本地构建版
```

## 快速使用

### 1. 跑一下生成好的 demo trace

```bash
# 不需要任何构建，trace_processor 第一次运行会下载真二进制（约 13 MB）
./trace_processor my_trace.perfetto-trace
> SELECT name, ts, dur FROM slice ORDER BY ts;
> .quit
```

### 2. 把 trace 拖到 Web UI

打开 https://ui.perfetto.dev ，把 `my_trace.perfetto-trace` 或任一 JSON
文件拖进去——能看到 2 条线程时间轴 + 1 条 counter 折线。

### 3. 用 C++ 嵌入版查询（核心成果）

```bash
# 默认 SQL：SELECT name, ts, dur FROM slice ORDER BY ts LIMIT 50
./embed_demo/embed_demo my_trace.perfetto-trace

# 自定义 SQL
./embed_demo/embed_demo timechart_data.json \
  "SELECT name, COUNT(*) AS n, SUM(dur) AS total_ns
   FROM slice WHERE dur > 0
   GROUP BY name ORDER BY total_ns DESC LIMIT 10"
```

## 关于离线运行

- `./trace_processor` 是 Python 启动器，首次运行需要联网下载二进制到
  `~/.local/share/perfetto/prebuilts/trace_processor_shell`；
- **下载后纯离线**——后续运行、`embed_demo`、SQL 查询、metric 等
  全部不连任何网络。

详见对话记录或下方"实现笔记"。

## 重新构建 `embed_demo`

```bash
# 首次完整构建（首次约 1~2 分钟，需要 gn gen + 编译 ~1700 个 TU + 链接）
./embed_demo/build.sh

# 改完 main.cpp 增量构建（5~10 秒）
./embed_demo/build.sh

# 清理重建
./embed_demo/build.sh clean
```

构建脚本会：

1. 调用 `tools/gn gen out/release --args='is_debug=false is_clang=true'`
2. 调用 `tools/ninja -C out/release embed_demo`
3. 在 `embed_demo/embed_demo` 处建立符号链接指向产物

依赖关系：`//embed_demo:embed_demo`
→ `//src/trace_processor:trace_processor`（一个 `complete_static_lib`，把
SQLite、protozero、protobuf-lite、zlib 等所有依赖打包进单一 `.a`）。

## 实现笔记

### 自己生成 `.perfetto-trace`

`gen_trace.py` 不依赖任何第三方库，纯手写 protobuf wire format：

- 用 `varint` / `length-delimited` 编码每个 `TracePacket`
- 描述了一个 `my_app` 进程，两条线程（`main` / `worker`）
- 嵌套 slice（`do_request` 内含 `parse_input` / `db_query`）
- 一个瞬时事件 `cache_miss`
- 一条 counter 轨道 `mem_usage_mb`

生成出 515 字节的二进制 trace，可被 trace_processor 完整解析。

### 修复"脏" JSON trace

`timechart_data.json` 来自 cnperf-cli，包含 1071 个事件。直接喂给
trace_processor 时会看到 `json_tokenizer_failure: 1` 警告——但实际上
1060 个 slice 都正常入库，仅 1 个缺 `ph` 字段的事件被丢弃。

`fix_json_trace.py` 做了三件事：

1. 把字符串 pid（`"Device0 task[Sync]"`）和字符串 tid
   （`"[PID 4587] Ctx:1 Q:0"`）映射成稳定整数 ID；
2. 同一字符串 tid 在不同 pid 下分配不同整数，避免误合并；
3. 补 `process_name` / `thread_name` 元数据事件，保留人类可读名称。

> 注：trace_processor 内部本来就会把字符串 pid/tid 哈希成整数，所以
> 即使不用 fix 脚本也能正常加载，只是会多一条无害警告。

### C++ 嵌入要点

`embed_demo/main.cpp` 仅依赖 3 个公共头：

```cpp
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"
```

核心 API：

| API | 作用 |
|---|---|
| `TraceProcessor::CreateInstance(Config)` | 构造一个内存中的 trace 引擎 |
| `ReadTrace(tp, path)` | 读文件、分块 `Parse()`、自动 `NotifyEndOfFile()` |
| `tp->ExecuteQuery(sql)` | 返回 `Iterator` |
| `it.Next()` / `it.Get(i)` / `it.GetColumnName(i)` / `it.ColumnCount()` | 遍历结果 |
| `it.Status()` | 查询是否成功 |

构建时的坑：

- perfetto 默认开 `-Werror -Wold-style-cast`，C 风格强制转换会被拒绝；
  必须用 `static_cast<long long>(...)` 等现代写法。
- `ReadTrace()` 内部已经调用 `NotifyEndOfFile()`，业务代码不要重复调，
  否则会打印警告。

### Trace Processor 能直接读哪些格式

无需任何转换即可直接 `ReadTrace()` 加载：

- Perfetto protobuf（`.perfetto-trace` / `.pftrace`）
- Chrome JSON / `chrome://tracing` JSON
- ftrace text / systrace
- Linux perf.data
- Android bugreport / logcat / Art method tracing
- Fuchsia trace、Gecko / Firefox profile、Ninja log 等

不支持的"业务 JSON"（不符合 Chrome Trace Event schema）必须先转换。

### `traceconv` 是单向的

`traceconv` 把 `.perfetto-trace` **转出**到 JSON / systrace / ctrace / text /
pprof / firefox 等格式；只有 `binary` 子命令支持反向，且输入必须是
perfetto Trace 的 **textproto**（不是任意 JSON）。

要把任意业务格式变成 `.perfetto-trace`，常见做法：

1. 直接喂给 Trace Processor（大多数标准格式都已支持）；
2. 写一个 Python 转换器，仿照 `gen_trace.py` 手写 protobuf 字节；
3. 写成 perfetto Trace textproto 后用 `traceconv binary` 转 binary。

## 验证命令速查

```bash
# 检查 trace 健康度
./trace_processor -Q "SELECT name, value FROM stats WHERE value > 0 \
                     AND severity IN ('error','data_loss')" my_trace.perfetto-trace

# 列出所有进程 / 线程 / counter
./trace_processor -Q "SELECT pid, name FROM process WHERE pid > 0" my_trace.perfetto-trace
./trace_processor -Q "SELECT tid, name FROM thread WHERE tid > 0" my_trace.perfetto-trace

# 找出耗时最多的 slice
./embed_demo/embed_demo timechart_data.json \
  "SELECT name, COUNT(*) AS n, SUM(dur) AS total_ns
   FROM slice WHERE dur > 0 GROUP BY name ORDER BY total_ns DESC LIMIT 10"

# 交互式探索：进入 shell 后敲 .tables / .schema slice
./trace_processor my_trace.perfetto-trace
```

## 参考链接

- Trace Processor 文档：https://perfetto.dev/docs/analysis/trace-processor
- PerfettoSQL Reference：https://perfetto.dev/docs/analysis/sql-tables
- Trace Processor Python API：https://perfetto.dev/docs/analysis/trace-processor#python-api
- Chrome Trace Event Format：https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview
- Perfetto UI：https://ui.perfetto.dev
- 源码仓库：https://github.com/google/perfetto
