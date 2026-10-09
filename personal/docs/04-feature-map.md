# 04 功能分布与修改定位表

用法：按需求选一行，先读入口/头文件，再查看实现和现有测试。测试目录表示可复用的邻近覆盖，不保证已覆盖你的新需求；目录中的未注册/跳过项见 [构建验证指南](06-build-and-test.md)。

## 谱面编辑与排版

| 功能/需求 | 主要源码 | 关联源码/数据 | 邻近测试 |
| --- | --- | --- | --- |
| 菜单、快捷键、工具栏 | `mscore/shortcut.*`、`musescore.cpp`、`scoreview.cpp` | `mscore/data/shortcuts*.xml`、`events.cpp`、`keyb.cpp` | 手动状态/焦点验证；部分 scripting |
| 鼠标/键盘输入、实时输入、重新定音 | `mscore/scoreview.cpp`、`events.cpp`；`libmscore/noteentry.cpp`、`input.*`、`cmd.cpp` | `shadownote.*`、`navigate.*`、`seq.cpp` | `mtest/libmscore/note`、`durationtype`、`tuplet` |
| 音符/和弦/休止、时值、临时记号 | `libmscore/note.*`、`chord.*`、`chordrest.*`、`rest.*`、`cmd.cpp` | `duration.*`、`durationtype.*`、`accidental.*`、`pitchspelling.*` | `note`、`durationtype`、`rhythmicGrouping` |
| 音高/调号/移调/等音拼写 | `libmscore/transpose.cpp`、`pitchspelling.cpp`、`key.*`、`keysig.*` | `mscore/transposedialog.*`、`keyedit.*`、`libmscore/note.*` | `transpose`、`keysig`、`concertpitch` |
| 多声部、交换声部、合并/拆散和弦 | `libmscore/edit.cpp`、`cmd.cpp` | `select.*`、`paste.cpp`、`range.*` | `exchangevoices`、`implode_explode`、`copypaste` |
| 钢琴双谱表、跨谱表与 beam | `libmscore/score.cpp` 的 `splitStaff`；`edit.cpp`、`chordrest.*`、`beam.*` | `staff.*`、`chord.cpp`、`layout.cpp`、`inspectorBeam.*` | `splitstaff`、`beam`、`layout_elements`、vtest |
| 小节增删/分割/连接、拍号 | `libmscore/measure.*`、`splitMeasure.cpp`、`joinMeasure.cpp`、`timesig.*` | `mscore/musescore.cpp`、`sig.*` | `measure`、`split`、`join`、`timesig`、`remove` |
| 连音符、连音线/延音线 | `libmscore/tuplet.*`、`slur.*`、`tie.*`、`slurtie.*` | `cmd.cpp`、`mscore/tupletdialog.*` | `tuplet`、`spanners`、`copypaste` |
| 选择、导航、复制粘贴、过滤 | `libmscore/select.*`、`navigate.*`、`paste.cpp`、`range.*` | `mscore/scoreview.cpp`、`dragdrop.cpp`、`dragelement.cpp` | `selectionfilter`、`selectionrangedelete`、`copypaste`、`copypastesymbollist` |
| 撤销/重做与链接对象 | `libmscore/undo.*`、`cmd.cpp`、`scoreElement.*` | `excerpt.*`、`elementmap.*` | `readwriteundoreset`、`links`、`parts` |
| 页面、系统、间距、自动避让 | `libmscore/layout.cpp`、`layoutlinear.cpp`、`system.*`、`page.*`、`skyline.*`、`shape.*` | `style.*`、`mscore/pagesettings.*`、`editstyle.*` | `layout`、`layout_elements`、vtest |
| 谱号、谱表类型、隐藏/缩放/鼓谱/六线谱 | `libmscore/staff.*`、`stafftype.*`、`clef.*`、`drumset.*`、`stringdata.*` | `mscore/editstaff.*`、`editstafftype.*`、`editdrumset.*` | `clef`、`clef_courtesy`、`earlymusic`、`parts` |
| 力度、渐强、踏板、八度线、指法 | `libmscore/dynamic.*`、`hairpin.*`、`pedal.*`、`ottava.*`、`fingering.*` | `spanner.*`、`rendermidi.cpp`、对应 inspector | `dynamic`、`hairpin`、`spanners`、`midi`；踏板需专门谱例 |
| 文本、歌词、和弦符号、低音数字 | `libmscore/textbase.*`、`textedit.*`、`lyrics.*`、`harmony.*`、`figuredbass.*` | `mscore/editlyrics.cpp`、`editharmony.cpp`、`editfiguredbass.cpp`、`share/styles/chords*.xml` | `chordsymbol`、`musicxml/io`；text suite 未默认注册 |
| 反复、跳转、结尾括号、展开反复 | `libmscore/repeatlist.*`、`repeat.*`、`jump.*`、`marker.*`、`volta.*`、`unrollrepeats.cpp` | `rendermidi.*`、`seq.cpp` | `repeat`、`unrollrepeats` |
| 乐器配置、模板、乐器切换、分谱 | `mscore/instrdialog.*`、`instrwidget.*`、`excerptsdialog.*`；`libmscore/part.*`、`instrument.*`、`excerpt.*` | `share/instruments/instruments.xml`、`orders.xml`、`instrtemplate.*`、`instrchange.*` | `parts`、`instrumentchange`、`links` |

