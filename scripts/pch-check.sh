#!/bin/bash

# 设置项目根目录
PROJECT_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo "$PWD")

# 需要插入的 PCH 行
PCH_LINE='#include "core/astra.hpp"'

# 查找所有 .cpp 文件
find "${PROJECT_ROOT}" -type f -name "*.cpp" | while read -r file; do
    # 获取第一行内容
    first_line=$(head -n 1 "$file")

    # 如果第一行不是 PCH
    if [[ "$first_line" != "$PCH_LINE" ]]; then
        echo "🔧 Fixing $file: Adding PCH include to first line."

        # 创建临时文件，插入 PCH 行
        temp_file=$(mktemp)
        echo "$PCH_LINE" > "$temp_file"
        cat "$file" >> "$temp_file"

        # 替换原文件
        mv "$temp_file" "$file"
    else
        echo "✅ $file already has correct PCH include."
    fi
done

echo "🎉 Done! All .cpp files now start with '#include \"core/astra.hpp\"'."

# 执行一次format.sh
bash format.sh 2>&1 | tee /dev/tty
echo "Running clang-tidy..."