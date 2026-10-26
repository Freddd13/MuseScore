# C3-B2-A：延音后段的指法与分手

2026-10-27。源码已检查，尚未安装；当前使用0.128.0。接着C3-B2-B换指／换手时机与提前松手、连续琶音，再到C3-B3具体建议、C4、D、E、F。

## 查到的原因与处理

C3-B2-A源码：[a531055312db](https://github.com/Freddd13/MuseScore/commit/a531055312dbea5f3fe9d9d1a3dbc7f358d90113)；插件：[0ab8b2835bb9](https://github.com/Freddd13/Muse3Plugins/commit/0ab8b2835bb98f8bd27f77fc18709d962bd7d2fe)。尚未安装。

完整快照已经保留每个音头的指法和分手，实际按键区间却一直使用起音音头的资料。后半个延音上的新数字、所指音索引和分手由此遗漏。

现在分别保留实际按键身份与当前音头身份。延音段按谱位更新指法、所指音和分手；没有新的数字或单音分手记号时，沿用前一个。换手会同时更新两只手的占用记录。起音、松键和踏板继续读取原来的实际MIDI。

范围返回增加`scoreStartSeconds`，音符返回增加`positionSeconds`，用于将变速与反复中的变化位置换算成实际时间。原有查询和字段保持。在明确标注指法或分手的位置增加检查点，普通延音分段复用资料；查找用有序索引，传给原生计算的分手引用保持一个。

连续提示按实际按键身份合并，谱面定位使用当前音头。指法冲突补充具体数字与音名。布局、颜色、个人设置及乐谱保存方式保持。

## 实际检查

- 首轮9项JS复现后段指法、分手和检查点遗漏；补齐后12项通过。覆盖指法继承、不同和弦内音索引、后段换手、变速／停顿的时间换算、两遍反复、踏板残音、连续提示合并和长链。
- 一千段延音中只有一处换指，实际只生成起点和换指点两次检查；每次分手引用保持一个。全部13组JS通过。
- 原生谱例中，C4从1指换2指，另一声部E4用1指。读取后能区分；将后段C4改为1指，实际面板出现冲突，并通过“定位相关音”选中后一个音头。撤销后提示消失。
- 变速、两遍反复、从第2拍开始、保存和重开均读回后段2指。起音身份保持，换手后的旧手位清除；单纯换指继续作为保持音。
- 最后10项原生／面板检查通过，24.650秒；同时回查实际移动时间、原有反复事件、低音提示与完整编配操作。
- 共用模块的指法、换手、MIDI松手、成组随机和worker共6项通过，2.030秒，记录为`tied-c3-shared-core-pluginhost.txt`。
- 真实谱仍为2429音、57个分手记号、108条指法，引用和原文件校验一致。读取695毫秒、后台分析5804毫秒；界面95%间隔在22.2毫秒内，最长61.5毫秒。该轮worker前后内存由153.5 MiB到165.9 MiB，真实谱的音乐判断继续按后续项目逐处核对。

本机记录：`P2/out/tied-c3-before.txt`、`tied-c3-after.txt`、`tied-c3-continuity-before.txt`、`tied-c3-final-pluginhost.txt`；实际结果为`P1/out/final-regression/arrangement-tied-context.json`，真实谱资料另存`P2/out/tied-c3-real-score.json`。

## 接下来的检查

继续C3-B2-B：换指、换手的明确时机与提前松手候选相互核对；补齐连续琶音和特殊指法文字。随后C3-B3整理少量具体指法、换手和配音／对位候选，C4接入同一份资料。

记谱含义对照[Steinberg的指法说明](https://www.steinberg.help/r/dorico-elements/6.2/en/dorico/topics/notation_reference/notation_reference_fingering/notation_reference_fingering_c.html)及[延音线说明](https://www.steinberg.help/r/dorico-se/6.2/en/dorico/topics/notation_reference/notation_reference_ties/notation_reference_ties_versus_slurs_c.html)。本批以音头上明确的1—5指和已有分手记号核对时间，操作见[延音中的换指与换手](ARRANGEMENT_TIED_GUIDE.md)。
