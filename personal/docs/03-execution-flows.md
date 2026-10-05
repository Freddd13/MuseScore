# 03 关键调用链与扩展点

以下是阅读源码的导航链，不代表每个动作严格依次经过所有节点。行号可从 [source-map.tsv](source-map.tsv) 查找；行为以函数实现为准。

## 菜单、快捷键与输入

```text
mscore/shortcut.cpp（命令 ID、状态、默认快捷键、QAction）
    -> MuseScore::cmd(QAction*)（主窗口状态校验、needsScore、事务）
    -> ScoreView::cmd()（视图/编辑/输入模式的本地命令表）
    -> Score::cmd(QAction*, EditData&)（核心命令表）
    -> cmd.cpp / edit.cpp / noteentry.cpp 中的具体规则
```

[MuseScore::cmd](../../mscore/musescore.cpp) 读取 `QAction::data()` 得到命令字符串，查 Shortcut、判断当前状态/是否需要乐谱，并按 `isCmd()` 决定包 `startCmd/endCmd`。应用级命令还有 `MuseScore::cmd(QAction*, QString)` 等路径。部分对话框操作自己连接 QAction，不经过同一分发链。

[ScoreView::cmd](../../mscore/scoreview.cpp) 含本地命令表，找不到时转交 `Score::cmd()`；[libmscore/cmd.cpp](../../libmscore/cmd.cpp) 的命令表再调用具体模型操作。[mscore/events.cpp](../../mscore/events.cpp) 管鼠标/键盘事件及 `changeState(ViewState)`，[keyb.cpp](../../mscore/keyb.cpp) 也有键盘相关逻辑。基准中没有 `mscore/cmd.cpp` 或 `mscore/noteentry.cpp`，不要根据名称猜文件。

音符输入由 `ScoreView` 的输入模式/事件，进入 `Score::addPitch/putNote`（[noteentry.cpp](../../libmscore/noteentry.cpp)）与 `setNoteRest`（`libmscore/cmd.cpp`）。InputState 决定 track、时值、当前 Segment、插入/重新定音等；改变快捷键不会自动改变模型规则。

**新增命令的检查顺序**：找最接近的 Shortcut 条目 → 选择应用/视图/核心的处理层 → 核对状态和事务 → 接菜单/工具栏 → 设置/翻译 → 测试撤销与输入模式。优先复用既有 ID，不以多处字符串分支堆同一行为。

## 命令结束、撤销与视图同步

```text
Score::startCmd -> cmdState.reset + UndoStack.beginMacro
模型操作 -> UndoStack.push（执行 redo，记录子命令）
Score::endCmd -> 可能 rollback -> Score::update(false)
    -> 必要的 doLayoutRange + MuseScoreView 更新
    -> endMacro + dirty / playlistDirty / autosaveDirty
    -> MuseScoreCore::endCmd -> MuseScore::endCmd
    -> Inspector / Timeline / Mixer / 输入状态 / 插件回调等
```

见 [cmd.cpp](../../libmscore/cmd.cpp)、[undo.cpp](../../libmscore/undo.cpp) 与 [musescoreCore.h](../../libmscore/musescoreCore.h)。`Score::endCmd()` 在 readOnly 或全局 error 状态时可以回滚；空命令不产生有意义的撤销记录。

`Score::undoRedo()` 使用共享栈，更新布局/选择，并标记播放列表过期。主窗口 `endCmd(bool undoRedo)` 还有插件结束通知与界面同步，不应与 Score 的事务关闭函数混为一谈。

**扩展点**：业务规则产生 undo 命令并标记布局影响，视图只负责显示和交互。需要自定义复合操作时以一次用户动作一次撤销为目标，先核对调用层是否已有事务。

## 属性检查器

```text
inspector/inspector*.ui + InspectorItem(Pid, 控件, reset...)
    -> InspectorBase::mapSignals
    -> InspectorBase::valueChanged
    -> Score.startCmd -> 多选元素.undoChangeProperty -> Score.endCmd
```

[InspectorBase](../../mscore/inspector/inspectorBase.cpp) 处理多选、不同值、重置与样式 flags，并有递归/信号控制。新元素属性 UI 尽量复用这套映射；刷新控件引发 valueChanged 的递归需沿既有保护处理。对应元素是否支持属性仍由核心 `getProperty/setProperty/propertyDefault` 决定。

## 排版与绘制

```text
模型/样式变化 -> CmdState 的 tick/staff/layout flags
    -> Score::update -> Score::doLayoutRange（或 doLayout）
    -> LayoutContext + collect/layout 系列阶段
    -> Measure / Chord / Beam / System / Spanner 的布局
    -> Page、System、元素位置/shape 更新
    -> ScoreView 绘制、命中测试、抓手等
```

[layout.cpp](../../libmscore/layout.cpp) 是总体流程，连续视图另有 [layoutlinear.cpp](../../libmscore/layoutlinear.cpp)。横向间距、系统/分页、自动避让与跨谱表等会互相影响；[skyline.*](../../libmscore/skyline.cpp) 和 [shape.*](../../libmscore/shape.cpp) 用于碰撞空间。具体 beam/slur/chord 排版在各自实现中。

