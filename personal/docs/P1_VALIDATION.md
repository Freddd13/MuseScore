# P1 0.31.0 最终验证

2026-10-12，Windows x64 Release，personal-v0.31.0。
所有套件顺序链接当前库，在独立运行目录和临时设置下执行。

| 套件 | 通过 | 失败 | 跳过 |
|---|---:|---:|---:|
| p1native | 13 | 0 | 0 |
| pluginhost | 12 | 0 | 0 |
| scoreobserver | 17 | 0 | 0 |
| p0native | 12 | 0 | 0 |
| performanceeditor | 44 | 0 | 0 |
| midi | 79 | 0 | 0 |
| readwriteundoreset | 6 | 0 | 0 |

合计183通过，包含Qt初始化/清理。重点包含插件菜单/悬浮、原生P键盘/选中/播放颜色与选区、0.31品牌、原MIDI转换、演奏参数编辑、只读数据、一次撤销、完整图片/音频/分谱/链接及个人事件副本、实际三个P1面板、范围/voice/旋律和设置记忆。

6000音/1000小节完整表达快照：116页，单批峰值1.346ms，总读取122.550ms。实际计时含首次缓存准备和性能字段；2ms为自适应预算目标，不保证任意大谱上限。方案隐藏时不读取/刷新；原谱编辑后比较缓存标为待检查。

前批独立验证：媒体17/17、媒体存在/缺失各3/3、独立MIDI值6/6；三格式/采样率/VBR/测试音保调和时长、静音输出。插件纯JS合成错漏/多弹/锚点/同音/延音/反复、6/8与三连音、段长/播放倍率，以及窄侧栏/顶部QML布局通过。

日志和截图：E:/programming/funcodes/muse3_plugins/P1/out/final-regression；summary.json保存退出状态。工具P1/tests/run_native_regressions.py（插件仓库）支持--build和--case p1native；同构建目录不得并行构建。

安装：msvc.install_p1_x64/bin/MuseScore3Evo.exe，保留原0.26和P0目录。新增菜单Reference Player、Performance Expression、Arrangement Variants；操作详见[用户说明](../USER_GUIDE.md)。

未冒充完成的人工验收：真实演奏真值/匹配质量、代表性钢琴的主观保调/循环/裁剪听感、长时间声卡及复杂大谱压力；其它平台未验证。首次读取和全谱副本布局可能同步等待；试听段末约33ms GUI heartbeat停止，非采样精度。原曲独立运输，音频变化按大小/mtime核查，MP3 seek仍有线性成本。完整部署/限制及各批提交见插件仓库P1/DELIVERY.md。
