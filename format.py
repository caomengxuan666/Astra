import os
import sys
import subprocess
import multiprocessing
import fnmatch
from pathlib import Path

def find_clang_format():
    """查找clang-format可执行文件"""
    # 优先使用环境变量
    if 'CLANG_FORMAT' in os.environ:
        if os.path.exists(os.environ['CLANG_FORMAT']):
            return os.environ['CLANG_FORMAT']
    
    # 检查系统路径
    for name in ['clang-format', 'clang-format.exe']:
        try:
            subprocess.run([name, '--version'], check=True, 
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            return name
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    
    return None

def load_gitignore(root_dir):
    """加载.gitignore规则"""
    gitignore_path = os.path.join(root_dir, '.gitignore')
    patterns = []
    if os.path.exists(gitignore_path):
        with open(gitignore_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                patterns.append(line)
    return patterns

def is_ignored(path, root_dir, ignore_patterns):
    """检查文件是否被.gitignore忽略"""
    rel_path = os.path.relpath(path, root_dir)
    
    # 使用git check-ignore命令检查（最准确）
    try:
        result = subprocess.run(
            ['git', 'check-ignore', '-q', rel_path],
            cwd=root_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        return result.returncode == 0
    except:
        pass
    
    #  fallback: 使用.gitignore规则检查
    for pattern in ignore_patterns:
        if fnmatch.fnmatch(rel_path, pattern) or fnmatch.fnmatch(os.path.basename(path), pattern):
            return True
    return False

def find_files(root_dir, suffixes):
    """查找所有需要格式化的文件"""
    ignore_patterns = load_gitignore(root_dir)
    files = []
    
    for dirpath, _, filenames in os.walk(root_dir):
        # 排除third-party目录
        if 'third-party' in dirpath.lower().split(os.sep):
            continue
            
        for filename in filenames:
            # 检查文件后缀
            ext = Path(filename).suffix
            if ext not in suffixes:
                continue
                
            file_path = os.path.join(dirpath, filename)
            
            # 检查是否被忽略
            if is_ignored(file_path, root_dir, ignore_patterns):
                print(f"跳过.gitignore忽略的文件: {file_path}")
                continue
                
            files.append(file_path)
    
    return files

def format_file(args):
    """格式化单个文件"""
    file_path, clang_format = args
    try:
        result = subprocess.run(
            [clang_format, '-i', '-style=file', file_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        if result.returncode != 0:
            return f"错误: 格式化 {file_path} 失败: {result.stderr}"
        return f"格式化: {file_path}"
    except Exception as e:
        return f"异常: 处理 {file_path} 时出错: {str(e)}"

def main():
    # 配置参数
    root_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    suffixes = os.environ.get('SUFFIXES', '.h .cc .cpp .hpp .cxx .hxx .C').split()
    parallel_jobs = int(os.environ.get('PARALLEL_JOBS', multiprocessing.cpu_count()))
    
    # 检查clang-format
    clang_format = find_clang_format()
    if not clang_format:
        print("错误: 未找到clang-format。请安装并确保它在PATH中，或通过CLANG_FORMAT环境变量指定路径", file=sys.stderr)
        sys.exit(1)
    
    # 输出版本信息
    try:
        version = subprocess.run(
            [clang_format, '--version'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        ).stdout.strip()
        print(f"使用 {version}")
    except:
        pass
    
    # 查找文件
    print("正在查找文件...")
    files = find_files(root_dir, suffixes)
    
    if not files:
        print("未找到需要格式化的文件")
        return
    
    print(f"找到 {len(files)} 个文件，使用 {parallel_jobs} 个并行任务处理...")
    
    # 并行处理
    with multiprocessing.Pool(parallel_jobs) as pool:
        # 传递参数元组 (file_path, clang_format)
        results = pool.map(format_file, [(f, clang_format) for f in files])
        
        # 输出结果
        for result in results:
            print(result)
    
    print("格式化完成")

if __name__ == "__main__":
    main()
    