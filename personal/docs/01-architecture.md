# 01 架构、项目结构与边界

阅读目的：确定需求落在哪一层，避免因上游 README 的旧目录名或 MuseScore 4 经验走错入口。基准与 [指南入口](README.md) 一致。

## 构建出来的实际层次

| CMake 目标 | 主要来源 | 实际职责/依赖 |
| --- | --- | --- |
| `mscore` 可执行文件 | [main/CMakeLists.txt](../../main/CMakeLists.txt)、[main.cpp](../../main/main.cpp) | 入口、资源初始化、平台参数适配；链接 `mscoreapp`、`global` |
| `mscoreapp` 静态库 | [mscore/CMakeLists.txt](../../mscore/CMakeLists.txt) | 应用 UI、控制器、音序器、文件分发、插件；链接核心、音频、导入、效果器、资源等 |
| `libmscore` 静态库 | [libmscore/CMakeLists.txt](../../libmscore/CMakeLists.txt) | 乐谱模型、编辑、排版、文件读写、MIDI 渲染；链接 FreeType、`preferences` |
| `audio` 静态库 | [audio/CMakeLists.txt](../../audio/CMakeLists.txt) | MIDI 文件/事件、合成器、MIDI/MP3 导出；链接核心、偏好、图标、扩展、音频文件库 |
| `importexport` 静态库 | [importexport/CMakeLists.txt](../../importexport/CMakeLists.txt) | 各外部谱面格式导入，MusicXML 导出；汇入多个 `.cmake` 源文件列表 |
| `preferences` / `extension` / `icons` | `mscore/CMakeLists.txt` 中拆出 | 减少部分静态库耦合；源码仍在 `mscore/` |
| `global` | [global/CMakeLists.txt](../../global/CMakeLists.txt) | 模块服务解析/注入、公共定义；主要为可选模块提供基础设施 |
| `effects` / `audiofile` / `awl` / `qzip` | 各同名目录（qzip 在 thirdparty） | 音效、音频文件封装、音频控件、ZIP 支持 |

这是依赖较强的传统桌面应用，不宜把目录误解为严格无环的分层。源码中已有针对静态库循环依赖的修补，例如 `audio/CMakeLists.txt` 明确不反向链接 `mscoreapp`。新增功能不要破坏这些局部边界。

## 目录全景

| 目录 | 功能分布 | 后续读取建议 |
| --- | --- | --- |
| `main/` | 主入口、ModulesSetup、可执行目标和安装部署 | 启动/打包问题首读 |
| `libmscore/` | 约 300 多个文件，模型/编辑/排版/读写/播放渲染集中于此 | 优先按符号定位，避免整目录加载 |
| `mscore/` | 桌面应用、乐谱视图、对话框、设置、资源、插件、面板 | 按子目录与主窗口入口拆开阅读 |
| `mscore/pianoroll/` | 卷帘编辑器、网格、键盘、参数曲线、事件微调 | 钢琴定制优先入口 |
| `mscore/inspector/` | 元素属性检查器，`.cpp/.h/.ui` 与公共映射机制 | 属性 UI 需求 |
| `mscore/palette/` + `mscore/qml/` | 调色板模型/工作区、QML 引擎；部分外观资源还在 `mscore/data/` | 不要把 QML 只当插件 |
| `mscore/plugin/api/` | QML 插件对模型的包装层 | 新插件能力/无核心改动实现 |
| `mscore/mixer/`、`importmidi_ui/` | 混音器、MIDI 导入选项界面 | 分别追踪通道模型、导入 operation |
| `mscore/migration/` | 3.6 字体/默认样式/位置迁移与提示 | 新旧谱兼容问题 |
| `mscore/script/`、`debugger/`、`widgets/` | 脚本录制测试、调试查看、复用控件 | 辅助设施，按需读 |
| `importexport/` | MusicXML、MIDI import、Guitar Pro/PTB、Capella、OVE、BWW、BIAB、MuseData | 见功能表；MIDI export 在 audio |
| `audio/midi/` | MIDI 事件、文件、MasterSynthesizer、Fluid、Zerberus | 旧 README 的顶层 fluid/msynth 说明不适用；[大型 SF2](11-large-sf2.md) 在 Fluid 加载边界内扩展 |
| `audio/exports/`、`audiofile/` | MIDI/MP3 导出、音频文件写入 | 音频导出另看 `mscore/exportaudio.cpp` |
| `audiodrivers/` | PortAudio、PortMidi、JACK、ALSA、PulseAudio 驱动 | 由 `mscore/CMakeLists.txt` include `.cmake` 汇入，非独立同名目标 |
| `effects/`、`aeolus/`、`awl/` | 效果器、可选管风琴合成、音频 UI 控件 | 大多不涉及谱面规则 |
| `share/` | 安装后的乐器 XML、预设、模板、插件、样式、工作区、翻译、音色库 | 数据驱动需求先查看这里 |
| `fonts/`、`assets/`、`fonttools/` | 字体/字形资源、品牌图片、字体辅助工具 | 记谱字体还需看 `libmscore/sym.*` |
| `build/` | CMake 模块、配置、CI、平台安装、打包工具 | 这里不是实际编译输出目录 |
| `mtest/` | QTest 单元与回归测试、谱例/参考文件 | 编辑/读写/导入规则验证首选 |
| `vtest/` | 参考 PNG 与视觉对比脚本 | 排版与绘制变化 |
| `test/` | 遗留测试、样本资料 | 不能假设均被 CTest 执行 |
| `doc/`、`rdoc/` | 插件/音高/时间等开发说明、旧帮助素材 | 查领域细节；不是完整架构文档 |
| `thirdparty/` | FreeType、ZIP、singleapp、PortMidi、BeatRoot、dtl、rtf2html 等 | 优先修改调用处，少改供应方源码 |
| `bww2mxml/`、`miditools/`、`demos/` | 独立转换/辅助工具、演示谱 | 默认构建范围看顶层 CMake |
| `omr/`、`avsomr/` | 可选 PDF/视觉识谱、外部识谱服务模块 | 默认 OFF；不要推断当前启用 |
| `telemetry/`、`crashreporter/`、`snap/`、`.github/`、`.tx/` | 统计、崩溃、发行、CI、翻译配置 | 构建/发布相关任务 |
| `personal/` | 本分支个人版本、日志、AI 指南、导航校验与 Windows 本地构建入口 | 不修改上游构建文件；[实测与脚本](09-windows-build-check.md) |