表中简写测试目录位于 `mtest/libmscore/`。所有源码路径相对仓库根；用 [source-map.tsv](source-map.tsv) 取得关键符号与基准行号。

## 钢琴、播放与导入导出

| 功能/需求 | 主入口与分工 | 验证建议 |
| --- | --- | --- |
| 卷帘停靠、工具栏、设置 | `mscore/musescore.cpp` 的 dock 创建；`pianoroll/pianoroll.*` 的 `PianorollEditor` | 横/纵方向、停靠/浮动、工作区切换、重开设置 |
| 卷帘音符编辑、范围/声部/多谱表 | `pianoroll/pianoview.*`、`pianorolledittool.h` | 模型、undo、休止、tuplets/ties、编辑目标与显示范围 |
| 卷帘力度/起音/时长 | `pianolevels.*`、`pianolevelsfilter.*`、`notetweakerdialog.*` | Note 属性 vs NoteEvent、Auto/User、保存/导出 |
| 虚拟键盘与按键显示 | `mscore/pianotools.*`（主窗口键盘）；`pianoroll/pianokeyboard.*`（卷帘键盘） | MIDI 输入、选择、播放高亮、重叠同音释放 |
| 音序播放、循环、定位、节拍器 | `mscore/seq.*`、`playpanel.*`、`iplaypanel.h`、`libmscore/rendermidi.*` | 反复 tick/utick、速度变化、UI/音频边界、停止清音 |
| 合成器、音色库、效果器 | `audio/midi/msynthesizer.*`、`fluid/`、`zerberus/`、`effects/` | SF2/SF3/SFZ、mixer 通道、设备与离线导出 |
| 设备/MIDI 输入输出 | `audiodrivers/driver.*`、`pa.*`、`pm.*`、`jackaudio.*`、`alsa.*` 等 | 后端选择、hotplug、输入和输出分别检查 |
| Mixer、声像/音量/静音/独奏 | `mscore/mixer/`；`libmscore/midimapping.cpp`、`instrument.*` | 通道映射、乐器变化、播放状态 |
| MSCX/MSCZ、自动保存 | `mscore/file.cpp`、`musescore.cpp`；`libmscore/scorefile.cpp`、各元素 read/write | `readwriteundoreset`、compat114/206、保存重开 |
| MusicXML/MXL | `importexport/musicxml/importmxml.cpp`、`importmxmlpass1/2.*`、`exportxml.cpp` | `mtest/musicxml/io`；当前 vs 旧 parser 路径 |
| MIDI 文件转记谱/左右手拆分/量化 | `importexport/midiimport/importmidi.cpp`、`importmidi_lrhand/quant/voice/tuplet*.cpp` | `mtest/importmidi`；导入面板 operation；不是实时输入 |
| MIDI 导出 | `audio/exports/exportmidi.*`，分发在 `mscore/file.cpp` | `mtest/libmscore/midi`；事件内容/反复/力度 |
| 音频/MP3 导出 | `mscore/exportaudio.cpp`、`audio/exports/exportmp3.*`、`audiofile/` | 对比实时播放和离线合成；构建能力开关 |
| PDF/PNG/SVG/打印 | `mscore/file.cpp` 的 `savePdf/savePng/saveSvg`、`scoreview.cpp` | 页数、裁剪、缩放、透明/背景和字体；vtest |
| Guitar Pro/PTB、Capella、OVE、BWW、MuseData、BIAB | `importexport/` 中对应目录和 `.cmake` | `mtest/guitarpro`、`capella`、`biab`；OVE suite 默认注释 |
| PDF/OMR、AVSOMR | `omr/`、`avsomr/`，编译开关控制 | 先确认是否启用，不按目录存在推断可用 |

## 界面、扩展与资源