只变 `ScoreView::paintEvent()` 可能修了屏幕却没修 PDF/PNG/SVG；印刷元素位置应找核心布局。反之纯 UI 选择色/光标不应修改持久化的元素 color/offset。改变布局需要实际谱例视觉对比，不能只做 XML 比较。

## 打开、导入、保存与导出

```text
文件对话框/CLI -> MuseScore::readScore 或 Ms::readScore（mscore/file.cpp）
    -> 根据扩展名选择 loadMsc/loadCompressedMsc 或 importexport 导入器
    -> Score 数据 -> 兼容迁移/通道/布局准备 -> 主窗口展示

MuseScore::saveAs（格式分发）
    -> libmscore/scorefile.cpp 的 MSCX XML / MSCZ ZIP
    -> MusicXML exportxml / audio/exports/exportmidi
    -> PDF、PNG、SVG 绘制 / exportaudio 音频
```

[file.cpp](../../mscore/file.cpp) 是重要路由器，不能因目录名 `importexport` 就只读那里。[scorefile.cpp](../../libmscore/scorefile.cpp) 管 MSCX/MSCZ 的高层读写；各元素也有 `read/write`。历史读取分支涉及 [read114.cpp](../../libmscore/read114.cpp)、[read206.cpp](../../libmscore/read206.cpp)、[read302.cpp](../../libmscore/read302.cpp)，3.6 样式迁移另在 `mscore/migration/`。

MusicXML 当前导入入口为 [importmxml.cpp](../../importexport/musicxml/importmxml.cpp)，先 Pass1 读取并分析结构，再将设备 rewind/seek 后交给 Pass2 构建模型；相关实现在 `importmxmlpass1/2.*`，音高和时值还有独立 helpers。该目录还保留 `importxml.cpp`、`importxmlfirstpass.cpp` 等代码，修改前必须核对当前入口实际调用到哪一套。

MIDI 文件导入由 [importmidi.cpp](../../importexport/midiimport/importmidi.cpp) `importMidi/convertMidi`，结合节拍分析、左右手拆分、量化、连音符、声部、和弦等步骤，导入面板 operation 决定选项。这是把演奏时间转换成记谱结构的过程，不是实时 MIDI 输入。

**持久化扩展检查**：MSCX 与 MSCZ 往返 → 老谱缺失字段默认值 → 分谱/链接 → clipboard/paste → 外部格式能力；不要暗中改格式版本，也不要将导出未支持字段误记成保存丢失。

## 播放、导出音频与设备

```text
Score / Note / articulation / tempo / repeat / pedal
    -> Score::createPlayEvents + MidiRenderer（libmscore/rendermidi.*）
    -> EventMap / NPlayEvent，按反复展开的 utick 分块
    -> Seq::collectEvents / ensureBufferAsync（缓存与异步渲染）
    -> Seq::process（音频回调）/ playEvent / 消息队列
    -> MasterSynthesizer::play/process + Fluid/Zerberus + effects
    -> Driver / PortAudio 等设备
```

[Seq](../../mscore/seq.h) 是应用音序器，`heartBeatTimeout()` 在界面侧处理播放信息、MIDI 输入和显示同步；`process()` 与 GUI 通信有消息/锁/缓存边界。不要在音频回调新增阻塞 UI、文件 I/O 或全谱扫描，跟随既有线程隔离方式。

实时 MIDI 输入大致为 Driver/MIDI FIFO → `Seq::midiInputReady/heartBeatTimeout` → `MuseScore::midiNoteReceived` → `ScoreView::midiNoteReceived` → Score 的输入队列/处理。虚拟钢琴键盘也通过信号接入应用，不能只修设备驱动。

[MidiRenderer](../../libmscore/rendermidi.h) 的 Chunk 关联 tick/utick；`renderSpanners()` 处理踏板等跨时解释。`Score::renderMidi()` 为整谱/MIDI 导出提供入口。音频导出入口 [exportaudio.cpp](../../mscore/exportaudio.cpp) 另组织离线合成，新增播放效果须按需求核对是否同时覆盖导出。

## 插件、设置与工作区

[PluginAPI](../../mscore/plugin/api/qmlpluginapi.h) 注册 QML 可见属性/对象，[cursor.*](../../mscore/plugin/api/cursor.cpp) 提供遍历/输入；[scoreelement.cpp](../../mscore/plugin/api/scoreelement.cpp) 按 PLUGIN/SCORE ownership 选择直接构造属性还是 undo。插件 `startCmd/endCmd` 与 `scoreStateChanged` 需要防止回调递归。

偏好键在 [preferencekeys.h](../../global/settings/types/preferencekeys.h)，注册/default 在 [preferences.cpp](../../mscore/preferences.cpp)，控件在偏好对话框；主窗口 `preferencesChanged` 分发部分 UI 更新。工作区同时涉及 `mscore/workspace.*`、`mscore/palette/`、[share/workspaces](../../share/workspaces/CMakeLists.txt)。保存于谱面、个人用户设置、工作区的状态应先明确归属再增加字段。
