# AI连接现在怎样工作

核对日期：2026-11-08。当前程序0.166.0，随包MCP客户端2.7.2，通信桥2.7.0，共44项工具。当前工具覆盖读谱、原生谱图、建议、试听和编辑；多轨MIDI与动态视频的专用AI操作安排在后续接口扩展中。

## 从哪里开始

在MuseScore打开乐谱，选择要讨论的一段，打开“视图 → AI连接”，点击“检查连接”。首次使用点“配置Codex”，然后在Codex重新连接MuseScore工具。

可以直接说：“确认当前谱名和选区，读取这四小节的完整声部及MuseScore原生谱图，先分析左手有没有更顺的写法。”

Codex先确认当前窗口和乐谱，再读取内存中的内容。还没有保存的改动也能读到。返回的谱面标题、作品信息标题、文件名和标签页名分别记录，便于核对旧谱中保留的不同名称。

## 程序怎样连接

```text
Codex中的MuseScore工具
    ↓ stdio
随程序提供的MuseScoreMCP.exe
    ↓ 本机WebSocket
程序内置的MuseScoreBridge.qml
    ↓ ScoreObserver及工作流接口
当前内存乐谱、原生排版、播放与撤销
```

MCP程序用Python和FastMCP编写，安装包已包含运行环境。它负责工具协议、分页数据整理、任务记录及请求转发。模型分析和对话由Codex完成。

MuseScore的连接入口已内置。通信桥仍使用QML及Qt WebSockets；实际读谱、谱图、演奏读取和局部修改由C++完成。插件仓库保留早期QML入口，日常使用通过内置“AI连接”。

内置服务绑定127.0.0.1，从8765至8864寻找空闲端口。每个运行窗口发布自己的实例记录。打开多个MuseScore窗口时，在“AI连接”选择本次窗口，Codex读取该窗口当前的乐谱标签页。

## 三处开关与进程列表

| 控件 | 作用 |
|---|---|
| 开启／停止读谱连接 | 控制MuseScore接收工具请求 |
| 启动软件时自动开启 | 决定下一次启动MuseScore是否准备连接 |
| 允许Codex启动MuseScore MCP | 更新Codex中本服务的启用设置，下次连接时采用 |
| Codex连接列表 | 查看进程、版本、启动时间、最近请求和活动状态 |
| 结束所选连接 | 关闭所选MCP进程；其它会话继续使用自己的连接 |

每个Codex会话通常有自己的MCP进程。关闭AI连接说明窗口后，已开启的服务继续运行。程序旁的`kumo.ini`中`[features] enable_exp_feature`决定个人扩展是否开启。

## 全部44个工具

### 确认连接与当前对象

| 工具 | 主要输入及结果 |
|---|---|
| `connect_to_musescore` | 连接当前选择的程序窗口 |
| `ping_musescore` | 检查本机通信是否响应 |
| `get_capabilities` | 返回桥、程序、客户端版本及可用接口 |
| `get_score_identity` | 返回窗口、标签页、总谱／分谱、可见标题、作品信息、路径及保存状态 |
| `get_cursor_info` | 返回当前输入／浏览位置 |

### 读取选段与继续任务

| 工具 | 主要输入及结果 |
|---|---|
| `read_arrangement_context` | `request`指定scope、whole、小节范围、purpose及fields；返回contextId、内容版本、完整音乐及资料 |
| `start_arrangement_read` | 开始可取消的分批读取，返回taskId |
| `arrangement_task` | 按taskId查看进度或取消 |
| `get_arrangement_context` | 按contextId取得完成的读取结果 |
| `get_arrangement_page` | 按snapshotId、范围、fromTick及limit读取一批原生数据 |
| `get_arrangement_state` | 读取版本、范围和可用字段 |
| `refresh_arrangement_context` | 乐谱修改后更新任务内容，保留原先范围及要求 |

普通音乐判断使用`purpose=analysis`；比较实际起音、力度和踏板使用`purpose=performance`；排版检查使用`purpose=engraving`。几何与演奏数据可以按需读取。

每批原生查询最多64项，Python根据宿主实际耗时调整批量，GUI读取以约2毫秒为目标。结果保留原生tick、精确四分音符单位和反复位置，同时提供声部、音高拼写、延音关系、分手、指法及可用和弦解释。

### 音符数据与图像

| 工具 | 用途 |
|---|---|
| `get_score_data` | 按起止小节返回结构化音符；当前宿主使用原生分页读取再整理为兼容结构 |
| `get_score` | 返回当前谱的LilyPond音符文本 |
| `get_score_image` | 输入context_id、image_index，返回MuseScore实际排版的PNG及相关音符位置 |
| `show_score` | 根据音符数据绘制简化示意图，适合浏览音高和时值 |

