# D4：字体对照与谱行锁定

2026-10-29，0.138.0 · Kumo branch / Freddd13。

## 复现与修改

- 在锁定行的中间插入小节，原来新小节没有禁止换行。修复后读取前后同属的锁定范围，按当前谱建立相应记号，随插入一起撤销；总谱、分谱各自处理。
- 标题框本身已有换行，旧操作仍加入额外的前边界标记。保存重开时，读取器按已有换行移除重复记号，导致分组记录数量不同。现在沿用标题框的固有换行。
- 首次分谱检查纠正了测试中的登记顺序、分谱自身分页及范围选音。采用软件相同的AddExcerpt顺序和实际音头后，再检查产品行为；旧失败记录保留。

## 正式操作结果

- engraving138-formal-host：11 passed, 0 failed, 0 skipped, 0 blacklisted, 32168ms。
- engraving138-formal-editor：9 passed, 0 failed, 0 skipped, 0 blacklisted, 7674ms。
- engraving138-formal-input：44 passed, 0 failed, 0 skipped, 0 blacklisted, 863ms。
- engraving138-formal-p0：5 passed, 0 failed, 0 skipped, 0 blacklisted, 2213ms。

- 从菜单实际点击锁定跨行选区，再解除全谱锁定，核对一次撤销、重做、原手工禁止换行与分页保留；当前谱同时导出PDF。
- 锁行内插入小节，连续撤销、重做并保存重开。另在总谱锁前半段、分谱锁后半段，分别插入行内／行外，核对每份谱实际采用的设置。
- 原有时值输入整组、两个4/4例子、符号面板、分手形状、密集圆滑线、停播后面板和普通MIDI一起回查。

## 官方3.6.2读取与重存

- lock-insert-138：音符／时值相同，三个MIDI的起音与松键事件相同，共128个；PDF 24944字节。
- lock-independent-parts-138：音符／时值相同，三个MIDI的起音与松键事件相同，共128个；PDF 25012字节。
- lock-selected-138：音符／时值相同，三个MIDI的起音与松键事件相同，共128个；PDF 24843字节。

普通版继续保留常规分行设置；额外分组资料以本分支原文件为准。测试文件和用户原谱都核对前后校验值。

## 用户原谱字体对照

只读源文件：《如果有来生-release5.mscz》，SHA256：4a62cc33723b1c19a207102f27425c983e9fd40d570a2ff65d7a818b82ac91ad。

- 两个程序均导出8页。标题请求Palatino Linotype，实际中文均采用KaiTi／楷体27点，标题位置相同；拉丁文字与速度数字采用Palatino Linotype。
- 速度音符与附点均为10.6点。兼容字体中5个对应码位的轮廓和字宽，与官方v3.6.2字体来源逐一相同。
- 600dpi实际图像中，速度音符高度均95像素，附点高度均12像素；音符宽37／38像素，附点宽12／13像素。字号和字形保持；本批没有调整全局字体或用户个人样式。
- 标题、速度字形图和PDF仅作本机对照，保存在P2/out/font-comparison-138。原谱内容保持。

## 文件与下一项

代码集中在LayoutBreak::inheritSystemLock与独立谱行工作流；插入调用为libmscore/edit.cpp中的一行。插件仓库保留Repairs/patches/system-lock-insertion.patch，便于合并时核对。

复现：engraving138-insert-before、insert-fixed、native-parts、separated-rows；正式结果见engraving138-formal-checks.json、engraving138-stock/report.json及font-comparison-138中的明细。

操作见[固定谱行与字体](ENGRAVING_138_GUIDE.md)。下一项E1连接与乐谱身份，随后E2数据／图像及AI采用、E3分享／重载、E4上游评估，最后F的完整使用与实际设备。

正式程序已安装：PDF 7446字节，MIDI 181字节、6音。个人配置、Qt绘图组件、MCP及Codex配置保持。
