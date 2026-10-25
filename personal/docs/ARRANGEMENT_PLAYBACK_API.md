# 实际演奏的只读查询

新增于0.122.0，供编配检查读取正常MIDI渲染结果。入口位于`ScoreObserver`，在GUI线程调用；后台规则只接收返回的数值资料。

## 调用顺序

1. 调用`analysisPlaybackRevision()`取得这次读取的版本。
2. 反复调用`analysisPlaybackPrepare(scoreStart, scoreEnd, revision)`，直到`ready`为true。每次返回`stage`、`progress`、耗时及准备数量；完成后返回`ranges`、`endTick`和`endSeconds`。
3. 从`INT_MIN`开始调用`analysisPlaybackPage(fromTick, firstTrack, endTrack, limit, revision)`，接着用返回的`nextTick`继续，直到`done`为true。

小节范围采用记谱tick，声部范围采用左闭右开的track范围。分页游标是展开反复后的tick。同一tick的事件一起返回；`limit`限制时间点组数，最多64组，每次以约2ms为工作目标。

准备复用原生`MidiRenderer`与正常的演奏映射，分音乐块生成，随后分批处理重触、松键和重复控制器。默认的正常渲染仍使用原来的逐块完成方式。

## 时间与身份

- `utick`为展开反复后的实际事件tick，包含起音偏移及实际踏板位置；`seconds`读取同一份播放时间表。
- `tick`为音符的记谱位置；`id`采用完整音符快照的身份，装饰音使用`tick:track:g序号:音符序号`。
- `logicalId`指向延音链起点；`eventIndex`区分一个记谱音的不同演奏事件。
- `offset`与`occurrence`记录该次演奏对应的反复偏移与遍次。提前起音按所属音符的记谱位置寻找遍次。
- `ranges`逐项给出`scoreStart/scoreEnd`、展开后的`start/end`、秒数、偏移与遍次。规则用这些范围截取本次检查。

## 音符与踏板

音符事件的`kind`为`on/off`，包含实际`pitch`、`velocity`、`channel`、`partId`、谱表、声部与拼写。`discard`保留正常播放器对同音重触和重复松键的处理；`muted`包含音符、谱表声部与音轨的播放开关。

踏板事件的`kind`为`pedal`，`value`为实际CC64值，`generated`说明是否由谱内踏板生成。即使只选钢琴的一张谱表，也会返回这架钢琴相关的踏板事件。

`Playback.js`将实际事件整理为按键和踏板留声区间。同音同刻的加倍保留多个记谱引用；重触和被正常播放器抑制的松键继续按实际规则处理。缺少配对的片段标记为未完整读取，供调用方决定怎样展示。

## 更新与生命周期

版本包含谱会话、内容变化、反复展开、播放方式、相对速度、声部播放开关及音轨静音。乐谱变更、关闭或切换后，重新准备对应资料。每个观察者保存自己的读取状态，关闭所属乐谱时释放。

播放或后台渲染进行中时返回`playing-or-rendering`；版本变化返回`stale`。界面应显示等待或重新读取状态，保留已有结果供查看。

实际检查见[本批记录](ARRANGEMENT_122_CHECKS.md)。后续可弹性与松手导出继续使用这里区分的按键、松键、踏板和记谱位置。