| 功能 | 主入口 | 易漏关联 |
| --- | --- | --- |
| 偏好与个人默认 | `global/settings/types/preferencekeys.h`、`mscore/preferences.*`、`prefsdialog.*` | default 注册、读取、UI、preferencesChanged、工作区保存 |
| Inspector | `mscore/inspector/inspectorBase.*`、`inspector.*`、各元素 panel | `Pid`、样式 flags、多选、reset |
| 调色板/主调色板 | `mscore/palette/`、`palette.cpp`、`masterpalette.cpp` | QML 资源、PaletteTree、workspace；`mtest/mscore/palette` |
| 工作区 | `mscore/workspace.*`、`share/workspaces/` | palette、快捷键/工具栏和卷帘状态；`mtest/mscore/workspaces` |
| 时间线、导航、对照/比较 | `mscore/timeline.*`、`navigator.*`、`scorecmp/`、`libmscore/scorediff.*` | selection 和 CmdState 更新 |
| 插件/插件编辑器 | `mscore/plugin/`、`plugin/api/`、`share/plugins/` | QML 包装对象 ownership、命令结束回调、API 文档 |
| 字体/符号/样式 | `libmscore/textbase.*`、`sym.*`、`style.*`、`mscore/musescorefonts*.qrc`、`share/styles/`、`fonts/` | 普通文本的 Qt 缺字合并与 ScoreText 的 Bravura Text 回退分开诊断；见 [13](13-rhythm-rules-and-font-fallback.md) |
| 3.6 样式迁移 | `mscore/migration/` | reader 的格式兼容与 UI 字体/位置迁移不是同一件事 |
| 翻译 | `tr/QT_TRANSLATE_NOOP`、`share/locale/`、顶层 lupdate/lrelease 目标 | `doc/i18n.md`；不要批量改全部 .ts 来实现一个功能 |
| 调试/脚本辅助 | `mscore/debugger/`、`mscore/script/`、`mtest/testscript/` | 脚本运行测试当前有 QSKIP，不能报告为有效覆盖 |
| 个人 Windows 构建复现 | `personal/tools/build_windows.ps1` | Qt 5 本地 SDK、Build Tools 识别、VS2019/v142；[本机实测](09-windows-build-check.md) |

## 常用定位命令

大型 SF2 的文件长度/边界、加载预热、取消/错误反馈和真实音源回归见 [11](11-large-sf2.md)，入口为 `SFont::load/Sample::load/Fluid::sfload`；没有新增 Driver、Seq 或 Voice DSP 行为。

```powershell
rg -n 'id-of-existing-command' mscore/shortcut.cpp mscore/scoreview.cpp libmscore/cmd.cpp
rg -n 'Pid::STEM_DIRECTION|crossStaffMove' libmscore mscore/inspector
rg -n 'splitIntoLeftRightHands|convertMidi' importexport/midiimport
rg --files mtest/libmscore/beam mtest/libmscore/splitstaff
rg -n 'QSKIP|add_test|subdirs|MTEST_LINK_MSCOREAPP' mtest
```

修改入口有多种候选时，以“最小改变能负责正确语义的层”为选择依据：只换显示不动模型，只改记谱规则不动音频 Driver；新增数据先确认所有保存/撤销消费者。详见 [开发流程](07-development-workflow.md)。

## 和声辅助入口

当前和弦、罗马级数、功能音和键盘在 `share/plugins/HarmonyAssistant/`；真实播放观察在 `mscore/plugin/api/scoreobserver.*`；屏幕配色在 `mscore/notepreview.h` 与 ScoreView，颜色不写模型。踏板保持与有限琶音窗口、全谱配色/文字、JSON/CSV 分析交换和配置在插件及通用 API；QmlPlugin 停靠桥接暴露实际位置/悬浮状态。验证为 `mtest/mscore/scoreobserver`、实际 Widgets/QML 宿主 `mtest/mscore/pluginhost` 和相邻 `mtest/libmscore/note`。接口与合并点见 [10](10-score-observer.md)。

0.4.0 和声显示入口：share/plugins/HarmonyAssistant/AppearanceEditor.qml 配置位置/顺序/颜色/字体，ColorOption.qml 选色，StableLabel.qml 固定行高与全文提示。原生固定记号、高亮和双击见 scoreobserver.*、notepreview.h、ScoreView::paint / activateNotePreview、events.cpp；真实交互测试为 tst_pluginhost。

0.5.0 新入口：AppearanceEditor 配置原谱优先、背景遮罩、顶部/右侧共存；applyPreview / previewTickX 实现和声变化 x 锚点；QmlPlugin::showDetailPanel 接受一个已有控件树。编辑遮挡接线见 ScoreView::previewEditBounds / editelement.cpp，回归为 tst_pluginhost。

自动规范输入时值（个人 0.7.0）：[inputrhythm.cpp](../../libmscore/inputrhythm.cpp)、`noteentry.cpp` / `cmd.cpp` / `score.cpp` 和 `pianoview.cpp` 接线；应用开关在工具菜单，默认开启。规则、复杂对象保护与回归见 [12](12-input-rhythm.md)。


