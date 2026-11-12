# Xiaomi AX3000E RN07 — OpenWrt experimental port

小米 AX3000E **RN07** 的实验性 OpenWrt 适配源码与研究记录。基于 **OpenWrt 23.05.3 / Linux 5.15.150，ARMv7**，不是 OpenWrt 官方支持机型，也不是稳定固件发布。

社区需求：[Add support Xiaomi AX3000E (RN07) #158](https://github.com/hzyitc/openwrt-redmi-ax3000/issues/158)。本仓库独立整理已有实机工作，没有声称被上游合并。

## 已经做到什么

在 **1 台原厂 1.0.24 的 RN07** 上完成：保留原厂槽 0，将自制系统装入槽 1；squashfs + UBIFS 配置持久化；一次正常重启后配置和双频 AP 保留；LuCI 中文登录和常用页面；有线 LAN；DHCP 和局域网 DNS；2.4G/5G 各上传、下载 4 MiB，SHA256 均一致；切回原厂 1.0.24 并确认原厂 Wi-Fi 和后台。

**还没有证明稳定可用：** WAN/NAT 外网、全部物理端口、80/160 MHz、长期高负载均未验收。历史无线退出/重启曾卡住，需要断电；另一次有线与无线同时不可达也靠断电恢复，原因没有定论。Q6 补丁只增加诊断日志，**不是稳定性修复**。不要把单次重启成功当成重启问题已解决。

最后按设备所有者要求切回原厂，保留原网络配置。OpenWrt 槽未擦除；这不是证明两个槽都恢复成原厂镜像。

## 内容

- `dts/`：256 MiB 内存布局、YT9215 端口连接、无线 mode2 地址、安装设备树。
- `patches/`：平台接线、限定 RN07 的 NAND 识别、只暴露环境/只读 ART/槽 1 的分区保护、Q6 诊断。
- `configs/`：当时成功启动使用的内核和 backports 配置。
- `rootfs-overlay/`：带机型及分区检查的启动标志工具和回原厂命令；仅适用于本仓库的安装布局。
- `tools/`：离线应用补丁、UBI 打包和布局校验。
- `docs/`：硬件、构建条件、启动流程、已知问题和恢复边界。

没有提供现成 `.bin/.ubi` 下载或一键刷机器。实机镜像依赖每台设备自己的校准数据、合法取得的原厂无线固件和私有初始化配置；分享个人镜像会携带这些内容。此版本公开可审阅的代码和步骤，公开构建流程尚未从零完整复现。

## 使用入口

1. 阅读 [实测范围与问题](docs/VALIDATION.md)、[硬件与分区](docs/HARDWARE.md)。
2. 按 [构建说明](docs/BUILDING.md) 在 Linux/x86_64 环境准备固定版本源码。
3. 参考 [安装流程](docs/INSTALLATION.md) 和 [恢复说明](docs/RECOVERY.md)。硬件操作不是离线脚本自动完成的。
4. 贡献补丁、可复现实测和稳定性修复，请见 [CONTRIBUTING.md](CONTRIBUTING.md)。

本仓库不会让你擦除原厂槽来挤空间装插件。设备只有 64 MiB NAND；当前保留双槽布局下可写空间约 9 MiB，余量约 8 MiB，不适合直接安装大型代理插件。

## English

Experimental, source-only Xiaomi AX3000E **RN07** port on a pinned community OpenWrt 23.05.3 / Linux 5.15.150 ARMv7 base. One device demonstrated persistent slot-1 boot, dual-band HE20 traffic, LuCI and return to the preserved stock slot. WAN and long-term stability remain unverified; shutdown/connectivity stalls required power cycling. No flashable firmware or device secrets are distributed. The public build recipe has not undergone a clean end-to-end rebuild. See the documentation before using any low-level boot helper.