要讨论字体、延音线、符杠、跨谱表和手动摆放，请明确要求“原生谱图”。`get_score_image`与读谱使用同一contextId，长范围按图读取。当前取图需要暂停播放。0.166修正了原生谱图的设备分辨率，文字和音乐符号按原谱比例显示。

### 保存资料和比较写法

| 工具 | 用途 |
|---|---|
| `save_arrangement_information` | 保存用户提供的角色、解释、材料及对应关系，记录来源和作用范围 |
| `submit_arrangement_options` | 对contextId提交1至3版局部写法，含说明、范围及修改 |
| `arrangement_option` | 查看谱面差异、读取演奏差异、试听原版／候选、采用、存为另一种写法或停止 |
| `arrangement_operation_result` | 按operationId查询一次操作的实际结果 |

逐音修改使用绑定读取版本的音符引用；重写声部使用覆盖整个范围的音符／休止序列，时值使用分数。默认保留指定旋律、范围长度和所采用和声；本次任务可以明确改变这些条件。

原生接口先检查候选，在副本中准备，再交给“编配建议”查看、试听。采用写入当前乐谱，一次采用对应一次撤销；存为另一种写法则使用已有分段方案。

### 定位与选择

`go_to_beginning_of_score`、`go_to_final_measure`、`go_to_measure`、`next_element`、`prev_element`、`next_staff`、`prev_staff`、`select_current_measure`、`select_custom_range`。

这些入口用于移动光标和选区。任务中普通浏览可以改变当前选音；继续原任务时使用保存的任务范围重新读取。

### 直接编辑

`add_note`、`add_rest`、`add_tuplet`、`append_measure`、`insert_measure`、`delete_selection`、`add_lyrics`、`add_instrument`。

按当前输入位置或明确参数完成修改。新增音符可以顺序输入，也能输入同一和弦。复杂局部编配优先采用候选流程，便于整段核对与撤销。

### 速度、拍号及乐器

| 工具 | 当前行为 |
|---|---|
| `set_tempo` | 在当前位置添加可见的四分音符速度记号 |
| `set_time_signature` | 设置拍号 |
| `set_instrument_sound` | 打开乐器选择窗口，由用户手动选择 |
| `set_staff_mute` | 早期谱表显隐接口；当前宿主返回不支持，声音静音使用混音器 |

后两项需要在后续工具整理中调整名称或补齐能力，避免名称让人误解。

### 批处理、撤销与重做

`processSequence`依次执行一组`action/params`命令，返回执行结果。中间失败时检查已完成部分。`undo`撤销，`redo`重做。局部候选采用另有完整预检和一次撤销。

## 一次编配任务的完整例子

1. 在MuseScore选中第9至12小节。
2. 对Codex说：“确认当前谱和选区，读取完整声部及原生谱图。保留旋律，为左手提出两版更易弹的写法。”
3. Codex确认身份，读取音乐与跨边界延音、踏板，再取得同一段原图。
4. 提交两版建议，在MuseScore“编配建议”中选择一版看差异。
5. 点“试听谱中”和“试听这版”比较声音。继续修改要求时，更新任务数据后重新准备建议。
6. 点“使用这版”，或让Codex采用指定候选；随后读取实际写入结果。
7. Ctrl+Z撤销，Ctrl+Shift+Z重做，Ctrl+S保存。

## 文件与数据放在哪里

- MCP入口：程序目录`tools/mcp/current.json`选择版本目录，里面是`MuseScoreMCP.exe`及运行文件。
- Codex配置：`CODEX_HOME/config.toml`，默认位于用户`.codex`目录。配置前保存备份，并保留其它服务和个人设置。
- 本机实例及会话：`%LOCALAPPDATA%/Freddd13/MuseScoreMCP`，可由`MCP_MUSESCORE_STATE_DIR`指定。
- 分批读到的任务：MCP进程内存，目前保留最多4份、共32MiB，重新连接后可重新读取。
- 主动保存的编曲资料：乐谱中的`kumo.*`资料分区，支持原生撤销。
- 采用的内容：当前乐谱；保存的另一种写法使用分段方案资料。
- 聊天：Codex会话。

## 开发入口

- 宿主连接与面板：`mscore/workflow/aiconnection.cpp`、`aiconnectionpanel.inc`。
- 内置通信桥：`share/workflow/MuseScoreBridge.qml`。
- 原生读取、谱图及候选：`mscore/plugin/api/scoreobserver.h`及`mscore/workflow/arrangement*.cpp`。
- MCP：`mcp-musescore/launcher.py`、`server.py`、`src/runtime.py`、`src/sessions.py`、`src/context/service.py`和`src/tools`。

后续讨论重点是统一读谱与原图的常用流程、减少名称含义不清的入口、补齐音源控制，并把新的MIDI轨道及视频材料以明确范围提供给AI。具体接口调整继续与用户确定。