完整的 Git 跟踪文件数量/类型统计保存在 [baseline.json](baseline.json)，不是运行时依赖清单。

## UI 与核心的关系

[MuseScore](../../mscore/musescore.h) 是主窗口与大量应用协调逻辑，维护当前 Score/ScoreView、菜单、面板、设置、文件与音序器；[ScoreView](../../mscore/scoreview.h) 是乐谱交互视图。多个视图可查看同一个 Score；分谱有自己的 Score，但借 MasterScore 共用关键状态。

[MuseScoreView](../../libmscore/mscoreview.h) 是核心通知视图的接口，[MuseScoreCore](../../libmscore/musescoreCore.h) 是应用端回调接口。核心可通过它们触发界面同步；测试需要初始化相应替代环境。不要在 `libmscore/` 新增直接操作主窗口 widget 的依赖。

界面有三类技术：传统 Qt Widgets 与 Designer `.ui`、应用内 QML（例如调色板和迁移界面）、QML 用户插件。QML 资源注册看 [qml.qrc](../../mscore/qml.qrc)，插件注册和包装看 `mscore/plugin/`。它们使用同一模型，但扩展入口不同。

## 启动与配置入口

[main.cpp](../../main/main.cpp) 的 `main()` 注册资源，执行 [ModulesSetup::setup](../../main/modulessetup.cpp)，处理 Windows UTF-16 参数到 UTF-8，再进入 `Ms::runApplication()`。主要应用初始化、CLI 解析、Seq 与主窗口创建仍在 [musescore.cpp](../../mscore/musescore.cpp)。

`ModulesSetup` 在基准中按编译宏注册 telemetry 与 AVSOMR；[ServicesResolver](../../global/servicesresolver.h)、[ServiceInjector](../../global/serviceinjector.h) 不意味着 Score、ScoreView、Seq 都经同一 IoC 容器构建。

三个不同的“配置”需区分：

- 编译能力：顶层 [CMakeLists.txt](../../CMakeLists.txt) 与 `build/config/*.cmake`，例如音频后端/识谱/崩溃报告。
- 应用偏好：`mscore/preferences.*`、`global/settings/types/preferencekeys.h`、偏好对话框；主要按用户保存。
- 乐谱样式：`libmscore/style.*` 与 `share/styles/*.mss`；属于谱面并影响排版，不能替代全局偏好。

## 解耦时的优先落点

纯资源/默认值需求先考虑模板、乐器 XML、样式或工作区；已有插件 API 足够时可用插件。新 UI 操作应留在对应面板与局部助手；新增可复用音乐规则才放核心。文件解析规则落对应 import/export；播放解释落 MIDI renderer；驱动只处理设备层问题。具体例子见 [功能表](04-feature-map.md) 与 [开发流程](07-development-workflow.md)。

## 通用插件观察与屏幕预览

`mscore/plugin/api/scoreobserver.*` 在 GUI 线程提供数值快照与 Seq 位置事件；`mscore/notepreview.h` 在 ScoreView 内维护临时图层。核心 Note 只增加 draw 重载，默认绘制/文件格式/音频回调保持原语义。插件发布快照在 `share/plugins/HarmonyAssistant/`。0.3.0 的 QmlPlugin 暴露停靠位置/悬浮状态；ScoreObserver 扩展踏板/时序快照、分批分析帧、两个屏幕层和原子文本/配置读写。谱面解释、交换格式和颜色规则仍在插件；不扩展音频线程或文件格式。详见 [10](10-score-observer.md)。

0.4.0 固定标记的几何和活动索引留在 NotePreviewLayers，ScoreView::paint 屏幕绘制，events.cpp 双击转发与 QmlPlugin::focusPanel 为局部接线。音乐模板、离调强调与样式配置仍在插件；没有新增模型/音频依赖。

0.5.0 将固定记号从音符颜色映射分离为时间标记向量，来源地址仅作析构清理。QmlPlugin 可提供一个共享 QML 控件的辅助停靠宿主；不重复创建分析或连接音频。

自动输入节奏策略集中于 [inputrhythm](../../libmscore/inputrhythm.cpp)，由应用偏好设置，核心编辑仍使用原节拍分组与 undo；详见 [12](12-input-rhythm.md)。

0.7.1 的自动时值开关由主窗口持有，注册进 Workspace 动作表，旧菜单恢复后在 `updateMenus()` 补齐；不扩展上游工作区文件格式。普通中日文回退属于 Qt 文字布局，与 ScoreText 的音乐字体回退不同；规则/诊断边界见 [13](13-rhythm-rules-and-font-fallback.md)。

## 演奏编辑器（0.9.0）

独立 `mscore/performanceeditor/` QWidget + MuseScoreView，通过主窗口选择/换谱和 ScoreView 屏幕绘制接线；属性事务与预览分离。`libmscore/notevelocity.*` 统一整数百分比转换；Score 的临时 tempo 批事务避免逐点重建。Seq 仅暴露后台渲染空闲/停播等待，不改音频线程。详见 [14](14-performance-editor.md)。
