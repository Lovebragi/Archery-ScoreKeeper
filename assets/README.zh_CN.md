<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 射箭计分器中文字库子集

射箭计分器不使用整套 CJK 字库，而是按应用实际会显示的字符串生成子集（8 MB
Flash、无 PSRAM，且界面文本是固定的）。

| 文件 | 内容与用途 |
| --- | --- |
| [`fonts/archery_chars.txt`](fonts/archery_chars.txt) | 非 ASCII 码点清单。可打印 ASCII `U+0020`–`U+007E` 始终隐式包含。 |
| [`fonts/archery_font_16.c`](fonts/archery_font_16.c) | 正文，16 px，4 bpp PLAIN，行高 24 px，位图约 19 KB。 |
| [`fonts/archery_font_20.c`](fonts/archery_font_20.c) | 标题与强调，20 px，4 bpp PLAIN，行高 30 px，位图约 29 KB。 |
| [`fonts/archery_font_16.json`](fonts/archery_font_16.json)、[`fonts/archery_font_20.json`](fonts/archery_font_20.json) | 每个字形的度量与打包位图 SHA-256，由 `tests/test_archery_fonts.py` 校验。 |
| [`fonts/OFL.txt`](fonts/OFL.txt) | 源字族的 SIL Open Font License 1.1。 |

- 源字族：Noto Sans SC，字重 400（可变字体默认实例为 100，生成脚本会显式设置轴值）。
- 来源：<https://github.com/google/fonts/tree/main/ofl/notosanssc>
  （`NotoSansSC[wght].ttf`，OFL-1.1）。源字体不提交，生成的子集已足够且能保持
  仓库体积。
- 转换：`tools/gen_lvgl_text_font.py` 用 Pillow/FreeType 光栅化后直接输出 LVGL 9
  `fmt_txt` 格式；刻意不使用 `lv_font_conv`。命令见
  [`docs/applications/archery-scorekeeper.zh_CN.md`](../docs/applications/archery-scorekeeper.zh_CN.md)
  与清单文件头部。
- 目标路径：由 `main/CMakeLists.txt` 链接进 `main` 组件，界面通过
  `main/archery_fonts.h` 选择 `archery_font_16` / `archery_font_20`。
- Flash 影响：字形位图与描述符约 53 KB，常驻只读 Flash，不占静态 RAM。
- 覆盖契约：UI 文本出现字库或清单未覆盖的码点时静态测试失败；修改界面文字后
  需重新生成两个字级。


## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
