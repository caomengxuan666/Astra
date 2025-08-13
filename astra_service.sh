#!/bin/bash
set -e

# 自动检测实际用户（即使使用sudo）
# SUDO_USER变量会保存调用sudo的原始用户
if [ -n "$SUDO_USER" ]; then
    TARGET_USER="$SUDO_USER"
    # 获取用户的主目录
    TARGET_HOME=$(eval echo ~$SUDO_USER)
else
    TARGET_USER=$(whoami)
    TARGET_HOME=$HOME
fi

TARGET_GROUP="$TARGET_USER"
SERVICE_NAME="astra"
SERVICE_FILE="astra.service"
DEST_SERVICE="/etc/systemd/system/${SERVICE_NAME}.service"

# 显示检测到的用户信息
echo "============================================="
echo "检测到的安装用户: $TARGET_USER"
echo "用户主目录: $TARGET_HOME"
echo "============================================="

# 验证用户存在
check_user_exists() {
    if ! id "${TARGET_USER}" &>/dev/null; then
        echo "错误: 用户 ${TARGET_USER} 不存在!"
        exit 1
    fi
}

# 创建必要目录并设置权限
create_directories() {
    echo "创建必要目录..."
    local base_dir="${TARGET_HOME}/.astra"
    local data_dir="${base_dir}/data"
    local log_dir="${base_dir}/logs"
    local config_dir="/etc/astra"

    # 创建目录（用 sudo）
    sudo mkdir -p "$config_dir" "$data_dir" "$log_dir"

    sudo chown "${TARGET_USER}:${TARGET_GROUP}" "$base_dir"
    sudo chmod 700 "$base_dir"

    # 设置子目录权限
    sudo chown -R "${TARGET_USER}:${TARGET_GROUP}" "$data_dir" "$log_dir"
    sudo chown -R "${TARGET_USER}:${TARGET_GROUP}" "$config_dir"
    sudo chmod 755 "$data_dir" "$log_dir"
}

# 生成服务文件（替换用户相关变量）
generate_service_file() {
    echo "生成服务文件..."
    local data_dir="${TARGET_HOME}/.astra/data"
    local config_dir="/etc/astra"
    local exec_path="/usr/local/bin/Astra-CacheServer"
    #local config_path="${config_dir}/config.yaml" 

    # 确保模板存在
    if [ ! -f "${SERVICE_FILE}.template" ]; then
        echo "错误: 服务模板文件 ${SERVICE_FILE}.template 不存在！"
        exit 1
    fi

    # 替换所有占位符
    sed -e "s|__USER__|${TARGET_USER}|g" \
        -e "s|__GROUP__|${TARGET_GROUP}|g" \
        -e "s|__WORKING_DIR__|${data_dir}|g" \
        -e "s|__EXEC_PATH__|${exec_path}|g" \
        "${SERVICE_FILE}.template" > "${SERVICE_FILE}"

    echo "生成的服务文件内容："
    cat "${SERVICE_FILE}"
}

# 安装服务
install_service() {
    generate_service_file
    
    echo "安装服务文件..."
    sudo cp "${SERVICE_FILE}" "${DEST_SERVICE}"
    sudo systemctl daemon-reload
    
    # 清理临时生成的服务文件
    rm -f "${SERVICE_FILE}"
}

# 显示使用帮助
show_help() {
    echo "用法: $0 [选项]"
    echo "  --install   安装并配置Astra服务（需sudo权限）"
    echo "  --uninstall 卸载Astra服务（需sudo权限）"
    echo "  --help      显示帮助信息"
}

# 卸载服务
uninstall_service() {
    echo "停止服务..."
    sudo systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
    
    echo "移除服务文件..."
    sudo rm -f "${DEST_SERVICE}"
    sudo systemctl daemon-reload
    
    echo "服务已卸载（用户数据保留在 ${TARGET_HOME}/.astra）"
}

# 主逻辑
case "$1" in
    --install)
        check_user_exists
        create_directories
        install_service
        echo "Astra服务安装完成"
        echo "可使用以下命令管理服务:"
        echo "  sudo systemctl start astra"
        echo "  sudo systemctl enable astra"
        echo "  sudo systemctl status astra"
        ;;
    --uninstall)
        uninstall_service
        ;;
    --help)
        show_help
        ;;
    *)
        show_help
        exit 1
        ;;
esac
    