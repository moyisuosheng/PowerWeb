# PowerWeb

[![Build](https://github.com/moyisuosheng/PowerWeb/actions/workflows/build.yml/badge.svg)](https://github.com/moyisuosheng/PowerWeb/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/moyisuosheng/PowerWeb?color=brightgreen)](https://github.com/moyisuosheng/PowerWeb/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

<p align="center">
  <img src="icon/app-256.png" width="112" alt="PowerWeb">
</p>

手机连上同一个 Wi-Fi，用浏览器打开一个网址，就能**关机 / 重启 / 睡眠 / 锁屏 / 预约关机**。

一个约 210 KB 的 Windows 原生程序，纯 Win32 + Winsock2，不依赖 .NET、Python 或任何运行时，双击就能跑。

```
电脑名称 : DESKTOP-XXXX
程序版本 : 1.0.0
监听端口 : 8000
手机打开 : http://10.250.17.185:8000
```

## 为什么会有这个

躺床上发现电脑还在跑、想关机懒得起身；挂着下载想让它下完自动关；远程桌面断了想重启。
这些场景都不需要一个 App，一个网页就够了 —— 手机浏览器打开，点一下。

## 快速开始

1. 到 [Releases](https://github.com/moyisuosheng/PowerWeb/releases/latest) 下载 `PowerWeb-<版本>-win64.zip` 并解压
   （**`PowerWeb.exe` 和 `index.html` 必须放在同一目录**）
2. 双击 `start-exe.bat`，窗口里会打印手机要访问的地址
3. 手机连同一个局域网（同一个路由器 / 同一个手机热点），浏览器打开那个地址
4. 打不开？双击 `open-firewall.bat` 放行 8000 端口（会弹 UAC，点「是」）

页面支持「添加到主屏幕」，用起来和 App 差不多。

> 只支持 Windows 10 / 11 x64。这是内网工具，不要把端口映射到公网。

## 功能

- **立即关机 / 立即重启** —— 页面有二次确认
- **预约关机 / 预约重启** —— 选 5 分钟、30 分钟、2 小时，或者指定几点几分
- **取消预约** —— 顶部实时倒计时，随时一键取消
- **睡眠 / 锁屏**
- **访问密钥**（可选）—— 开了之后同 Wi-Fi 的其他人也操作不了
- **演练模式** —— 只打印要执行的命令，不会真的关机

## 命令行

```
PowerWeb.exe [选项]

  --port N       监听端口，默认 8000
  --key K        访问密钥；设置后 /api 请求必须带 X-Auth-Key
  --dry-run      演练模式，只打印要执行的命令，不真的关机
  --html PATH    页面文件，默认取 exe 同目录的 index.html
  --version      显示版本号
  -h, --help     显示帮助
```

## HTTP 接口

设置密钥时，所有 `/api` 请求都要带请求头 `X-Auth-Key: <密钥>`。

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/status` | `{ok, app, hostname, nowTs, now, scheduled, keyRequired, dryRun}` |
| POST | `/api/schedule` | `{"action":"shutdown"\|"restart","seconds":0}` 立即执行；`seconds:N` 或 `at:"2026-11-18T23:30"` 预约 |
| POST | `/api/cancel` | 取消当前预约（等价 `shutdown /a`） |
| POST | `/api/power` | `{"action":"sleep"}` 或 `{"action":"lock"}` |

```powershell
Invoke-RestMethod http://127.0.0.1:8000/api/status
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8000/api/schedule `
  -ContentType 'application/json' -Body '{"action":"shutdown","seconds":3600}'
```

## 从源码编译

需要 [mingw-w64](https://www.mingw-w64.org/)（gcc + windres）或 MSVC。产物是单个静态链接的 exe。

```bash
# 本机 MinGW（gcc/windres 已在 PATH）
make

# 交叉编译（Linux / macOS / WSL）
sudo apt install gcc-mingw-w64-x86-64
make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres

# 指定版本号（同时写进 exe 属性和 --version）
make VERSION=1.2.0
```

产物在 `build/dist/`：exe、`index.html` 和几个批处理已经放在一起，直接双击就能用。
`make zip` 打成发布压缩包，`make clean` 清理。

MSVC 也能编（资源用 `rc.exe` 编译）：

```bat
rc /fo build\app.res res\app.rc
cl /O2 /utf-8 src\powerweb.c build\app.res ws2_32.lib /Fe:PowerWeb.exe
```

编译产物只链接 `KERNEL32` / `msvcrt` / `USER32` / `WS2_32` / `ADVAPI32` / `SHELL32`，全是系统自带的。

## 发布流程

仓库用 GitHub Actions 做 CI，配置在 [`.github/workflows/build.yml`](.github/workflows/build.yml)：

| 触发 | 做什么 |
| --- | --- |
| push / PR | Ubuntu 上用 mingw-w64 交叉编译 → 在 Windows runner 上跑冒烟测试（起服务、调接口、验密钥） |
| 打 tag `v*` | 全部通过后自动创建 Release，附 zip、单文件 exe 和 `SHA256SUMS.txt` |

发版就是推一个 tag：

```bash
git tag v1.0.0
git push origin v1.0.0
```

版本号从 tag 取（`v1.0.0` → `1.0.0`），写进 `build/version.h`，再进 exe 属性、`--version` 输出和 Release 文件名 —— 只有一处定义。

## 目录结构

```
src/powerweb.c            服务本体，单文件（HTTP + 电源命令 + 预约）
res/app.rc                图标 + 版本信息
res/app.ico               多尺寸图标（16~256）
web/index.html            手机端页面
packaging/                随 exe 一起分发的批处理（启动 / 防火墙 / 开机自启）
scripts/smoke.ps1         冒烟测试（本地和 CI 共用）
icon/                     图标的设计源码（纯 Python 渲染，可重新生成）
```

## 几个实现细节

**预约交给 Windows 管。** 调的是系统自带的 `shutdown /s /t <秒数>`，不是页面在计时。所以关掉网页、关掉手机、甚至关掉 PowerWeb 服务，预约一样会到点执行；`state.json` 只负责渲染倒计时，删掉不影响。

**立即关机先回包再动手。** 先把 HTTP 响应 flush 出去，`Sleep(400)` 之后再调 `shutdown /s /t 0`，否则电脑一关手机收不到回执，页面会一直转圈。

**每个连接一个线程。** 手机轮询 `/api/status` 时，一个正在执行的 `shutdown` 不会把其他请求卡住。

**`shutdown /a` 的退出码 1116** 表示「没有进行中的关机操作」，代码里当成正常结果，翻译成「当前没有进行中的关机任务」而不是报错。

**睡眠用动态加载。** `LoadLibrary("powrprof.dll")` + `GetProcAddress("SetSuspendState")`，少一个链接依赖。

**图标是生成出来的。** `icon/render_icon.py` 用距离场画抗锯齿图形、自己写 PNG 编码，不依赖 Pillow / ImageMagick，改完跑一次就重出一套。

## 安全提醒

1. 不设密钥时，**同网段任何人都能关你的电脑**。建议 `--key` 或改 `start-exe.bat` 里的 `ACCESS_KEY`。
2. 不要做端口转发到公网，这是内网工具。
3. 立即关机没缓冲，`/t 0` 直接发起，未保存的文档会丢（页面有二次确认，但确认了就动手）。

## 常见问题

**手机打开一直转圈？** 先在电脑上开 `http://127.0.0.1:8000` 确认服务本身正常；再确认手机和电脑在同一网段（笔记本连有线、手机连 5G 热点就不算）。公司网络可能开了 AP 隔离。

**端口被占用？** `netstat -ano | findstr :8000` 看是谁，再换个端口最省事。

**笔记本合盖后手机连不上？** 睡眠状态网卡会断，唤醒即恢复，因为进程还在。

**在电脑上手动取消了预约，页面没变？** 页面最多 1 分钟会自己同步掉。

## License

[MIT](LICENSE)
