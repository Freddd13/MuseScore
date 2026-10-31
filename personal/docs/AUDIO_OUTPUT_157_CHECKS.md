# Mi Monitor 声音输出检查

2026-10-31；0.157.0 · Kumo branch / Freddd13。固定任务 F-AUDIO。

## 当前输出

按用户确认，使用 Mi Monitor 显示器音频。程序保存的输出为 PortAudio／MME，设备编号3；实际打开为双声道、44100Hz，驱动报告的输出缓冲约91.43毫秒。原有输出、音量和延迟设置保持。

## 实际播放

使用独立的八小节、64音测试谱，包含踏板。默认 MuseScore_General.sf3 与本机 CFX Compact 各完成一次播放中停止，以及一次从开头重播至自然结束。每次结束后检查播放按钮、谱面状态和鼠标操作恢复。

声卡每块输出576帧，约13.06毫秒。结果如下：

| 音源 | 操作 | 声音块数 | 最长处理时间／毫秒 | 输出欠载 | 处理超时 |
|---|---|---:|---:|---:|---:|
| SF3 | 手动停止 | 554 | 0.7432 | 0 | 0 |
| SF3 | 重播至曲尾 | 1225 | 0.7034 | 0 | 0 |
| CFX | 手动停止 | 554 | 2.4336 | 0 | 0 |
| CFX | 重播至曲尾 | 1225 | 1.2485 | 0 | 0 |

四次输出均有有限声音信号，播放、手动停止、重播与自然结束的状态检查通过。Qt报告4项通过，含开始及结束。

## 怎样继续听

打开自己的谱，先选一段熟悉的音乐，分别使用内置钢琴和CFX播放。重点听重音与渐弱、换踏板后的清晰程度、停止后尾音，以及再次播放的起点。原曲与谱面比较时，再检查循环和共同播放的对齐。发现异常时记下谱段、音源和当时操作，便于按同样过程检查。

后续仍按[完整进度](REWORK_PROGRESS.md)核对真实音乐的连续听感、原曲多轨和实体键盘录制。

## 开发检查方法

测试使用真实PortAudio设备和原生Seq播放。独立测试驱动沿用正式驱动的设备参数与处理入口，在处理入口旁记录统计；数据包含真实设备名称、帧数、峰值、最长处理时间及欠载。设备枚举中的JACK提示与实际MME输出分别核对，四次MME流均已打开成功。

原始记录在插件仓库：P2/out/actual-audio157-complete-pluginhost.txt；数值为P1/out/final-regression/actual-audio-157-SF3.json及actual-audio-157-CFX.json。初次手动停止的记录另存P2/out/actual-audio157-pluginhost.txt。

复查时明确指定本机设备和独立测试谱：

```powershell
$env:P2_TEST_BATCH='actual-audio157-complete'
$env:P2_TEST_NATIVE_RENDER='1'
$env:P2_ACTUAL_AUDIO_DEVICE='3'
$env:P2_ACTUAL_AUDIO_SCORE='E:/programming/funcodes/muse3_plugins/P1/out/final-regression/composition-independent.mscz'
python P2/test_builtin.py actualAudioOutput157
```

本批补充测试与说明，正式程序及0.157完整ZIP沿用已校验的文件。

安装目录已同步236份说明，1318个本地链接检查通过；程序、ZIP、Qt组件、扩展开关与MCP路径校验值保持。
