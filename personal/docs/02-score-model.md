# 02 乐谱数据模型与修改不变量

阅读目的：理解“改一个音符”实际涉及哪些对象、时间/轨道/链接状态。以下依据当前源码，不把对象关系当成通用 MIDI 编辑器模型。

## 两套互相引用的结构

```text
音乐结构：MasterScore/Score -> MeasureBaseList -> Measure -> Segment
                                              -> track 对应的 Chord/Rest
                                                          -> Note 列表
乐器结构：Score -> Part -> Staff；每个 Staff 固定 4 个 Voice
排版结果：Page -> System -> Measure 的位置；元素有 bbox/shape/pos
跨时间线：Spanner（Slur/Hairpin/Pedal/Ottava...）-> 各 System 的 SpannerSegment
```

这不是每个对象都只由图中箭头唯一拥有的树；`parent`、引用容器、链接对象、undo 命令、布局缓存均影响生命周期。复用既有 add/remove/undo API，不按图手工释放对象。

| 对象 | 意义与关键入口 |
| --- | --- |
| [ScoreElement](../../libmscore/scoreElement.h) | 通用属性、样式和 linked element 基类；Part/Staff 等也继承它 |
| [Element](../../libmscore/element.h) | 可绘制/可选元素基础，track、parent、位置、bbox、属性、读写、布局 |
| [Score / MasterScore](../../libmscore/score.h) | 乐谱/总谱、选择/输入/样式/编辑接口；MasterScore 协调分谱、反复、播放及共享状态 |
| [MeasureBase / Measure](../../libmscore/measurebase.h) | MeasureBase 链还包括框架；Measure 包含时间段、各谱表信息、拍号与多小节休止等 |
| [Segment](../../libmscore/segment.h) | 同一时间位置的一类内容，按 SegmentType 区分；不是一小节，也不是音符时值 |
| [DurationElement](../../libmscore/duration.h) | 时值/连音符上下文；`ticks()` 与 `actualTicks()` 有不同含义 |
| [ChordRest](../../libmscore/chordrest.h) | 和弦/休止共享节奏、beam、staffMove 等状态 |
| [Chord](../../libmscore/chord.h) / [Note](../../libmscore/note.h) | Chord 放多个 Note 和符干/装饰等；同时开始且共享时值的一组音是一个 Chord |
| [Part](../../libmscore/part.h) / [Staff](../../libmscore/staff.h) | Part 管理乐器及谱表；Staff 管理谱表类型、随时间变化的谱号/调号等 |
| [Selection](../../libmscore/select.h) / [InputState](../../libmscore/input.h) | 谱面选择与输入上下文；界面还另有 ViewState |
| [Spanner](../../libmscore/spanner.h) | 带起止锚点/时间/track 的跨时元素；视觉分段是另一层对象 |

## Track、声部与跨谱表

固定关系在 `element.h`、`mscore.h`：`VOICES = 4`，`track = staffIdx * VOICES + voice`。内部分声部 0–3，界面通常显示 1–4。`staffIdx()` 是逻辑谱表；`ChordRest::vStaffIdx() = staffIdx() + staffMove()` 是跨谱表显示谱表。

例如钢琴上谱表的第二声部：staffIdx=0、voice=1、track=1；显示到下谱表时可以 staffMove=+1，但 track 仍然是 1。这与把音乐真正移动到下谱表的 track=5 不同。跨谱表显示、声部交换、左右手拆分是三种操作，不要混用。

谱表索引可能在插入/删除/分谱映射后变化；不能以 UI 行号长期保存模型引用。分谱与总谱 track 映射需查看 [excerpt.*](../../libmscore/excerpt.cpp) 与 [elementmap.*](../../libmscore/elementmap.h)。

## 时间、时值、速度

| 名称 | 含义 | 修改时注意 |
| --- | --- | --- |
| `Fraction` | 精确有理数，时值以全音符为单位 | 四分音符是 `Fraction(1,4)`，不是 `Fraction(480,1)` |
| `DIVISION` | 基准中为 480，四分音符的整型 tick 数 | 使用常量和转换接口，不散布硬编码 |
| `Fraction::fromTicks()` / `.ticks()` | 整型 tick 与分数间转换 | `.ticks()` 有舍入，不适合反复来回转换连音符 |
| `Segment::tick()` / `rtick()` | 全谱时间 / 相对当前小节时间 | 两者不能互换，且同 tick 可有不同 SegmentType |
| `ChordRest::ticks()` | 模型存储时值 | 连音符等情况下与实际占据谱面时间不同 |
| `DurationElement::actualTicks()` | 考虑连音符等后的实际时长 | 选择跨时区间与结束位置应核对既有调用方式 |
| `tick` / `utick` | 谱面时间 / 展开反复后的播放时间 | 用 RepeatList 映射，不以直接加减解决反复跳转 |
| `utime` / frame | 播放秒 / 音频采样帧 | 受 TempoMap 与 sampleRate 影响，与 UI 像素不同 |