0.8.0 和声区间：share/plugins/HarmonyAssistant/Timeline.js 是纯数值加权识别/人工范围，RangeEditor.qml 为手柄/精确 tick/独立历史，Preferences.js 配置单/多踏板与摘要/详情结果。attackTick 与踏板二分索引、私有 Harmony/QPicture 位于 scoreobserver.cpp；NotePreviewEntry 的字形等价与尺寸位于 notepreview.h；状态颜色优先级入口为 ScoreView::paint，单击接线 events.cpp。新回归在 tst_scoreobserver / tst_pluginhost，不修改音频、原生 P 键盘或选区框。

## 演奏参数（0.9–0.11）

独立演奏编辑器：`mscore/performanceeditor/performanceeditor.*`（快照/参数手势）、`performanceview.*`（共享视窗/区间索引/画布）、`performancepainting.cpp`（绘制）、`performanceinteraction.cpp`（选择/播放/交互）、`performanceoverlay.cpp`（谱面双向提示／浮层滚动重绘）、`performanceselection.*`（原生选择桥）、`performancesettings.*`（外观）、`parameteredit.*`（原生 undo）、`libmscore/notevelocity.*`（百分比/基准）。入口在视图菜单；回归 `mtest/mscore/performanceeditor`。与原卷帘和检视器共享 Note 属性，使用原 TempoText/Pedal；见 [14](14-performance-editor.md)。

0.11：试听、直接定位／原生播放、重叠端点选择与轮换、独立纵向按钮／数值滚动条，操作入口见 [用户功能说明](../USER_GUIDE.md)。


个人 0.12：演奏编辑器单音滚轮细／粗调入口在 `mscore/performanceeditor/performancewheel.cpp`，仍走原生属性及单次撤销；Evolution 顶部其他选项栏共享菜单开关。启动／关于身份由 `personal/branding.cmake` + `branding.h.in` 从 VERSION 生成，保留上游版本。交互见 [演奏编辑器](14-performance-editor.md)，更新要求见 [开发流程](07-development-workflow.md)。


## 0.13.0 界面与谱行交互修复

演奏编辑器新增截图风格力度默认色、完整黑白键条、MIDI 基线固定缩放；谱面开启滚轮模式直接调未选音頭，Shift 粗调，Alt 保留移动。Evolution Other Options 的同一按钮移至最后。

## 钢琴琶音（0.14.0）

Arpeggio 属性/原生读写 → PlaybackTiming 参与音/窗口 → renderArpeggio → Seq 与导出；InspectorArpeggio/scrubproperty → PerformanceEditor 参数暂存。定位与测试见 [15](15-piano-expression.md)。

0.14.1：新琶音保留续接音的时长事件供 MIDI 延音链累加，续接音仍不参与展开、不重触发；见 [钢琴演奏扩展](15-piano-expression.md)。

## 0.15 快速小音符

新增预设 action 为 `fast-grace`，复用 `Score::setGraceNote` 和 APPOGGIATURA 外观。时序模型在 Chord / PlaybackTiming / createGraceNotesPlayEvents；检视器在 InspectorNote；回归在 tst_midi 的 timedGrace 系列及 grace_playback_tests.inc。旧长倚音入口保持，拍前／拍上／拍后与外观独立，操作见用户说明，设计见 [15](15-piano-expression.md)。

## 0.16 谱行带与参数交互

| 需求 | 实现 | 验证 |
| --- | --- | --- |
| 数字刻度／独立缩放／匹配 | performanceoverlay + performancecontrols + 独立 PerformanceViewport | 原生值、播放位置和编辑器纵轴不变 |
| 声部配色／谱表过滤／提示开关 | 原生 selectColor + 缓存 note.enabled + performanceEditor 设置组 | 过滤保留原选择且不误改，真实菜单与重启 |
| 速度点按／上下拖动 | begin/move/finishGesture + 原 TempoText 事务 | 方形节点保护、BPM 和一次撤销 |
| 细分／自由踏板 | 缓存谱表 CR 边界、Pedal 原生 tick、linePos 与已有 location 写入 | 127 顶端、离音符标记、跨小节保存重开和 CC64 |

用户入口见 [说明](../USER_GUIDE.md)，边界见 [演奏编辑器](14-performance-editor.md)。

| 可调轻重音（0.17） | `libmscore/articulation.*`、`notevelocity.cpp`、`rendermidi.cpp` | `cmd.cpp`、`mscore/menus.cpp`、`inspector/inspector.*` | `accent_playback_tests.inc`、`accent_inspector_tests.inc` |

| rit./a tempo（0.18） | `textline.*`、`tempotext.*`、`tempoexpression.*` | `parameteredit.*`、`tempocurves.cpp`、文字线/速度检视器、添加→文本 | `rit_playback_tests.inc`、`rit_editor_tests.inc` |
