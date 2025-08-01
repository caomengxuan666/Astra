#!/usr/bin/env python3
"""
✅ 终极版 Clang-Tidy 脚本
功能：
  - 完全尊重 .gitignore
  - 排除 build, out, cmake-build-*, _build 等构建目录
  - 移除 MSVC PCH 编译参数（/Fp, /Yu）
  - 多线程并行分析
  - 生成 HTML 报告
"""

import os
import sys
import subprocess
from pathlib import Path
import json
import tempfile
import shutil
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
import re

try:
    from tqdm import tqdm
except ImportError:
    tqdm = None


# =============================
# 配置
# =============================

PROJECT_ROOT = Path(__file__).parent.resolve()
BUILD_DIR = PROJECT_ROOT / "build"
COMPILE_DB = BUILD_DIR / "compile_commands.json"
REPORTS_ROOT = PROJECT_ROOT / "code_stats_reports" / "clang-tidy"

# 构建目录名称模式（自动排除）
BUILD_DIR_PATTERNS = [
    "build", "Build", "BUILD",
    "cmake-build", "cmake-build-debug", "cmake-build-release",
    "out", "Out", "build-output", "_build",
    "dist", "bin", "lib", "obj"
]

# PCH 相关的 MSVC 参数
PCH_ARGS = ["/Fp", "/Yu", "/Yc", "/FPC"]

# 临时目录
TEMP_DIR = Path(tempfile.gettempdir()) / "clang_tidy_filtered"
TEMP_COMPILE_DB = TEMP_DIR / "compile_commands.json"

# 报告目录
NOW = datetime.now()
REPORT_DIR = REPORTS_ROOT / NOW.strftime("%Y-%m-%d_%H-%M-%S")
REPORT_DIR.mkdir(parents=True, exist_ok=True)
HTML_REPORT = REPORT_DIR / "index.html"
JSON_REPORT = REPORT_DIR / "report.json"


# =============================
# 工具函数
# =============================

