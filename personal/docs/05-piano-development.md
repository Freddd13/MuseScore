# 05 钢琴编曲开发专题

本章重点覆盖当前分支已有能力与适合个人需求的扩展位置。它是开发地图，不是新功能承诺或已经运行验证的用户手册。

## 当前基准已有的相关演进

本地历史和源码可确认：

- `575fc7179`（2026-09-17）：卷帘大幅升级，提交主题含 Dockable、Vertical Scroll、Multi-Staff Editing；相关代码已在本基准。
- `f1d76dc6f`（2026-09-17）：Workspace migration (version 3) / Piano Roll Editor。
- `238ae8380`（2026-09-23）：播放时在卷帘/键盘/ScoreView 省略选择绘制。
- `b22ce4b77`、`786dfe6bf`、`53ad8c4ae`（2026-09-28）：跨谱表 beam 上/下/自动属性、翻转循环、全小节休止居中处理。

这里只描述本地已有提交，不判断哪些来自用户、上游或其他贡献者。实现需求前要先核对已有 UI/代码能否满足，避免重复重写。

## 卷帘模块分工

| 文件/类 | 职责 | 修改入口 |
| --- | --- | --- |
| [pianoroll.* / PianorollEditor](../../mscore/pianoroll/pianoroll.h) | QWidget + MuseScoreView，工具栏、当前谱表、范围、方向、设置、更新通知、播放跟随 | `setStaff/setScope/setOrientation/updateAll/writeSettings/readSettings` |
| [pianoview.* / PianoView](../../mscore/pianoroll/pianoview.h) | QGraphicsView，音符块绘制、时间/音高坐标、选择、添加、删除、拖动、切分、tie、事件调整 | `updateNotes/addNote/deleteSelectedNotes/finishNoteGroupDrag/finishNoteEventAdjustDrag` |
| [pianorolledittool.h](../../mscore/pianoroll/pianorolledittool.h) | scope/orientation/tool/coloring 枚举、时间桶查询、共享声明 | 添加交互模式先核对这里和 view 的 DragStyle/CursorMode |
| [pianolevels.*](../../mscore/pianoroll/pianolevels.h) | 力度/起音/时长曲线绘制、命中、批量线性调整 | `adjustLevel/adjustLevelLerp/setStaff`、时间桶索引 |
| [pianolevelsfilter.*](../../mscore/pianoroll/pianolevelsfilter.cpp) | 各参数 value/setValue，力度与 NoteEvent 之间的桥接 | 复用 ChangeNoteEvent 与 Note 属性 undo |
| [pianokeyboard.*](../../mscore/pianoroll/pianokeyboard.h) | 卷帘键盘、方向、输入/播放高亮 | 与主窗口 `pianotools.*` 分开 |
| [pianoruler.*](../../mscore/pianoroll/pianoruler.h) | 时间尺、locator、方向与缩放 | 与 view/levels 一起保持坐标一致 |
| [notetweakerdialog.*](../../mscore/pianoroll/notetweakerdialog.h) | NoteEvent 微调对话框 | 列表刷新与事件指针生命周期 |
| [mscore/musescore.cpp](../../mscore/musescore.cpp) | dock 创建/显示、当前谱切换、主窗口选择/设置分发 | 主窗口中只留必要 glue |

`PianoItem` 是持有 Note 指针的视图辅助对象，不是另一套音乐模型。`updateNotes()` 清空 scene 与 note 数据，根据 `pianoRollScopeTracks()` 从 Score 的 ChordRest segments 重建；不能跨此调用缓存旧 PianoItem。

## 显示范围与编辑目标

`PianoRollScope = STAFF / PART / SCORE` 表示可显示/遍历范围。钢琴一个 Part 通常包含两个 Staff；`_staff`/可编辑谱表仍是输入的目标上下文。画面展示多个谱表不能推出添加音符应同时添加到所有谱表。

新增“左右手联动选择”“多谱表批处理”等功能前，明确：作用于已选择 Note，作用于 scope 内所有 Note，还是作用于当前 editable Staff。多选可跨谱表，track 必须来自具体目标/现有 Note，不能全部重写成当前谱表 voice。

`setStaff()` 按 scope 判断是否需要重新定位视口；`setEditableStaff()` 与范围重建不同。保留这种区分可避免每次切编辑目标都让整个卷帘跳动。

## 谱面音符编辑与演奏微调

| 需求 | 主要层 | 要守住的语义 |
| --- | --- | --- |
| 移动音高/谱面起点、增加/删除音符 | PianoView + 核心记谱 API | 和弦、休止填充、声部、拼写、tuplet/tie、Undo |
| 修改印刷时值、切分跨小节音符 | PianoView + `setNoteRest/changeCRlen` 等 | 不能只改 block 宽度或一个 duration 成员 |
| 稍早/稍晚发声、短奏/延长播放 | NoteEvent + ChangeNoteEvent | 1000 比例单位、User 状态、谱面不变 |
| 固定绝对力度或随力度标记偏移 | Note velocity/type + level filter | 绝对值与偏移不是同一域，不改变 dynamic 文本 |
| 渐强/人性化/琶音演奏规则 | 先考虑插件/批处理；必要时核心 MIDI renderer | 可撤销、可重复生成、保留用户微调、导出一致 |
| 新的力度/时间参数曲线显示 | PianoLevels/Filter/Chooser | 曲线 UI 与数据语义分开，不把像素存到模型 |

