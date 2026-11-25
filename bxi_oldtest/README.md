# 电机老化测试

默认先执行 3 轮「50 次上下电 → 上电 7200 秒」，然后再执行一组 50 次上下电，最后保持上电。一次上下电为上电 4 秒、断电 6 秒。每段定时上电结束后先断电 6 秒，再开始下一组，包括最后补做的一组。包含上下电的模式如果启动时 GPIO2 已开启，也会先断电 6 秒。初始为断电时，默认流程计划在 6 小时 33 分 38 秒后进入一直上电。

配置命令把值保存在工程目录的 `terminal.conf`，每个选项只更新对应的值。配置文件尚未创建时使用默认值 50、7200 秒和 3 轮。`--round` 仅用于交错模式。已经启动的进程使用启动时读取的配置。

日志记录轮次、周期、单调时钟经过时间、GPIO2 输出和 GPIO0 输入。程序核对 GPIO2 输出寄存器。GPIO0 为诊断记录，需独立测量供电才能确认电机端实际电压。驱动的供电接口作用于板上的电机电源使能，程序无需发送 CAN 运动指令。

在主控机上编译并预演。

```bash
cd /home/bxi/bxi_ws/bxi_oldtest
cmake -S . -B build
cmake --build build
./build/bxi_oldtest --dry-run
./build/bxi_oldtest --help
```

例如先保存每组 20 次上下电，随后在前台启动交错流程。

```bash
./build/bxi_oldtest --on_off_power 20
sudo ./build/bxi_oldtest --run
```

指定单项模式时，运行一组上下电或一段定时上电，然后保持上电。启动时也可同时传入新值，程序会保存后执行。

```bash
sudo ./build/bxi_oldtest --run on_off_power --on_off_power 20
sudo ./build/bxi_oldtest --run continuous_power --continuous_power 7200
```

`--dry-run` 可以附带模式和数值来预演，例如 `./build/bxi_oldtest --run on_off_power --on_off_power 1 --dry-run`。预演使用模拟时钟和电源状态，临时覆盖值仅用于当次预演。

用 systemd 后台启动交错流程，让进程在 SSH 断开后继续运行。

```bash
sudo systemd-run --unit=bxi-oldtest --collect /home/bxi/bxi_ws/bxi_oldtest/build/bxi_oldtest --run
```

查看进度。

```bash
sudo journalctl -u bxi-oldtest -f
```

前台用 `Ctrl+C` 停止，后台用下列命令停止。程序收到停止信号后断电并释放 PCI 驱动。

```bash
sudo systemctl stop bxi-oldtest
```
