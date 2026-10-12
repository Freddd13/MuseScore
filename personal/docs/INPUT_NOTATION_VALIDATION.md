# 0.36.0 输入净谱检查

日期：2026-10-13。Windows x64，Qt5.15.2，Release；测试使用独立临时配置和测试谱，没有修改OneDrive官谱。

| 检查 | 结果 |
|---|---|
| 输入时值、光标、延音、保存与撤销 | 44通过 |
| 原有符杠分组、方向与修改 | 19通过；预期结果文件未改 |
| 原有MIDI输出、反复、力度和速度 | 79通过 |
| 实际菜单、记忆设置、P0操作和图示 | 14通过 |
| 演奏编辑器 | 45通过；剪贴板子检查本次未运行 |

Windows两次拒绝Qt取得系统剪贴板，普通完整运行均有44通过、1失败，失败仅在“复制版本信息”。之后使用测试已有的PERFORMANCE_NO_SYSTEM_CLIPBOARD选项完成其余操作；没有修改复制功能或绕过系统锁。关于窗口、版本文字、作者标识、链接和工具栏动作仍检查通过。剪贴板复制保留为尚未验收。

两个明确的4/4第一拍例子：附点四分＋八分改四分，最终为720＋240＋240 ticks，光标1200，延音音实际持续480；3-3-2为360＋120＋240＋240，第二起音的延音持续360。开启跨拍连符杠后，音符MIDI事件与关闭时一致。手动不连、手动方向及跨谱表不被协调规则覆盖。

协调只处理同小节的紧凑普通音组，最多64个和弦与8个符杠组。多声部、跨谱表、宽音区、连音组、震音及手动设置使用原有处理。它是本分支的净谱选择，不把所有延音同向宣称为通用要求。

依据：[Dorico符干说明](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/notation_reference/notation_reference_stems/notation_reference_stems_direction_c.html)、[音符与休止分组说明](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/notation_reference/notation_reference_note_rest_grouping/notation_reference_note_rest_grouping_c.html)。Behind Bars完整相关正文未取得，已核对范围不扩大声称。

没有新增音频回调工作，没有用这组小谱声称完整排版零开销或所有乐器均已验收。继续沿用现有播放、选择、P键盘和导出路径。
