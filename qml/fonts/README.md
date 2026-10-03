# 中文字体

`Theme.qml` 里的 `FontLoader` 指向 `qml/fonts/msyh.ttf`。

**仓库里已经带了这份字体**（`msyh.ttf`，Microsoft YaHei，14.3 MB），
与 `cc/bili_plugin/qml/msyh.ttf` 是同一份文件 —— bili 插件在同一台词典笔上
用的就是它，所以字形覆盖和加载性能都验证过。

词典笔自带字体不含中文字形，**不加载这份字体中文会显示成方块**，所以别删。

## 为什么放在 `qml/fonts/` 而不是 `qml/` 根目录

bili 把 `Theme.qml` 和 `msyh.ttf` 都放在 `qml/` 根目录，本插件把 `Theme.qml`
放在 `qml/components/`（由 `qml/components/qmldir` 声明为 singleton，QML 单例
必须是这种布局）。因此 `FontLoader` 的相对路径要按 `qml/components/` 为基准写：
`"../fonts/msyh.ttf"`。

## 换字体

保持文件名不变，直接替换这个文件即可：

```bash
cp /path/to/SomeCJK-Regular.ttf  qml/fonts/msyh.ttf
```

要求：

- 必须是真正的 TrueType（魔数 `00 01 00 00`，含 `glyf` / `loca` 表）。
  `.ttc`（字体集合）、`.otf`（CFF）在 Qt 5.15 的 `FontLoader` 上不一定能加载。
- 文件名必须还是 `msyh.ttf`，否则 `Theme.qml` 找不到（改了记得同步那里的 `source`）。
- 字体族名最好还是 `Microsoft YaHei`：`FontLoader` 是异步的，加载完成前
  `appFont.name` 为空，`Theme.fontFamily` 会先回退到 `"Microsoft YaHei"`。

## 打包与版本库

- 打包时 `qml/` 整目录会被复制，所以字体会一起进 `com.weibopocket.client.zip`；
  `tools/verify.py` 的 N 项和 CI 的 zip 断言都会检查它确实在包里。
- `.gitignore` 里 `*.ttf` 是排除的，只对这份字体开了例外
  （`!qml/fonts/msyh.ttf`）；`.gitattributes` 标了 `*.ttf binary`，
  避免被当成文本做换行转换。
- ⚠ Microsoft YaHei 是微软的专有字体，随仓库分发属于个人自用范围；
  若要公开发布，建议换成可再分发的开源字体（LXGW WenKai、Source Han Sans 等）。