现有 `PianoView::addNote()` 会考虑起点是否在既有 ChordRest 内、是否要保护已有节奏边界，再复用核心实现。移动/拉长/切分时先读这个策略及其调用者，不能以“删掉原和弦重新建一个”替代。

事件改动通常借 `ChangeNoteEvent`；切换到用户演奏事件还要核对 `ChangeChordPlayEventType` 等既有路径。`Score::createPlayEvents()` 对 Auto 自动生成、对 User 保留，所以 User 状态遗漏会在后续渲染中覆盖微调。

## 方向、缩放、选择与性能

横向/纵向模式不只是交换两条滚动条；时间轴、音高轴、键盘排列、ruler、levels、locator、命中测试、拖动起止点与播放跟随均需一致。纵向另有 chromatic/keyboard-aligned pitch layout；不要使用固定“X 等于时间、Y 等于 pitch”的假设。

视图与 levels 使用 time buckets 减少范围查询；helper 对负 tick 使用向下取整。添加新的视图缓存时必须注明何时重建/失效，移动事件后保守覆盖 Note 本身和所有 NoteEvent 时间范围，避免可见音符无法点中。

拖动有 preview 与提交、复合 undo 分组。无用户动作时不要在 paintEvent 中改谱或重建全谱；一次拖动尽量保持一次撤销。`NoteEvent*` 指向列表内部元素，列表替换/删除后需重找。

选择状态和播放高亮当前有所区分；同一 MIDI 音高可能对应不同声部或重复 Note-on。改键盘状态要检查重叠释放、停止/跳转清音、播放时是否覆盖编辑选择显示，而非简单单个布尔数组。

## 跨谱表、左右手与钢琴记谱

三种需求分别定位：

1. **临时显示到另一谱表**：`ChordRest::staffMove/vStaffIdx`、`ChangeChordStaffMove`、beam/chord 布局。
2. **真正改归属/声部**：track、休止与节奏结构、链接/分谱；参考 `cmdExchangeVoice` 等已有命令。
3. **拆左右手**：已有谱的 `Score::splitStaff(staffIdx, splitPoint)`（`libmscore/score.cpp`）与 MIDI import 的 `LRHand::splitIntoLeftRightHands()` 是两套入口，后者还在量化转换流水线中。

跨谱表 beam 的方向属性在 `beam.cpp` 的 get/set/default/read/write 与 `edit.cpp` 的 cmdFlip，以及 `InspectorBeam` 中均有关联。当前也读写 `crossStaffMove` 以处理对应互通语义；改动必须回归上/自动/下、休止、跨谱表、旧谱/保存重开和视觉位置。

## 踏板、八度线、速度与节拍器

踏板有两层：`Pedal/Spanner` 的印刷符号与起止锚点；`MidiRenderer::renderSpanners` 的 sustain controller 事件。长踏板在反复/跳转/停止后是否清音须检查 `seq.cpp` 的 controller 释放逻辑。若需求是半踏板/连续 CC 曲线，先验证现有数据与 synth 支持，不能只修改踏板线外观。

八度线主要影响 `ppitch()`；UI Note pitch、TPC 和发声 pitch 要区分。速度来自 TempoMap/tempo text；反复后 locator 应处理 tick/utick。基准 `Seq` 已含独立节拍器的 GUI/实时状态字段与计算，增加练习节拍功能前读 `seq.*`、`playpanel.*`，不要另建竞争的音频计时器。

## 钢琴需求的最小回归谱例

根据实际改动挑选必要场景：普通双谱表/和弦 → 2–4 声部 →跨谱表 beam → 附点/连音符/跨小节 tie → 八度线/踏板/反复 → 多选/多谱表 → undo/redo → 保存重开 → 实时播放/MIDI 导出。卷帘外观改动另测横/纵、缩放、停靠/浮动、空谱和切换文件。

基准没有默认注册的专门 `mtest/mscore/pianoroll` suite。可复用 `beam/splitstaff/links/parts/note/midi` 等邻近核心测试；GUI 场景需实际操作或新增有意义的集成测试。不得把邻近测试通过写成卷帘新行为全部已验证。

## 和弦观察与实时配色

个人 0.2.0 的 ScoreObserver 提供按声部持续音和 Seq activeNoteEvents；插件实现和弦识别。播放音高与 writtenPitch/TPC 分开，跨分谱投射，0.3.0 的 contextSnapshot 另提供记谱踏板区间保持音及限小节/休止截断的短时聚合；它模拟踏板状态，不估计声音衰减。即时音列表和推断上下文分开。ScoreView 色层只作用屏幕，不影响撤销/保存/导出；保留编辑选中颜色及播放标记。参考 [10](10-score-observer.md)。

0.4.0 和弦/级数记号固定在乐器顶谱表上方同一行，播放仅高亮；低音锚点双击以 Part::startTrack 聚焦整乐器。小调 V/导音允许升七级的离调强调规则属于插件，不能视为新的声音识别/踏板算法。

0.5.0 释放/踏板导致和弦变化但没有新音时，插件仍生成变化 tick 的标记；来源持续音可以在上一系统，几何取变化所在系统。该变化只影响分析展示，不修改 NoteEvent 或踏板解释。

## 原生演奏编辑器（0.9.0）

便捷改力度/画速度阶梯/踏板改由独立面板和谱面浮动 UI 实现，原卷帘保留。原卷帘和检视器的相对/绝对切换改用真实整数百分比转换，逐音使用自身谱表基准。播放时面板值暂存，停播后提交；详细边界见 [14](14-performance-editor.md)。
