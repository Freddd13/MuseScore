# MuseScore 3 Evolution 个人分支：AI 代码指南

用户实际功能与开关先见 [用户功能说明](../USER_GUIDE.md)，每次功能更新与开发日志分别维护。

本套指南用于减少后续会话的重复全库扫描。先读此页，再按任务读取一两个专题，并核对相应源码；不要一次把全部文档加入上下文。

- 源码基准：`f2a80b9f59dd396698b3b507a19a56bfb8791af2`，`3.x`，2026-10-05 整理。
- 应用版本：`config.cmake` 为 `3.7.0`；个人维护版本见 [VERSION](../VERSION)。
- 个人仓库：`origin = git@github.com:Freddd13/MuseScore.git`；上游是 Jojo-Schmitz/MuseScore。2026-10-05 后续构建任务已核对上游 `3.x` HEAD 与上述源码基准相同；最新状态仍需重新核对，见 [09](09-windows-build-check.md)。
- 本机仓库：`E:\programming\funcodes\muse3_dev\MuseScore`；外层工作区不是 Git 仓库。
- 证据等级：结构与调用链来自源码静态阅读，首次调查环境见 [基准快照](08-baseline-and-limits.md)。后续 Debug 编译、安装、版本启动实测已通过；离屏 PDF 生成后退出超时，运行限制见 [09](09-windows-build-check.md)。

## 60 秒架构模型

```text
main/main.cpp -> Ms::runApplication (mscore/musescore.cpp)
Qt Widgets + 部分 QML -> MuseScore / ScoreView / Inspector / PianorollEditor
界面命令 -> Score::startCmd -> undo 操作 -> Score::endCmd
libmscore: 乐谱对象 + 编辑规则 + 撤销 + 排版 + MSCX/MSCZ + MIDI 渲染
播放: MidiRenderer -> Seq -> MasterSynthesizer -> Driver
导入: mscore/file.cpp -> importexport/* -> 同一套 Score 模型
```

这是 Qt 5 / C++20 的 MuseScore 3 演进代码。`libmscore` 不是纯数据层，`mscore` 也不只有窗口；音序器、文件格式分发、偏好、插件均在该目录。`global` 的服务注入只覆盖部分模块，不能视为全应用统一模块架构。

## 按任务读取

| 任务 | 首读 | 接着定位 |
| --- | --- | --- |
| 理解目录、依赖、启动 | [01 架构与结构](01-architecture.md) | `main/`、各层 CMake |
| 音符、和弦、节奏、声部、属性 | [02 数据模型与不变量](02-score-model.md) | `score.h`、`note.h`、`cmd.cpp` |
| 菜单/快捷键、撤销、排版、读写、播放 | [03 关键调用链](03-execution-flows.md) | 对应调用链的 3–5 个入口 |
| 根据功能找文件/测试 | [04 功能开发定位表](04-feature-map.md) | [源码索引](source-map.tsv) |
| 钢琴卷帘、双谱表、踏板、MIDI、力度 | [05 钢琴开发专题](05-piano-development.md) | `pianoroll/`、`rendermidi.cpp` |
| Windows 构建、测试、调试 | [06 构建与验证](06-build-and-test.md) | CMake、`mtest/`、CI 脚本 |
| 当前电脑如何编译、实测阻塞与本地 SDK | [09 Windows 构建实测](09-windows-build-check.md) | `personal/tools/build_windows.ps1` |
| 自动输入/时值修改的节拍分组与延音链 | [12 自动规范输入时值](12-input-rhythm.md) | `libmscore/inputrhythm.*`、`mtest/libmscore/inputrhythm` |
| 节奏规范依据、复合拍子、旧菜单开关与中日文字体回退 | [13 规则与字体诊断](13-rhythm-rules-and-font-fallback.md) | `durationtype.cpp`、`textbase.cpp`、`MuseScore::updateMenus` |
| 大型 SF2、预加载、音源失败与性能验证 | [11 大型 SF2](11-large-sf2.md) | `audio/midi/fluid/`、`mtest/audio/sfloader` |
| 开发、版本、日志、提交、上游合并 | [07 开发流程](07-development-workflow.md) | [更新日志](../CHANGELOG.md) |
| 判断哪些结论需重新验证 | [08 基准与限制](08-baseline-and-limits.md) | [机器快照](baseline.json) |

## 每次新任务的最小读取顺序

1. [仓库约束](../../AGENTS.md)、本页、最新更新日志；检查工作区。
2. 用功能表挑路径，用 TSV 找符号。读对应 `.h` 理解状态，再读实现与邻近调用者。
3. 读最接近的现有测试与样例谱；需要修改再沿撤销、持久化、排版、播放方向扩展阅读。
4. 完成后更新个人版本、受影响的指南和日志；提交并在远端状态允许时推送。

```powershell
# 在 MuseScore 仓库内执行；搜索范围尽量从相关目录开始
rg -n '跨谱表|卷帘|播放|力度|导入' personal/docs/source-map.tsv
rg -n 'Score::splitStaff|staffMove|vStaffIdx' libmscore
rg -n 'PianorollEditor::|PianoView::' mscore/pianoroll
python personal/tools/check_guides.py
```

TSV 包含主题、源码路径、搜索锚点、基准行号、职责与邻近测试路径；测试列 `-` 表示没有列出直接测试，不表示该功能已验证。行号是基准快照的导航提示，更新源码后必须重新查找符号；校验器会报出漂移，不会自动修订知识结论。原始领域说明还可按需读取 [pitch](../../doc/pitch.md)、[tpc](../../doc/tpc.md)、[ticklength](../../doc/ticklength.md)。

## 个人扩展：和声辅助

个人版本 0.8.0 提供区间输入元数据、原生和弦字形与状态颜色保护，以及通用播放观察、踏板/时序快照、屏幕临时颜色/标注和自适应停靠，和声解释仍在独立插件；入口与边界见 [10 通用观察/预览](10-score-observer.md)。

## 个人扩展：演奏参数编辑

个人版本 0.10.0 完善演奏编辑器：连续播放线、独立纵向视窗、紧凑键条/音名、双向原生批选、声部多选过滤和可调配色；继续使用原生属性和撤销。方案、操作、播放边界与维护入口见 [14 演奏编辑器](14-performance-editor.md)。


个人 0.12 已加入演奏编辑器单音滚轮、Evolution 其他选项栏按钮及启动／关于 Kumo 身份。操作见 [用户功能说明](../USER_GUIDE.md)，模块入口见 [14](14-performance-editor.md)，生成版本标识的维护要求见 [07](07-development-workflow.md)。


## 0.13.0 界面与谱行交互修复

个人 0.13.0 完善截图风格力度色、键盘、MIDI 基线缩放与谱行带播放稳定性，支持未选音头滚轮，工具栏按钮位于末尾。操作见 [用户说明](../USER_GUIDE.md)，实现与回归见 [14](14-performance-editor.md)。

## 钢琴演奏与记谱扩展

0.14.0 的琶音对拍、预备时间和参数拖拽见 [15](15-piano-expression.md)。

0.14.1：新琶音保留续接音的时长事件供 MIDI 延音链累加，续接音仍不参与展开、不重触发；见 [钢琴演奏扩展](15-piano-expression.md)。
