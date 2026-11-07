# 随包视频编码工具

程序使用独立 FFmpeg 进程生成视频。谱行、光标、高亮和透明画面由 MuseScore 合成，再以有限队列送入编码器。

- 位置：程序同目录 `tools/video/ffmpeg.exe`。
- 已核对版本：`2021-12-20-git-631e31773b-full_build-www.gyan.dev`，Windows x64 静态构建。
- SHA-256：`8c40af3558a4f452b4eafd6b870d8a75dd0f2c91577103a5f7550f9d2a1de549`。
- 使用 libx264／AAC 输出普通 MP4，prores_ks／ProRes 4444 输出透明 MOV；透明声音使用 PCM。
- 原分发者：[Gyan](https://www.gyan.dev/ffmpeg/builds/)。[FFmpeg 对应源码](https://github.com/FFmpeg/FFmpeg/commit/631e31773b)。随包目录包含原 LICENSE、README.txt 和记录构建配置的 provenance.json。
- 本工具许可为 GPL v3，完整许可与来源说明随工具分发。

构建时用 `personal/tools/install_score_video_tools.py --source 分发目录 --bin 程序bin目录` 放入经过核对的文件。发布脚本会连同 tools/video 整个目录打包。运行视频功能时无需另外安装 Python。

开发者可以通过 QSettings 的 `scoreVideo/ffmpeg` 指向另一份编码器。正式程序优先使用随包版本。
