# 字体放这里

`Theme.qml` 里的 `FontLoader` 指向 `qml/fonts/weibo.ttf`。

仓库**不包含**任何字体文件（体积与授权原因）。如果设备上的中文显示成方块，
把一个中文 TTF 复制到这里并改名即可：

```bash
cp /path/to/LXGWWenKai-Regular.ttf  qml/fonts/weibo.ttf
```

推荐（开源、可再分发）：

| 字体 | 地址 |
|------|------|
| LXGW WenKai（霞鹜文楷） | https://github.com/lxgw/LxgwWenKai |
| Source Han Sans（思源黑体） | https://github.com/adobe-fonts/source-han-sans |

要求：

- 格式必须是真的 `.ttf`（TrueType）。`.ttc` 字体集合、`.otf` 有可能加载失败。
- 文件名必须是 `weibo.ttf`，否则 `Theme.qml` 找不到。
- 字体缺失时 `Theme.fontFamily` 会自动回落到 `Microsoft YaHei`，
  插件仍可运行，只是中文可能显示异常。

打包时 `qml/` 整目录会被复制，所以字体放进来就会一起进 `com.weibopocket.client.zip`。
注意 `.gitignore` 已排除 `*.ttf`，以免把大文件提交进仓库。