def get_gitignore_patterns() -> list:
    """从 .gitignore 和 git 配置中读取所有忽略模式"""
    patterns = []

    # 1. .gitignore 文件
    gitignore_files = [
        PROJECT_ROOT / ".gitignore",
        PROJECT_ROOT / ".git/info/exclude",
    ]

    # 2. 全局 git ignore
    try:
        global_file = subprocess.check_output(
            ["git", "config", "--global", "core.excludesfile"],
            cwd=PROJECT_ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
        if global_file:
            gitignore_files.append(Path(global_file).expanduser())
    except Exception:
        pass

    for file in gitignore_files:
        if not file.exists():
            continue
        try:
            with open(file, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#') or line == '/':
                        continue
                    patterns.append(line)
        except Exception as e:
            print(f"⚠️ 读取 {file} 失败: {e}")
    return patterns


def is_ignored_by_git(path: Path, patterns: list) -> bool:
    """检查路径是否被 .gitignore 忽略"""
    try:
        rel_path = path.relative_to(PROJECT_ROOT).as_posix()
    except ValueError:
        return True  # 不在项目内

    # 转换为正则匹配
    for pattern in patterns:
        pat = pattern.strip()
        if not pat or pat.startswith('#'):
            continue
        if pat.startswith('/'):
            # 项目根目录开始
            regex = f"^{re.escape(pat[1:])}"
            if pat.endswith('/'):
                regex += '.*'
        elif pat.endswith('/'):
            regex = f".*/{re.escape(pat[:-1])}/.*"
        else:
            regex = f".*{re.escape(pat)}.*"
        if re.match(regex, rel_path.replace('\\', '/')):
            return True
        # 也匹配完整路径
        if pat in str(path).replace('\\', '/'):
            return True
    return False


def is_build_dir(path: Path) -> bool:
    """检查路径是否为构建目录"""
    path_str = str(path).replace('\\', '/').lower()
    return any(pattern.lower() in path_str for pattern in BUILD_DIR_PATTERNS)


def cleanup_compile_command(cmd: str) -> str:
    """清理编译命令：移除 PCH 参数"""
    args = cmd.split()
    cleaned = []
    i = 0
    while i < len(args):
        arg = args[i]
        arg_lower = arg.lower()
        if any(arg_lower.startswith(p.lower()) for p in PCH_ARGS):
            # 跳过 /Fp xxx
            if '=' not in arg and i + 1 < len(args):
                i += 1
            i += 1
            continue
        cleaned.append(arg)
        i += 1
    return " ".join(cleaned)


def filter_compile_commands():
    """过滤 compile_commands.json"""
    if not COMPILE_DB.exists():
        print(f"❌ 错误：未找到 {COMPILE_DB}")
        print("请运行：cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B build")
        sys.exit(1)

    with open(COMPILE_DB, 'r', encoding='utf-8') as f:
        try:
            db = json.load(f)
        except json.JSONDecodeError as e:
            print(f"❌ JSON 解析失败: {e}")
            sys.exit(1)

    # 获取 gitignore 模式
    gitignore_patterns = get_gitignore_patterns()

    filtered_db = []
    for entry in db:
        file_path = Path(entry["file"])

        # 1. 转换为绝对路径以便判断
        if not file_path.is_absolute():
            entry_dir = Path(entry["directory"])
            file_path = (entry_dir / file_path).resolve()

        # 2. 检查是否应忽略
        if (
            is_build_dir(file_path) or
            is_ignored_by_git(file_path, gitignore_patterns)
        ):
            continue

        # 3. 清理编译命令
        cmd = entry["command"]
        cleaned_cmd = cleanup_compile_command(cmd)

        filtered_db.append({
            "directory": entry["directory"],
            "command": cleaned_cmd,
            "file": str(file_path)
        })

    # 创建临时目录
    TEMP_DIR.mkdir(exist_ok=True)
    with open(TEMP_COMPILE_DB, 'w', encoding='utf-8') as f:
        json.dump(filtered_db, f, indent=2)

    print(f"✅ 过滤后的 compile_commands.json: {TEMP_COMPILE_DB}")
    print(f"   原始条目: {len(db)}, 过滤后: {len(filtered_db)}")


def cleanup():
    if TEMP_DIR.exists():
        try:
            shutil.rmtree(TEMP_DIR)
        except Exception as e:
            print(f"⚠️ 清理失败: {e}")

import atexit
atexit.register(cleanup)


# =============================
# 并行分析
# =============================

diagnostics_lock = threading.Lock()
all_diagnostics = []


def parse_diagnostics(output: str) -> list:
    diagnostics = []
    lines = output.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if ":" in line and any(kw in line for kw in ("warning:", "error:", "note:")):
            try:
                parts = line.split(":", 3)
                if len(parts) < 4:
                    i += 1
                    continue
                file_path = parts[0].strip()
                line_no = parts[1].strip()
                col_no = parts[2].strip()
                msg_part = parts[3].strip()
                msg_type = msg_part.split()[0].lower() if msg_part else "unknown"
                message = " ".join(msg_part.split()[1:]) if len(msg_part.split()) > 1 else msg_part

                notes = []
                j = i + 1
                while j < len(lines) and "note:" in lines[j]:
                    note_parts = lines[j].split(":", 3)
                    if len(note_parts) >= 4:
                        notes.append(note_parts[3].strip())
                    j += 1

                diagnostics.append({
                    "file": file_path,
                    "line": line_no,
                    "column": col_no,
                    "type": msg_type,
                    "message": message,
                    "notes": notes
                })
                i = j
            except Exception:
                i += 1
        else:
            i += 1
    return diagnostics


def analyze_file(file_entry: dict):
    """分析单个文件"""
    file_path = Path(file_entry["file"])
    rel_file = file_path.relative_to(PROJECT_ROOT) if file_path.is_relative_to(PROJECT_ROOT) else file_path

    cmd = ["clang-tidy", str(file_path), f"-p={TEMP_DIR}"]
    try:
        result = subprocess.run(
            cmd,
            cwd=PROJECT_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=300
        )
        output = result.stdout.strip()
        diags = parse_diagnostics(output)
        for d in diags:
            d["file"] = str(rel_file)
        with diagnostics_lock:
            all_diagnostics.extend(diags)
    except Exception as e:
        print(f"❌ {rel_file} 分析失败: {e}")


def run_clang_tidy_parallel(entries: list):
    max_workers = min(16, (os.cpu_count() or 4) * 2)
    print(f"🚀 启动 {max_workers} 个线程")

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = [executor.submit(analyze_file, entry) for entry in entries]
        iterator = as_completed(futures)
        if tqdm:
            iterator = tqdm(iterator, total=len(futures), desc="🔍 分析", unit="文件")
        for _ in iterator:
            pass
    return all_diagnostics


# =============================
# 报告生成
# =============================

def generate_html_report(diagnostics: list):
    total = len(diagnostics)
    errors = len([d for d in diagnostics if "error" in d["type"]])
    warnings = len([d for d in diagnostics if "warning" in d["type"]])

    # 按文件分组
    files = {}
    for d in diagnostics:
        fname = d["file"]
        if fname not in files:
            files[fname] = []
        files[fname].append(d)

    body = f"""
    <h1>✅ Clang-Tidy 分析报告</h1>
    <p><strong>项目:</strong> {PROJECT_ROOT.name}</p>
    <p><strong>时间:</strong> {NOW.strftime("%Y-%m-%d %H:%M:%S")}</p>

    <div style="background:#ecf0f1;padding:15px;border-radius:8px;">
        <strong>📊 摘要:</strong>
        <span style="color:#e74c3c;">❌ 错误: {errors}</span>
        <span style="color:#f39c12;margin-left:10px;">⚠️ 警告: {warnings}</span>
        <span style="margin-left:10px;">📌 总数: {total}</span>
    </div>

    <h2>📋 详细问题</h2>
    """

    for file, diags in files.items():
        body += f"<details><summary><strong>{file}</strong></summary><ul>"
        for d in diags:
            notes = "".join([f"<li><i>{note}</i></li>" for note in d["notes"]])
            body += f"<li><strong>{d['type']}:</strong> {d['message']} (L{d['line']}:{d['column']}) {notes}</li>"
        body += "</ul></details>"

    body += f"<hr><small>Generated by run_clang_tidy.py | Project: {PROJECT_ROOT}</small>"

    html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Clang-Tidy Report</title></head><body>{body}</body></html>"""

    with open(HTML_REPORT, "w", encoding="utf-8") as f:
        f.write(html)

    with open(JSON_REPORT, "w", encoding="utf-8") as f:
        json.dump({"diagnostics": diagnostics, "summary": {"errors": errors, "warnings": warnings}}, f, indent=2)

    print(f"\n🎉 报告已生成: {HTML_REPORT}")


# =============================
# 主函数
# =============================

def main():
    print("🔧 准备分析环境...")

    # 1. 过滤 compile_commands.json
    filter_compile_commands()

    # 2. 读取过滤后的数据库作为分析目标
    with open(TEMP_COMPILE_DB, 'r', encoding='utf-8') as f:
        entries = json.load(f)

    if not entries:
        print("✅ 没有需要分析的文件（全部被忽略）")
        # 仍生成空报告
        generate_html_report([])
        return

    print(f"✅ 共 {len(entries)} 个文件待分析\n")

    # 3. 并行分析
    diagnostics = run_clang_tidy_parallel(entries)

    # 4. 生成报告
    generate_html_report(diagnostics)


if __name__ == "__main__":
    main()