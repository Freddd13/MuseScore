# 15 钢琴演奏与记谱扩展

## 状态和边界

0.14.0 为琶音对拍及通用参数交互，0.14.1 修复延音续接；0.15.0 增加快速小音符播放解释。0.17.0 增加可调轻重音；0.18.0 实现 rit./a tempo；0.19.0 实现震音／颤音力度包络；自由圆滑线和分手折线仍是后续批次；不能把计划当作已实现功能。

旧谱缺少个人属性时沿用上游行为。新增交互入口写入显式属性，复制与导入不批量改谱。个人属性属于原生 MSCX/MSCZ；其他版本重新保存可能删除扩展字段。

## 琶音数据和播放

- `Arpeggio::_timingMode` / `Pid::ARP_TIMING_MODE`：0 原 stretch，1 首音对拍，2 末音对拍。构造/读取缺字段为 0；调色板与 Chord::drop 为新增交互设置 2。
- `ARP_INTERVAL_MS`：1–1000 ms，默认 65；`ARP_OFFSET_MS`：−1000–1000 ms，默认零。参数有限值检查，避免 NaN/Inf 进入速度图。
- `PlaybackTiming::arpeggio` 找同声部跨谱表的所有者。`arpeggioNotes` 对实际发声音排序，排除静音/隐藏/续接延音、自定义事件、双音震音和带独立琶音的下方和弦。span 不跨乐器。
- `intervalMs` 以实际速度图求各和弦记谱时值，总展开不超过最短者一半。renderArpeggio 将毫秒转为原 NoteEvent 的千分比，保留原结束点和后续延音；精度为原 MIDI tick / 千分比粒度。
- MidiRenderer::canBreakChunk 只在提前音实际越过下小节开头时合并相邻渲染块，避免插入重复的 lookahead 事件。

## 起播与导出

`NPlayEvent::nominalTick` 是每个 Note-on/off 的原和弦展开后 tick，用于区分重复段的不同出现；不是新谱面字段。生成于 playNote 后保留到 Note-off。

GUI collectEvents/seekCommon 计算目标和弦必要的最早事件；Seq::setPos 仅使用准备好的结果。预备期间过滤无关前文，光标停在目标拍点。负时间用初始速度换算，不交给 RepeatList 的非负查找。循环检查跳过预备阶段，停止/跳转重新定位。

音频导出统一加最短预备秒数并报告首拍偏移，初始化控制器先应用。MIDI 导出把音符与后续速度/节拍一起偏移，初始化事件留在零点，写入 `Kumo: score first beat` marker。原谱不插入小节。

## 检视器和撤销

`mscore/inspector/scrubproperty.h` 为新增数字框配名称拖拽。拖动只预览数字，释放触发一次标准检视器属性事务；Shift 精调，Esc/隐藏取消，键盘仍可输入。旧参数控件不全局替换。

InspectorBase 只对追加的个人 Pid 采用播放中暂存。PerformanceEditor::_propertyPending 按对象/属性合并，停播且后台渲染空闲后用原生 undoChangeProperty 提交；沿用保存/换谱 flushForScore。销毁通知、外部操作和取消预览清理临时指针。未打开编辑器时按需创建隐藏 dock，不强制显示。

## 验证入口

- `mtest/libmscore/midi/tst_midi.cpp`：timedArpeggio、timedArpeggioSpanAndCap；邻近旧 MIDI 全套回归。
- `mtest/mscore/performanceeditor/tst_performanceeditor.cpp`：arpeggioInspectorScrubAndDeferredCommit、arpeggioRealSequencerPreRoll；现有 GUI/播放/力度/保存回归。
- 测试使用隔离配置、无声合成器和真实 Seq，不连接声卡；不能代替真实音源的听音及设备性能验收。实际结果见个人 CHANGELOG 对应条目。

