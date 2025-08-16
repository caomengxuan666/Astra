#!/usr/bin/env python3
"""
generate_command_index.py

为 Redis 命令实现文件生成可跳转的命令索引，使用 /* */ 包裹，每行带 * 前缀。
"""

import re
import sys
from pathlib import Path


def extract_commands(filepath):
    """从文件中提取所有命令类及其对应的命令名"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # 匹配命令注册结构：{"GET", ...}
    command_pattern = re.compile(r'\{"([A-Z]+)"\s*,', re.MULTILINE)
    command_names = command_pattern.findall(content)

    # 去重，保持顺序
    seen = set()
    ordered_commands = []
    for cmd in command_names:
        if cmd not in seen:
            seen.add(cmd)
            ordered_commands.append(cmd)

    # 匹配类定义：class XxxCommand : public ICommand
    class_pattern = re.compile(r'class\s+([A-Za-z]+Command)\s*:\s*public\s+ICommand')
    classes = class_pattern.findall(content)

    # 构建 {command_name: class_name} 映射
    command_to_class = {}
    for cls in classes:
        stem = cls[:-7]  # remove "Command"
        cmd_name = ''.join([c.upper() if c.isupper() or i == 0 else c for i, c in enumerate(stem)])
        cmd_name = re.sub(r'([A-Z])', r'\1', cmd_name).upper()  # 全大写
        if cmd_name in ordered_commands:
            command_to_class[cmd_name] = cls

    result = []
    for cmd in ordered_commands:
        cls = command_to_class.get(cmd, "UnknownCommand")
        result.append((cmd, cls))

    # 按字母顺序排序
    result.sort(key=lambda x: x[0])
    
    return result


def generate_index(commands):
    width = 87
    inner_width = width - 2

    def pad(s, total):
        return s + " " * (total - len(s))

    lines = []
    lines.append("/*")
    lines.append(" * ┌" + "─" * (inner_width - 2) + "┐")
    lines.append(" * │" + pad(" 📚 Astra Redis 命令索引（Command Index） ", inner_width - 2) + "│")
    lines.append(" * ├" + "─" * (inner_width - 2) + "┤")

    for i, (cmd, cls) in enumerate(commands, 1):
        # 添加序号，右对齐
        index = f"{i:2d}."
        line = f" * │ {index} {pad(cmd, 10)}→ {cls}::Execute{' ' * 47}"
        line = line[:width - 1] + "│"
        lines.append(line)

    lines.append(" * └" + "─" * (inner_width - 2) + "┘")
    lines.append(" */")

    return "\n".join(lines)


def update_file(filepath, index_block):
    """将索引插入文件开头"""
    filepath = Path(filepath)
    content = filepath.read_text(encoding='utf-8')

    # 删除旧的索引（支持 /* */ 和 // ┌ 两种格式）
    old_index_pattern = re.compile(
        r'/\*\s*\n(?:\s*\*\s*.*?\n)*\s*\*/\s*\n|//\s*┌.*?//\s*└.*?\n',
        re.DOTALL
    )
    content = old_index_pattern.sub('', content)

    # 添加新索引
    new_content = index_block + "\n\n" + content

    filepath.write_text(new_content, encoding='utf-8')
    print(f"✅ 命令索引已更新：{filepath}")


def main():
    filepath='CommandImpl.hpp'

    if not Path(filepath).exists():
        print(f"❌ 文件不存在: {filepath}")

    print(f"🔍 扫描文件: {filepath}")
    commands = extract_commands(filepath)
    print(f"📦 发现 {len(commands)} 个命令")

    index_block = generate_index(commands)  # 直接使用生成的索引，不添加 //
    update_file(filepath, index_block)

    # 输出示例
    print("\n✅ 示例索引预览：")
    print(index_block)


if __name__ == "__main__":
    main()