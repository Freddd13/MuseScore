# 13 节奏规则依据与字体回退诊断

个人版本 0.7.1；调查日期 2026-10-06。本页区分规范依据、已实现行为和仍需实谱/程序对比的结论。

## 节奏：依据与默认取舍

原则是让拍号的节拍层级容易辨认，同时避免把可读的常见切分拆得过碎。一个输入音的起点、音高和总时长不因规范化改变；必要时用同音延音链表达。符杠分组与音符时值拆分相关，但不是同一功能：本功能调用既有时值算法，不另建符杠规则。

- [Dorico 官方 Note and rest grouping](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/notation_reference/notation_reference_note_rest_grouping/notation_reference_note_rest_grouping_c.html)：按拍号/位置整理，允许附点与切分例外；3/4 不能简单套用偶数拍的中线。Dorico 提供可选择的记谱选项，不存在唯一通用于所有出版风格的“Dorico 规则”。
- [Dorico SE 5.1 官方手册](https://archive.steinberg.help/dorico_se/v5/en/Dorico_SE_5_Operation_Manual_en.pdf)：印刷页 246 给出 6/8 开头二分音符变为附点四分与八分延音的例子；印刷页 725 区分 2/2 自身分组和可选的 4/4 分组。这里保留拍号本身的含义，不强制 2/2 按 4/4。
- [Open University 复合拍子教程](https://www.open.edu/openlearn/history-the-arts/music/an-introduction-music-theory/content-section-3.9)：6/8、9/8 的大拍为附点四分，符杠应帮助显示三等分的大拍。
- [University of Puget Sound 节奏记谱教程](https://musictheory.pugetsound.edu/mt21c/CommonRhythmicNotationErrors.html)：应显示拍点，但常见切分可以作为例外。不能把“显示节拍”解释成每跨任意拍点就必须加延音线。
- 《Behind Bars》只核对了 [作者目录](https://behindbarsnotation.co.uk/contents/toc.pdf) 以及 [Faber 官方试读入口](https://www.fabermusic.com/shop/behind-bars-general-conventions-p462723/sample)。此次可访问的三页试读未提供完整的 Metre / Note-spelling 内容；没有据此声称已逐页实现 Gould 的全部细则。若以后提供书中有关章节，应逐例核对现有策略与出版社例外。

## 已实现并锁定的默认行为

以 [durationtype.cpp](../../libmscore/durationtype.cpp) 的 `toRhythmicDurationList()` / `splitCompoundBeatsForList()` / `populateRhythmicList()` 为唯一分组实现，不按拍号复制一套个人算法。

| 拍号 / 输入位置与持续时值 | 默认写法 | 原因 |
| --- | --- | --- |
| 4/4，开头整小节 | 全音符 | 无需遮蔽内部拍点来解释整小节 |
| 4/4，第二拍起二分 | 二分 | 保留常见四分—二分—四分切分 |
| 4/4，第二拍起附点四分 | 四分＋八分延音 | 露出第三拍 |
| 4/4，第二拍后半起四分 | 八分＋八分延音 | 露出第三拍 |
| 3/4，第二拍起二分 | 二分 | 三拍结构，无人为的小节中点 |
| 3/4 或 3/8，开头整小节 | 附点二分或附点四分 | 保留完整小节的合法音符 |
| 6/8，开头二分 | 附点四分＋八分延音 | 在第 4 个八分位置显露第二个大拍 |
| 6/8，开头附点二分 | 附点二分 | 完整覆盖两个大拍 |
| 6/8，开头四分 | 四分 | 单个大拍内部的合法组合 |
| 6/8 或 9/8，从第 3 个八分起四分 | 八分＋八分延音 | 跨附点四分大拍边界 |
| 9/8，开头附点二分 | 附点二分 | 覆盖两个完整大拍，允许较长值 |
| 12/8，开头附点二分 | 附点二分 | 覆盖前两个完整大拍 |
| 12/8，从第 6 个八分起四分 | 八分＋八分延音 | 露出第 3 个大拍 |
| 6/4，开头全音符 | 附点二分＋四分延音 | 与 6/8 的大拍层级等比例 |
| 6/16，开头四分 | 附点八分＋十六分延音 | 与 6/8 的大拍层级等比例 |
| 2/2，从第 4 个八分起四分 | 四分 | 半拍切分；不偷换成 4/4 层级 |
| 6/8，开头二分休止 | 附点四分休止＋八分休止 | 显示大拍，休止之间不加延音 |
| 6/8，第 2 个八分起四分休止 | 两个八分休止 | 休止的拍内组合较音符保守 |
| 任意已支持拍号，整小节休止 | 整小节休止 | 音符与休止分别处理 |

本表是此分支选择并测试的策略，不声称每项都是 Dorico 所有配置下的唯一结果。6/8、9/8、12/8 分别有 2、3、4 个大拍；不能仅按分子数字或机械的“半小节”判断。不规则/加法拍号、局部 hemiola、弱起和所有自定义分组尚未进行完整规范审计；原机制与临时关闭开关保留，不能宣称支持所有例外。

[输入测试](../../mtest/libmscore/inputrhythm/tst_inputrhythm.cpp) 检查实际输入后的时值片段、总播放时长及光标；另检查 6/8 连续两次二分输入，第二个音跨小节时总时值仍不变。已有复杂对象保护、一次撤销、和弦整链、分谱与 MIDI 回归保留。规范化不是拒绝输入，也不是把输入时值截到下一拍。

## 菜单缺失的实现原因

0.7.0 的 `auto-rhythmic-input` 没有进入 Workspace 的 action/string 注册表，而且动作归旧 QMenu 所有。恢复菜单会按注册表重建，并延迟删除原 QMenu：开关可能被丢弃甚至随旧菜单销毁，核心偏好却仍生效。

0.7.1 在 [musescore.cpp](../../mscore/musescore.cpp) 注册该动作，将所有权放到主窗口；`updateMenus()` 向旧工具菜单补入动作并重新翻译，插在 `reset-groupings` 后。保留原工作区、偏好与菜单项目，不要求用户删除配置。中文旧命令实际译为“重组旋律”，自动开关另叫“自动规范输入时值”。[真实 GUI 测试](../../mtest/mscore/pluginhost/tst_pluginhost.cpp) 恢复缺少该项目的旧菜单，处理延迟析构，验证中文、位置、唯一性与可切换性。

## 普通中日文与音乐符号：两条字体路径

- [textbase.cpp](../../libmscore/textbase.cpp) 中 `TextFragment::font()` 对 `ScoreText` 片段用谱面 `musicalTextFont`，缺字符时指定 `ScoreFont::fallbackTextFont()`（[sym.cpp](../../libmscore/sym.cpp)，Bravura Text）。这是速度/力度里的音乐符号路径。
- 普通标题、歌词及普通 Unicode 字符用请求的文字字体，经 QFont / QTextLayout 合并缺字字体；本分支没有固定的“标题中文默认回退字体”。[Qt 5.15.2 QFont 源码/文档](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/text/qfont.cpp) 与 [Windows 字体数据库实现](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/platformsupport/fontdatabases/windows/qwindowsfontdatabase.cpp) 说明这取决于字体、书写系统、平台、系统字体与字体链接等。
- Windows 交付目录 `bin/qt.conf` 选择 `fontengine=freetype`。不同 Qt 版本、引擎和字形度量可造成差异；看到外观不同不能直接等同于回退到了另一个字体，也不能用更改 Bravura Text 修复标题汉字。
- 本机独立 PyQt5 / Qt 5.15.2 探针注册同库 Edwin / FreeSerif / FreeSans；24 pt 的 Edwin、Times New Roman、Arial 中日文例子均回退到宋体（SimSun），两种引擎的若干 glyph bounds 不同。结果仅证明本机示例的 Qt 行为，未证明用户标准版和 3E 各自的实际回退。忽略构建树 `font-fallback-freetype.json`、`font-fallback-native.json` 保存记录。

要复现用户差异，应收集两个 exe 路径/版本、原字体、同一份 MSCX、具体文字和截图；比较应用 Qt DLL / qt.conf / 样式与片段字号，再从 QTextLayout 的 QGlyphRun::rawFont().familyName() 查实际回退。`QFontInfo` 只看主字体不足以判断混合脚本的逐片段回退。先对同一字体和相同引擎做隔离对比，再决定是否需要局部的应用级备用字体选项；不要全局替换字体引擎或写死中文回退，以免改变全谱排版和非中文用户。