专业依据：[Dorico 琶音开始/结束对拍](https://archive.steinberg.help/dorico_se/v5/en/Dorico_SE_5_Operation_Manual_en.pdf)。默认 65 ms 为本分支预设，可修改，不是普遍记谱规定。

## 0.14.1 延音续接

排除排序不等于清空续接音的所有事件：collectNote 通过续接音单个 NoteEvent 的 len 累加延音总时长。新琶音为 tieBack 保留 `NoteEvent(0, 0, 1000)` 的时长体；collectNote 原有 tieBack 规则阻止独立 Note-on。`timedArpeggioTies` 检查两个相邻四分和弦低音连接时结束 tick 959（修复前 479）、无续接重触发且 Tie 不变。

## 0.15.0 小音符外观与播放

参数存于主 Chord，整组共享：`GRACE_PLAY_MODE` 为 0 原解释、1 拍前、2 拍上、3 拍后；`GRACE_DURATION_MODE` 为 0 每个小和弦毫秒、1 整组占主音比例；`GRACE_DURATION` 有限范围 1–1000，默认 65。缺字段仍是 0，不改变旧谱或原长倚音入口。小音符 Chord 的 `propertyDelegate` 把时序属性交给主 Chord；原 MSCX 倚音／时值／附点标签仍负责外观，`GRACE_APPEARANCE` 编码 NoteType、DurationType 和 dots，作为检视器一次原生撤销的属性，避免第二份外观数据。

`fast-grace` QAction / 倚音调色板预设调用原 `Score::setGraceNote`，创建无斜线八分外观，写入 1 / 0 / 65。Note Inspector 可单独选外观、位置、单位和持续时间，数字与名称拖拽共用原属性事务。应用预设通过现有 queueProperty 批量提交一次 Undo，播放期间暂存至安全边界。

`PlaybackTiming::graceNotes` 汇合前后小音符的原顺序，只选择自动且可发声的组；`graceSpanMs` 通过 TempoMap 求主音的实际记谱时间，整组最多取一半；拍前再限制到前一同声部音／休止起奏间隔的一半，保护曲首短前音。`graceStartTick` 使用与第一生成事件相同的 tick／千分比量化。`createGraceNotesPlayEvents` 在旧融合颤音判断后计算新事件，不重复播放已融合的小音符。拍前事件为负千分比，主音仍为零；拍上延迟主音；拍后在主音末端留出空间。固定毫秒受既有 MIDI tick / 千分比量化，实际间隔误差随主音时值变化，长音可能有数毫秒；首版保持既有事件表示。

`collectNote` 仅缩短同声部紧邻的自动前音（沿延音链寻找其最后片段）；不修改乐谱时值、NoteEvent 保存值、其他声部或用户事件；延音链任一片段有用户事件时不裁剪。拍上／拍后把自动生成的震音／滑音事件放入主音窗口，外部延音保留结束点。`canBreakChunk` 复用琶音的提前窗口合块规则。起播、循环、MIDI 和音频统一走 0.14 的预备时间助手；没有给谱面插入时值或小节。

模型入口为 `timedGracePositions`、`timedGracePrecedingAndCustom`、`timedGracePersistence`、`timedGraceRepeatJump`、`timedGraceMergedTrill`、`timedGraceGeneratedEvents`、`timedGraceTiedContinuations`、`timedGraceShortPredecessor`；真实 Qt 入口单独放在 `grace_playback_tests.inc`，测试名称拖拽、Shift、Esc、播放暂存、原生预设、单次撤销和真实 Seq 的曲首／中途拍前发声。结果以更新日志实际记录为准。

## 0.17.0 重音倍率

`Articulation::velocityMultiplier` 为渲染和 NoteVelocity 的共同入口。`ARTIC_VELOCITY_MODE` 缺字段 false，取乐器原倍率；true 取 `ARTIC_VELOCITY_PERCENT`（有限 1–400）。两属性 linked，默认参数 115；读取不应用新预设。`Score::addArticulation` 与内置 Articulations 调色板在新建时对普通 > 组合调用 `applyLightAccentPreset`，不重写全局乐器文件或通用 drop/import。

InspectorArticulation 复用 scrubproperty 和 queueProperty；原生输入一次事务，播放暂存安全提交，取消自定义恢复原乐器，显式按钮应用 115%。普通重音在音符绝对力度前计算；NoteVelocity 的基准与 collectNote 一样先限幅，避免 400% 后减半时显示与发声不一致。后续重复事件包络尚未实现。

模型测试 `accent_playback_tests.inc` 检查旧/新倍率、相对/绝对、限幅、关闭发声、组合/^、原生命令、克隆/Undo、关联分谱和 MSCX/MSCZ。真实 Qt `accent_inspector_tests.inc` 检查数字、名称拖动/Shift/Esc、暂存、预设、内置调色板及 MIME 保留。实际结果见版本日志。

## 0.18.0 派生速度表达

TextLine 的 RIT_MODE 明确类型，RIT_PLAY 控制发声，均缺字段 false。参数 linked；targetMode=0 百分比/1 BPM，target默认80，curve .1–8，startBpm=0 随前/5–999显式。TempoText 的 TEMPO_RESTORE_MODE=0 旧文字、1最近渐变前、2曲首、3指定，不从显示字符串推断。SLine普通布局不动；仅已配置 rit. 扩展 Spanner/Scorefile 的既有精确踏板端点，TextLine::linePos 按原生时间在小节CR横向坐标插值。

tempoexpression.cpp 在 fixTicks 和完整/局部布局结束批量重建派生 TempoMap；_tempoExpressionsPresent 是旧谱快速旁路缓存，不是存储字段。边界事件顺序为渐变结束→原速度文字→渐变开始；恢复保存最近渐变开始前速度，指定起速不覆盖这份恢复值。冲突曲线跳过派生并在UI说明；手绘不修改关联范围。暂停和原Fermata stretch沿用，relativeTempo在TempoMap积分后统一作用。准备时数值积分求每区间调和速度，单次 normalize，单tick以上区间按前缀误差细化；音频回调继续只查旧速度图。

ParameterEdit::{tempoCurve,restoreTempo,editTempoCurve,detachTempoCurve} 为菜单、检视器及编辑器公共写入入口。先验证范围/值/冲突，再一次原生Undo；显式replace才移除整条重叠曲线与内部标记，边界保留。自由绘制保护关联区，恢复语义标记也按受保护原节点处理。tempocurves.cpp 缓存同一派生图，起终/曲率手柄预览不写谱，实际提交属性；屏幕像素采样限制绘制量，不减少播放节点。数字焦点随参数切换清理，销毁/换谱清理对象指针。

rit_playback_tests.inc 检查积分/前缀、局部重排幂等、精确非CR端点与关播放、暂停/Fermata/相对速度、恢复/指定起速、重复、冲突、克隆、真实分谱、MSCX/MSCZ。rit_editor_tests.inc 检查原生命令、冲突/替换、检视器、手柄预览/Undo/Esc/数字以及真实Seq事件时间/中途起播/循环。实际结果和部署hash见版本日志；后续包络/圆滑线/分手仍未交付。

## 0.19.0 生成事件力度包络

PlaybackEnvelope 是 Tremolo、Trill 和符合条件的 Articulation 的小型共用值对象。ENVELOPE_MODE=0缺字段旧值，1柔和、2渐强、3渐弱、4自定义；START/END 1–400%，CURVE .1–8，ALTERNATE −100–100百分点，均 linked、有限值校验。新建内置菜单／调色板调用显式preset，不改构造／读取／复制默认；非颤音奏法不显示包络。预设多属性由检视器一次 queueProperty 事务写入，不能在 setProperty 内暗改其余字段以破坏Undo。

NoteEvent 的 velocityFactor 默认1、velocitySourceIndex 默认−1，是准备阶段派生数据，读入重置、不写XML。用户自定义事件在 eventFactor/eventSource 一律忽略这两项。renderTremolo/renderChordArticulation复用旧音高和重复算法，生成事件后按完整范围赋因子；新单音震音贯穿匹配参数的自动延音链，差异／User作为边界。新根在原生段上收集，不再从旧根递归重复收集；新包络跨小节不分渲染块，保证中途定位仍看到既有完整事件。

双音震音仍把双方事件放在第一和弦的事件表，新 sourceIndex 对应第二和弦实际 Note。NPlayEvent 保留实际发声音符，同时 noteEventOwner/index 保留原表身份；基准、奏法、定制力度和播放开关来自真实源，nominalTick仍按原表定位。NoteVelocity::eventVelocity 统一对 customizeVelocity 后的值乘因子、四舍五入、限幅1–127。旧mode0事件因子1、源−1，保持上游输出；全局乐器定义和音符保存力度不改。

envelopepreview.cpp 仅在停播快照时采样每次发声基准及因子、过滤双音各自源，并合并相邻相同项。generatedVelocityRange 从缓存和当前预览属性计算实际范围；绘制／试听／提示不重新生成事件，音频回调不计算曲线或分配包络数据。保存值轴继续用定制前事件因子；范围旁标区分实际发声。不增加逐次事件绘制编辑。

envelope_playback_tests.inc 覆盖旧值、相对／绝对、柔和／渐强／渐弱／曲率、双音独立定制／不发声／不同音数、完整延音／差异和自定义边界、非法值、克隆／Undo／实际分谱、MSCX／MSCZ与分块、128小节8192攻击。envelope_editor_tests.inc 用真实Qt检视器验证预设单Undo、数字、Shift拖动、Esc、播放暂存、单震音／颤音段／短颤音、缓存范围以及真实Seq逐次力度／中途起播／循环。实际最终结果见版本日志。
