# 15 钢琴演奏与记谱扩展

## 状态和边界

0.14.0 为琶音对拍及通用参数交互。倚音、可调重音、rit./a tempo、震音力度包络、自由圆滑线和分手折线仍是后续批次；不能把计划当作已实现功能。

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
