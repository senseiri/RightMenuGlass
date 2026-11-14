# RightMenuGlass 插件开发文档

RightMenuGlass 允许你用自己的 JSON 文件扩展右键菜单，**无需编译、无需重启系统**。
插件在每次弹出菜单时实时读取，保存 JSON 后下一次右键立即生效。

---

## 1. 插件放在哪里

```
%LOCALAPPDATA%\RightMenuGlass\plugins\
```

在资源管理器地址栏输入 `%LOCALAPPDATA%\RightMenuGlass` 回车即可打开该目录。
新建 `plugins` 子文件夹（如果没有），把 `*.json` 文件放进去。

- 一个 `.json` 文件 = 一个插件
- 文件名随意，例如 `我的工具.json`
- 文件必须是 **UTF-8** 编码（带不带 BOM 都行）
- JSON 出错不会崩溃，只会在日志里记录错误并跳过该文件

安装包自带的示例插件在程序目录的 `plugins-example\` 里，复制到上面的目录即可使用。

---

## 2. 最小示例

```json
{
  "name": "我的工具",
  "items": [
    {
      "text": "用记事本打开",
      "command": "notepad.exe \"%1\""
    }
  ]
}
```

只要写 `name` 和 `items` 就能工作，其余字段都是可选的。

---

## 3. 完整字段说明

### 3.1 顶层字段

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `name` | 字符串 | 文件名 | 插件名称，仅用于日志和分组 |
| `version` | 字符串 | `""` | 版本号，自由填写 |
| `author` | 字符串 | `""` | 作者 |
| `enabled` | 布尔 | `true` | `false` 可临时停用插件而不删文件 |
| `order` | 整数 | `100` | 排序，数字小的排在前面 |
| `group` | 布尔或字符串 | 无 | 见下 |
| `match` | 对象 | 无 | 匹配条件，见 3.2 |
| `items` | 数组 | 必填 | 菜单项列表，见 3.3 |

`group` 控制这些菜单项显示在哪里：

- 不写 `group`：所有菜单项**直接放到一级菜单**
- `"group": true`：收进名为「插件」的二级菜单
- `"group": "工具"`：收进名为「工具」的二级菜单（同名分组会自动合并）

### 3.2 match：匹配条件

```json
"match": {
  "contexts": ["files"],
  "extensions": [".png", ".jpg"],
  "nameContains": ["报告"],
  "pathContains": ["D:\\工作"]
}
```

所有条件同时满足才会显示，任意字段都可以省略。

**contexts（右键位置）**

| 值 | 含义 |
| --- | --- |
| `any` | 任意位置 |
| `files` | 选中了文件或文件夹 |
| `background` | 在文件夹空白处右键 |
| `folder` | 在资源管理器窗口里右键 |
| `desktop` | 在桌面右键 |

可以写多个，满足任意一个即可。不写 `contexts` 表示任何位置都显示。

**extensions**：按扩展名过滤（需要选中文件），`"*"` 表示任意扩展名。
**nameContains**：文件名包含任一关键词时显示。
**pathContains**：完整路径包含任一关键词时显示（注意 JSON 里反斜杠要写成 `\\`）。

### 3.3 items：菜单项

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `text` | 字符串 | 必填 | 菜单项文字 |
| `command` | 字符串 | 必填 | 要执行的命令 |
| `icon` | 字符串 | `""` | 预留字段，当前版本忽略 |
| `admin` | 布尔 | `false` | `true` 时以管理员身份运行（会弹 UAC） |
| `console` | 布尔 | `false` | `true` 时显示命令行窗口；默认静默执行 |
| `separatorAfter` | 布尔 | `false` | `true` 时在这个菜单项下面画一条分隔线 |

---

## 4. 命令中的变量

| 变量 | 含义 | 示例 |
| --- | --- | --- |
| `%1` | 第一个选中项的完整路径 | `D:\图片\a.png` |
| `%D` | 第一个选中项所在目录（选中的是文件时取上级目录） | `D:\图片` |
| `%N` | 第一个选中项的文件名（含扩展名） | `a.png` |
| `%S` | 所有选中项，已自动加引号并用空格分隔 | `"D:\a.png" "D:\b.png"` |
| `%C` | 选中项数量 | `2` |
| `%E` | 扩展名（小写，含点） | `.png` |

> 变量替换是纯文本替换，**不会自动加引号**（`%S` 除外）。
> 路径里有空格时请自己写引号：`notepad.exe "%1"`。

---

## 5. 示例

### 5.1 在文件夹里打开命令行

```json
{
  "name": "命令行工具",
  "group": "工具",
  "match": { "contexts": ["folder", "background", "desktop"] },
  "items": [
    { "text": "在此处打开命令行", "command": "cmd.exe /k cd /d \"%D\"" },
    { "text": "在此处打开 PowerShell", "command": "powershell.exe -NoExit -Command \"Set-Location -LiteralPath '%D'\"" }
  ]
}
```

`cmd.exe /k` 让窗口执行完命令后保留，这样命令行窗口不会一闪而过。

### 5.2 按文件类型显示（只对图片生效）

```json
{
  "name": "图片工具",
  "match": {
    "contexts": ["files"],
    "extensions": [".png", ".jpg", ".jpeg", ".webp"]
  },
  "items": [
    { "text": "用画图打开", "command": "mspaint.exe \"%1\"" },
    { "text": "复制文件名", "command": "cmd.exe /c echo %N| clip" }
  ]
}
```

### 5.3 管理员权限运行

```json
{
  "name": "系统工具",
  "items": [
    { "text": "以管理员身份打开命令行", "command": "cmd.exe", "admin": true },
    { "text": "打开服务管理器", "command": "services.msc", "admin": true }
  ]
}
```

### 5.4 全局显示（直接进一级菜单）

```json
{
  "name": "快速记事",
  "order": 5,
  "items": [
    { "text": "新建便签", "command": "notepad.exe" }
  ]
}
```

---

## 6. 调试

- 日志文件：`%LOCALAPPDATA%\RightMenuGlass\dll.log`
- 在 `config.json` 里把 `"logVerbose": true` 打开可看到每个插件的加载结果与匹配情况
- 右键菜单里没出现你的插件时，先看日志里有没有：
  `plugin loaded: 插件名 (3 actions)` 或 `plugin error: 文件名.json: invalid json`

常见问题：

| 现象 | 原因 |
| --- | --- |
| 完全没有反应 | 文件不在 `%LOCALAPPDATA%\RightMenuGlass\plugins\` 目录，或扩展名不是 `.json` |
| 日志报 `invalid json` | JSON 语法错误：多了逗号、引号没转义、用了单引号 |
| 菜单项出现但点了没反应 | 命令路径不存在；可以先用 `"console": true` 打开窗口看报错 |
| 命令窗口一闪而过 | 用 `cmd.exe /k` 而不是 `cmd.exe /c` |
| 路径带空格失败 | 命令里自己加引号：`"command": "notepad.exe \"%1\""` |

---

## 7. 安全提示

插件会以当前用户（或你确认的管理员）身份执行任意命令。
**不要使用来源不明的插件文件**——它和双击运行一个 `.exe` 没有区别。
