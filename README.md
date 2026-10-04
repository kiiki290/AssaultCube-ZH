# AssaultCube 简体中文汉化版

> **非官方汉化**，基于 [AssaultCube](https://github.com/assaultcube/AC) 1.4（上游 `release_1.4`，基线提交 `de5dbd5b1`）。
>
> 让游戏能正常显示和输入中文，可与官方客户端 / 官方服务器正常联机。

<!--
  截图位置。请补 2~3 张（建议：主菜单、设置菜单、中文聊天）。
  游戏内按截图键或在控制台执行 `screenshot`，图片会存到 screenshots/ 目录。

  ![主菜单](docs/screenshots/main-menu.png)
-->

## 汉化了什么

| 范围 | 内容 |
|---|---|
| **菜单** | 主菜单、设置（视频 / 抬头显示 / 控制台 / 玩法 / 计分板 / 键盘 / 鼠标 / 声音 / 杂项 / 模组包）、多人游戏、服务器浏览器、Bot、认证、语音通讯 |
| **游戏内文本** | 武器名、音效名、队伍与身份、击杀播报、游戏模式名、断线原因 |
| **其他** | 演示录像、参考手册、帮助页、关于页、模式说明（`gamemodedesc`） |

**中文可以任意输入** —— 聊天、昵称、服务器描述都支持 UTF-8。

## ⚠️ 已知限制

**中文聊天需要服务端也运行这个版本。**

官方服务端会对收到的聊天做 7-bit 消毒（`protocol.cpp` 里的 `c &= 0x7F`），你的中文会被**改写成随机英文字母** —— 不是不显示，是变成乱码。

| 场景 | 中文聊天 |
|---|---|
| 单机 / 局域网 / 自己开服（用本项目的服务端） | ✅ 正常 |
| 连接运行**官方原版**的服务器 | ❌ 乱码 |

其余限制：

- **地图编辑器菜单**（约 213 条）未汉化 —— 只有做地图的人会看到
- **许可协议**为法律文本，保留英文原文
- **生僻字 / 繁体字 / 韩文 / emoji 显示为空白** —— 字体裁剪到 GB2312（6763 汉字）+ 标点 + 拉丁
- **About 页的作者署名**保留原文（专有名词）

界面汉化（菜单、HUD、提示）是纯客户端的，**在任何服务器上都正常**。

## 使用

### 方式一：下载预编译版

<!-- 待补充 Release -->

### 方式二：自己编译

依赖：`clang`、`make`、`libsdl2-dev`、`libsdl2-image-dev`、`zlib1g-dev`、`libogg-dev`、`libvorbis-dev`、`libopenal-dev`，以及 OpenGL + X11 开发头文件。

```sh
cd source/src
make
make install
cd ../..
./assaultcube.sh
```

### 切换语言

设置 → **语言**，或在控制台输入：

```
uilang zh     # 简体中文
uilang en     # English
```

选择会自动保存，重启后保持。

## 实现方式

给想自己改的人：

- **运行时渲染中文**：内置 [stb_truetype](source/src/stb_truetype.h)（public domain，**无新增构建依赖**），汉字按需栅格化进动态字形图集；ASCII 继续走原有的 94 字形位图字体，保留像素观感。字号和基线可用 `cjkfontsize` / `cjkbaseline` 微调。
- **UTF-8 文本管线**：`rendertext.cpp` 的排版宏改为按码点迭代，同时保留字节偏移供光标与命中检测使用；`draw_text` 支持绘制中途切换纹理。
- **翻译查表层**：`tr()` 挂在绘制与输出的咽喉点（`draw_text` / `text_bounds` / `draw_textf` / `conoutf` / `hudoutf`），因此 CubeScript 定义的菜单**零改动**即可汉化，切换语言立即全局生效。**查不到就原样显示英文，不会出错。**
- **语言包**：`config/lang/zh.cfg`，键是屏幕上英文原文的逐字节匹配。
- **工具脚本**：`source/dev_tools/`（裁剪字体、从菜单提取可翻译字符串并校验键匹配）。

## 许可与致谢

- **AssaultCube 本体**：zlib 类开源许可，原文见 [README.upstream.md](README.upstream.md)
- **内置中文字体** `packages/misc/fonts/AcZhSans.ttf`：Noto Sans CJK SC 的子集，[SIL OFL 1.1](packages/misc/fonts/LICENSE_OFL.txt)
- **[stb_truetype](https://github.com/nothings/stb)**：public domain

---

## English

Unofficial Simplified Chinese localization for [AssaultCube](https://github.com/assaultcube/AC) 1.4 (branch `release_1.4`, base commit `de5dbd5b1`).

- Localizes the menus and in-game text: weapons, sound names, teams, kill messages, game modes, disconnect reasons, help and demo screens.
- Arbitrary Chinese input works (chat, nicknames, server descriptions).
- **Chinese chat requires the server to run this build as well** — the stock server masks chat to 7 bits, corrupting UTF-8. Interface localization is client-side and works on any server.
- Switch with `uilang zh` or *Settings → Language*.

Built on upstream `release_1.4` (base commit `de5dbd5b1`).
