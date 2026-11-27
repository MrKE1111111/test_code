# 电机老化测试

程序通过 PCI 驱动控制电机电源。一次「上下电」是先上电 4 秒，再断电 6 秒，共 10 秒。程序记录每次切换、GPIO2 使能输出和 GPIO0 反馈输入。GPIO2 用于校验程序设置的使能状态，电机端电压可通过独立测量确认。

## 快速开始

在主控板上进入测试目录，查看帮助并预演当前配置。

```bash
cd /home/bxi/bxi_ws/test_code/bxi_oldtest
./build/bxi_oldtest --help
./build/bxi_oldtest --dry-run
```

首次使用时，直接执行下面的命令会按默认参数前台运行。已有配置时，使用上次保存的数值。

```bash
sudo ./build/bxi_oldtest --run
```

默认执行 3 轮「50 次上下电 → 上电 7200 秒 → 断电 6 秒」，再补一组 50 次上下电，最后保持上电。共 200 次上下电和 3 段两小时上电。从断电状态开始且未暂停时，计划在 6 小时 33 分 38 秒后进入最后的持续上电阶段。如果启动包含上下电的模式时电源已开启，程序会先断电 6 秒。

## 命令行参数

| 参数 | 含义 |
| --- | --- |
| `--run` | 执行交错模式。每轮做一组上下电，再做一段定时上电，随后断电 6 秒。有限轮数全部完成后，再做一组上下电并保持上电。 |
| `--run on_off_power` | 只做一组上下电，然后保持上电。使用 `--on_off_power` 的数值。 |
| `--run continuous_power` | 只做一段定时上电，然后继续保持上电。时长到达时会打印完成消息，电源状态保持开启。使用 `--continuous_power` 的数值。 |
| `--on_off_power COUNT` | 每组上下电次数，默认 `50`。`COUNT` 必须是正整数，例如 `20` 表示一组 20 次，每次 4 秒上电、6 秒断电。 |
| `--continuous_power SEC` | 每段定时上电的时长，单位为秒，默认 `7200`（2 小时）。`SEC` 必须是正整数，例如 `600` 表示 10 分钟。 |
| `--round COUNT` | 交错模式的轮数，默认 `3`。`COUNT` 必须是正整数。共有 `COUNT` 段定时上电和 `COUNT + 1` 组上下电。 |
| `--round no_stop` | 无限重复交错流程。每段定时上电结束后断电 6 秒，再开始下一轮，直至退出。 |
| `--dry-run` | 用模拟时钟快速预演所选模式。可以附带 `--run` 和配置参数，临时数值只用于当次预演。`no_stop` 的预演显示一轮。 |
| `--help` 或 `-h` | 显示命令帮助。 |

`--on_off_power`、`--continuous_power` 和 `--round` 分别更新工程目录下 `terminal.conf` 中对应的值。省略的项沿用保存值，首次使用时取默认值。单独传入配置参数只保存配置。和 `--run` 一起传入时，程序先保存再启动。单项模式只使用对应的数值，`--round` 只影响交错模式。已启动的进程使用启动时读到的配置。

## 常用示例

**只改每组次数，稍后运行。** 下面先把每组改为 20 次，再按已保存的时长和轮数执行交错流程。如果其余两项仍是默认值，会做 4 组各 20 次上下电，中间穿插 3 段各 7200 秒的上电。

```bash
./build/bxi_oldtest --on_off_power 20
sudo ./build/bxi_oldtest --run
```

**指定完整的有限轮流程。** 交错执行 2 轮「10 次上下电 → 上电 600 秒 → 断电 6 秒」，再补 10 次上下电，最后保持上电。

```bash
sudo ./build/bxi_oldtest --run --on_off_power 10 --continuous_power 600 --round 2
```

**只做一组上下电，或只做一段定时上电。** 以下是两个独立的启动示例，每条命令都会保存它指定的数值。需要切换模式时，先退出当前测试。

```bash
sudo ./build/bxi_oldtest --run on_off_power --on_off_power 20
sudo ./build/bxi_oldtest --run continuous_power --continuous_power 1800
```

第一条做 20 次上下电，之后保持上电。第二条上电 30 分钟并打印完成消息，随后继续保持上电。

**开启无限交错模式。** 使用保存的每组次数和定时上电时长。由于 `no_stop` 会被保存，后续直接执行 `--run` 也会无限交错。

```bash
sudo ./build/bxi_oldtest --run --round no_stop
```

**设置回默认值。** 下面这条命令只保存默认值，适合为下次启动恢复默认流程。

```bash
./build/bxi_oldtest --on_off_power 50 --continuous_power 7200 --round 3
```

**用小参数预演。** 模拟 1 次上下电、上电 5 秒、断电 6 秒，再补 1 次上下电。预演使用模拟电源且保留原配置。

```bash
./build/bxi_oldtest --dry-run --run --on_off_power 1 --continuous_power 5 --round 1
```

## 前台控制

程序运行时，在同一个终端输入以下命令并按回车。有限流程完成并进入持续上电后，也可以使用这些命令。

| 输入 | 行为 |
| --- | --- |
| `stop` | 断电暂停，冻结当前阶段的剩余时间。 |
| `start` | 恢复暂停前的通断电状态，继续当前阶段的剩余时间。例如上电阶段剩 2 秒时暂停，恢复后会上电跑完这 2 秒。 |
| `quit` | 正常退出，断电并释放 PCI 驱动。 |

前台也可按 `Ctrl+C` 执行退出清理。需要断开 SSH 后继续跑测试时，可用下面的后台方式启动。

## 后台运行和日志

把测试交给 systemd 后台运行。`--run` 后可追加与前台相同的模式和参数。

```bash
sudo systemd-run --unit=bxi-oldtest --collect \
  /home/bxi/bxi_ws/test_code/bxi_oldtest/build/bxi_oldtest --run
```

后台进度由 journald 收集。用下列命令查看最近 30 行或持续跟踪日志。

```bash
sudo journalctl -u bxi-oldtest -n 30 --no-pager
sudo journalctl -u bxi-oldtest -f
```

在 `journalctl -f` 中按 `Ctrl+C` 会退出日志查看。需要结束后台测试并断电时，执行下面的命令。

```bash
sudo systemctl stop bxi-oldtest
```

前台运行时，测试进度直接显示在终端中。老化程序保留自身的进度与错误信息，包括 PCI 初始化和 GPIO 校验失败。驱动的 CAN 收发错误计数输出已屏蔽。

## 从源码编译

CMake 默认使用本工程 `lib/` 下的 `bxi_pci_drv.h` 和 `libbxi_pci_drv.a`。在主控板上使用新的构建目录 `build-local`，即可用工程内的驱动重新编译。

```bash
cd /home/bxi/bxi_ws/test_code/bxi_oldtest
cmake -S . -B build-local
cmake --build build-local
./build-local/bxi_oldtest --help
```

要运行这次编译生成的版本，把上面示例中的 `./build/bxi_oldtest` 换成 `./build-local/bxi_oldtest`。后台启动时也使用对应的绝对路径。

已有的 `build-local` 如果缓存了外部驱动路径，可以执行 `cmake -S . -B build-local -U BXI_PCI_DRV_DIR` 恢复使用工程内的驱动。需要主动使用外部驱动时，可传入 `-DBXI_PCI_DRV_DIR=/实际驱动工程路径`。外部工程的 `lib/` 下需要有这两个文件，且库需要提供 `pcie_gpio_tx_get`。
