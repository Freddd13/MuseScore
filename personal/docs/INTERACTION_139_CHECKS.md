# 0.139 按键、踏板、播放和频谱检查

2026-10-29 · Kumo branch / Freddd13 · personal-v0.139.0。

## 实际操作

- 在卷帘、力度区、时间尺选音后按Esc，谱面与编辑器同步清空，播放位置和撤销数量保持。悬浮卷帘的独立时间尺也逐项按键检查。原有写音先结束写音，拖动中先取消拖动并恢复原值。
- 先打开编辑器，再建立声音系统并开始播放，重现了编辑器漏接通知的问题。修复后通过原生播放通知接入；Home从中途返回开头，确认第一音重新输出，播放计时继续，Esc选音保持播放。
- 连续24次Alt+Shift滚轮后，播放中的值保留为预览；停止后一次写入，一次撤销／重做。原有直接指向音符、粗细调节、拖线取消、分谱表力度与悬浮查看一起回查。
- 踏板沿用0.106的SF2／钢琴换踏板计算，补齐当前方式和时间参数提示、两种方式的说明及窗口宽度。实际核对单段跟随、全乐器、连续控制保护、分块、链接分谱、保存重开、导出选择、图中区间及拖动取消。
- 四种频谱工具下右键均输出A4试音，原曲／乐谱位置、循环、标注和撤销保持。原曲实际文件、两种对照、调律、缩放、循环与播放操作通过原有频谱检查回查。
- WaveTone配色核对七种强弱颜色、叠加透明度及旧设置恢复；查看设置和踏板窗口截图，修正了说明窗口过窄的问题。

## 正式检查

- interaction139-keys：5 passed, 0 failed, 0 skipped, 0 blacklisted, 2537ms。
- interaction139-editor：8 passed, 0 failed, 0 skipped, 0 blacklisted, 10484ms。
- interaction139-pedal：13 passed, 0 failed, 0 skipped, 0 blacklisted, 4501ms。
- interaction139-spectrum：6 passed, 0 failed, 0 skipped, 0 blacklisted, 20573ms。
- interaction139-latency：3 passed, 0 failed, 0 skipped, 0 blacklisted, 62508ms。
- interaction139-host：5 passed, 0 failed, 0 skipped, 0 blacklisted, 34236ms。

## 多谱播放与界面耗时

同一设备、1280×920窗口，同时打开四份600小节谱。每种状态播放约12秒；逐步开启编辑、滚轮及和弦／谱行带，期间滚轮调力度和Ctrl缩放。声音由实际SF2合成到缓冲，记录峰值及每块处理时间。

| 状态 | 界面间隔P95／最长（ms） | 声音块最长（ms） | 超出声音块可用时间 |
|---|---:|---:|---:|
| 0 | 31.11／47.07 | 0.306 | 0 |
| 1 | 30.91／44.44 | 0.409 | 0 |
| 2 | 30.92／46.93 | 0.956 | 0 |
| 3 | 30.41／45.63 | 0.254 | 0 |

原始记录：P2/out/interaction139-formal-checks.json、interaction139-playback-latency.json及各组同名日志。功能与源码见INTERACTION_139_GUIDE.md及source-map.tsv。早期检查发现新增Esc抢在拖动恢复前处理，已恢复原有取消顺序后重跑；保留早期失败记录。

本批核对软件输出、界面和保存。真实键盘、声卡与CFX／Pianoteq的音乐听感继续按原计划在F进行；0.106原有六份钢琴音频输出记录保留。
