#!/usr/bin/env bash
# Octoaxes 主控固件烧录脚本
#   ./download.sh                交互选择
#   ./download.sh safe           启用激光联锁（pin 2 需接联锁信号，常规出厂版本）
#   ./download.sh nointerlock    禁用激光联锁（无联锁工位用，否则 D1-D5 荧光通道点不亮）
set -e
cd "$(dirname "$0")"

choice="${1:-}"
if [ -z "$choice" ]; then
    echo "请选择烧录版本："
    echo "  1) 启用联锁 (safe)        - 需要 pin 2 接激光联锁信号才能开 D1-D5 TTL 端口"
    echo "  2) 禁用联锁 (nointerlock) - 跳过联锁检查，无激光工位用；LED 矩阵不受影响"
    read -rp "输入 1 或 2: " ans
    case "$ans" in
        1|safe|SAFE)              choice=safe ;;
        2|nointerlock|NOINTERLOCK) choice=nointerlock ;;
        *) echo "无效选择: $ans" >&2; exit 1 ;;
    esac
fi

case "$choice" in
    safe)         env=teensy41 ;;
    nointerlock)  env=teensy41_nointerlock ;;
    *) echo "未知版本: $choice (允许: safe | nointerlock)" >&2; exit 1 ;;
esac

echo ">>> 烧录 env=$env"
pio run -e "$env" -t upload