[fraction.h](../../libmscore/fraction.h)、[duration.*](../../libmscore/duration.cpp)、[repeatlist.*](../../libmscore/repeatlist.cpp)、[tempo.*](../../libmscore/tempo.cpp) 是这些转换的依据。节拍/拍号看 `sig.*`、`timesig.*`；输入的 `TDuration` 表达记谱类型与附点，不能仅靠整数 tick 恢复原本的记谱意图。

## 音高与播放事件

`Note::pitch()` 是音高；`tpc1` 表示非移调拼写、`tpc2` 表示移调拼写，TPC 区分相同 MIDI 音高的升降记法。`ppitch()` 是用于播放的音高，考虑八度线等因素。修改半音值后必须检查拼写、调号、临时记号、移调乐器、八度线和链接谱，优先沿既有 transpose/undoChangePitch 路径。

`NoteEvent` 是 Note 的播放微调事件，见 [noteevent.h](../../libmscore/noteevent.h)：

- pitch 是相对 Note 的偏移；不是绝对 MIDI 音高。
- ontime 和 len 的单位是名义音符时长的千分之一，默认 0/1000；不是毫秒，也不是全谱 tick。
- 一个 Note 可以有多个事件，例如装饰音/演奏解释；力度调整主要在 Note 的 velocity/type 属性，不在 NoteEvent 的三个字段中。
- `Chord::playEventType()` 的 Auto/User 区分是否自动生成事件。`Score::createPlayEvents()` 只在 Auto 时替换事件列表，用户微调必须保持 User 语义。

改谱面时值会改变播放事件相对单位的实际长度；改 NoteEvent 通常不应改变印刷音符。参见 [钢琴专题](05-piano-development.md)。

## 属性与样式不是普通 setter

[property.h / property.cpp](../../libmscore/property.cpp) 定义 `Pid`、类型、XML 名称和链接规则；[style.h / style.cpp](../../libmscore/style.cpp) 定义 `Sid` 与乐谱级默认样式。元素的 ElementStyle 映射将两者关联。

`PropertyFlags::STYLED` 表示跟随样式，`UNSTYLED` 表示局部覆盖，`NOSTYLE` 表示无样式映射。检查器改属性与“设为默认样式”不是同一个流程：后者还会修改乐谱样式与元素 flags。新增属性通常需核对：Pid 表、get/set/default、style 映射、read/write、链接策略、undo、检查器与插件包装；只加成员变量会漏掉持久化或撤销。

`ScoreElement::undoChangeProperty()` 使用 `propertyLink(pid)` 决定是否遍历 linkList，并以 ChangeProperty 实现。直接 `setProperty()`/`setPitch()` 可能只改当前对象；它们适用于读取/构造或已有命令内部的低层过程，不能作为用户动作的默认入口。

## 撤销、链接与生命周期

用户动作需要明确的事务所有者。`Score::startCmd()` 开启 UndoMacro，调用 `score->undo(new ...)` 或 `undoAddElement/undoRemoveElement/undoChangeProperty`，最后 `Score::endCmd()` 执行布局、dirty/playlist 状态与 UI 回调。主窗口的 QAction 路径可能已经包好了事务；不要在里面无条件再嵌套 startCmd。

`UndoStack::push()` 在没有活动事务时仍会 redo 并删除命令，因此“功能看起来生效”不证明能撤销。添加/移除对象还涉及 links、spanner、beam、tie、tuplet、selection、输入位置与 postponed deletion。原始指针不可跨重建视图/删除操作任意长期缓存。

总谱与分谱通过 linked element 和共享撤销栈联动，`Score::undoStack()` 委托给 MasterScore。测试普通钢琴谱通过后，还应按需求检查生成分谱、链接谱表、undo/redo、保存重开。

## 常见错误定位

| 现象 | 首先检查 |
| --- | --- |
| 改动生效但 Ctrl+Z 无效 | 事务是否活动、是否直接 setter、使用 push 还是 push1 |
| 五线谱变了卷帘没变/反之 | 是否改同一 Score/Note、是否触发命令结束与视图重建 |
| 保存后恢复旧值 | 属性 read/write、默认值省略、User 播放事件状态 |
| 跨谱表后声部/播放不对 | track 与 staffMove 被混淆、逻辑 Staff 与显示 Staff |
| 三连音/跨小节变形 | ticks/actualTicks、tuplet 上下文、休止填充和 tie 链 |
| 主谱正常分谱出错 | propertyLink、linkList、excerpt track 映射、共享 undo |
| 连续编辑后闪退 | 元素删除、PianoItem 重建、NoteEvent 列表替换导致指针失效 |
