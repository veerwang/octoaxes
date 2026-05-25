#!/usr/bin/env bash
# 手控盒固件烧录脚本
#   ./download.sh            交互选择
#   ./download.sh cn          国内版本（默认 /8 灵敏度）
#   ./download.sh overseas    海外版本（/4 灵敏度，2x 输出）
set -e
cd "$(dirname "$0")"

choice="${1:-}"
if [ -z "$choice" ]; then
    echo "请选择烧录版本："
    echo "  1) 国内版本 (cn)       - joystick X/Y 灵敏度系数 /8"
    echo "  2) 海外版本 (overseas) - joystick X/Y 灵敏度系数 /4"
    read -rp "输入 1 或 2: " ans
    case "$ans" in
        1|cn|CN)        choice=cn ;;
        2|overseas|OS)  choice=overseas ;;
        *) echo "无效选择: $ans" >&2; exit 1 ;;
    esac
fi

case "$choice" in
    cn)        env=teensyLC ;;
    overseas)  env=teensyLC_overseas ;;
    *) echo "未知版本: $choice (允许: cn | overseas)" >&2; exit 1 ;;
esac

echo ">>> 烧录 env=$env"
pio run -e "$env" -t upload
