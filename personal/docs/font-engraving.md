# 外部字体的专用波浪滑音（0.71）

使用步骤：在偏好设置的 Score Fonts 目录放入完整的外部音乐字体文件夹，重启；在乐谱副本的“格式 → 样式 → 乐谱”选择该音乐字体，使用原有波浪滑音对象。字体的 JSON 可声明：

```json
{"kumoEngraving":{"wavyGlissandoGlyph":"wiggleGlissando"}}
```

仅外部音乐字体读取此可选声明；字形必须在该字体自身存在、有墨迹及有限正字宽。不声明、拼错、缺字或空字形均保持旧的 `wiggleTrill` 路径。内置字体、直滑音和颤音不受影响。它不新增谱面属性或文件格式，不改变播放、音高、时值、附着与撤销方式；旧程序打开相同谱面仍用其原有滑音波形。

原先波浪滑音在 `GlissandoSegment::draw()` 固定借用颤音符号，单纯改字体的专用滑音字形无法生效。此接口只让字体声明自己的绘制符号，避免按家族名称硬编码。`ScoreFont` 在加载时验证并缓存 `SymId`，复制字体时保留；不依赖插件或 MCP。重复、居中和外框共用计算；极短段仅压缩一段波形的水平宽度，保持高度。真实墨迹外框用于选择与布局，方向使用 atan2。既有滑音文字位置保持；任意巨大字形与文字的避让不是此接口的通用保证。

验证：独立0.69基线检出构建 Release 主程序、完整安装数据和 `tst_fontengraving`。7项测试通过，包括缺省/未知/缺少本字库字形/空墨迹/有效声明、字体复制、实际栅格墨迹与外框；有效场景覆盖水平、上行、下行、反向、短段与零长。直接运行裸 exe 时缺少工作区数据会报 `No workspaces found`，应先 `cmake --install`，不能当成字体回归。

字体工程的同一 Study 轮廓用0.480无声明与0.490有声明对照，Control逐像素一致；Study中仅波浪滑音路径改变，直线、颤音和其它SVG元素保持。自制钢琴谱包含跨谱表、密集音与跨行段。实际谱面尺寸复查 mf、8、8va、Ped.及星号均与0.480相同；用户原MSCZ只读。纸面打印和更多宿主缩放仍单独验收。

源码：`libmscore/sym.*`、`libmscore/glissando.cpp`；独立测试入口 `mtest/mscore/fontengraving/`，用例 `mtest/libmscore/spanners/private_font_glissando_tests.inc`。构建目标 `tst_fontengraving` 位于嵌套 mtest 工程，链接完整应用测试工具。版本定位 `personal-v0.71.0`，基线 `3ec4a2db7`，提交主题 `Support opt-in external font wavy glissando glyphs`。

功能提交`4a4ef1a5e`，整合`3ff1a61d5`包含0.70的`d24539071`。整合后重新Release构建、完整安装、7项原生测试及同谱导出均通过。最终隔离宿主SHA256 `be8c3894c01256fbe039085981dbd5ccd26da8e0a7b80f051e81b15be2878d8b`。字体项目中的最终证据为`dist/native-gliss-133/`、`native-size-134/`、`native-size-audit-135/`，用户谱副本仍9页。原安装程序保持。
