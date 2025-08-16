#!/usr/bin/env python3
"""
Git代码统计终极完美版
1. 完全匹配手动cloc参数格式
2. 保留交互式图表和精美HTML
3. 精确排除目录和文件
"""

import os
import subprocess
import json
from datetime import datetime
import pandas as pd
from jinja2 import Template

# 配置
REPO_PATH = "."
REPORT_DIR = "code_stats_reports"
CLOC_PATH = "cloc"

# 完全复制您手动运行的参数格式
EXCLUDE_DIRS = "build,bin,obj,cmake-build-debug,cmake-build-release,out,.git,third-party,.idea,.vs,.vscode,snapshots"

def ensure_report_dir():
    """确保报告目录存在"""
    os.makedirs(REPORT_DIR, exist_ok=True)

def run_cloc():
    """运行cloc，参数格式完全匹配手动运行"""
    cmd = [
        CLOC_PATH,
        f"--exclude-dir={EXCLUDE_DIRS}",  # 关键：所有目录合并为一个参数
        "--json",
        "--git",
        "."
    ]
    
    print("执行命令:", " ".join(cmd))
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=True,
            encoding='utf-8'
        )
        return json.loads(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"cloc执行失败: {e.stderr}")
        return {"ERROR": str(e)}
    except json.JSONDecodeError:
        print("cloc返回无效JSON")
        return {"ERROR": "Invalid JSON output"}

def generate_charts(cloc_data):
    """生成可视化图表"""
    try:
        import matplotlib.pyplot as plt
        import plotly.express as px
        
        # 设置中文字体支持
        plt.rcParams['font.sans-serif'] = ['SimHei', 'FangSong', 'Microsoft YaHei', 'Arial Unicode MS']
        plt.rcParams['axes.unicode_minus'] = False
        
        # 准备数据
        languages = []
        codes = []
        for lang, stats in cloc_data.items():
            if lang == "SUM" or not isinstance(stats, dict):
                continue
            if stats.get("code", 0) > 0:
                languages.append(lang)
                codes.append(stats["code"])
        
        # Matplotlib 水平条形图
        plt.figure(figsize=(10, 6))
        plt.barh(languages, codes, color='skyblue')
        plt.xlabel('代码行数')
        plt.title('代码语言分布')
        plt.tight_layout()
        plt.savefig(os.path.join(REPORT_DIR, 'bar_chart.png'), dpi=100)
        plt.close()
        
        # Plotly 交互式图表 (改为静态图片)
        df = pd.DataFrame({
            'Language': languages,
            'Code': codes
        })
        fig = px.pie(df, values='Code', names='Language', 
                    title='代码语言分布', hole=0.4)
        fig.write_image(os.path.join(REPORT_DIR, 'pie_chart.png'))
        
        return True
    except ImportError:
        print("缺少可视化库，跳过图表生成")
        return False
    except Exception as e:
        print(f"图表生成出错: {e}")
        return False

def generate_html_report(cloc_data):
    """生成精美HTML报告"""
    if "SUM" not in cloc_data:
        return
    
    # 准备数据
    languages = []
    for lang, stats in cloc_data.items():
        if lang == "SUM" or not isinstance(stats, dict):
            continue
        if stats.get("code", 0) > 0:
            languages.append({
                "name": lang,
                "files": stats["nFiles"],
                "blank": stats["blank"],
                "comment": stats["comment"],
                "code": stats["code"],
                "percentage": round(stats["code"] / cloc_data["SUM"]["code"] * 100, 1)
            })
    
    languages.sort(key=lambda x: -x["code"])
    
    # HTML模板
    html_content = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>代码统计报告 - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</title>
        <style>
            body {{ font-family: Arial, sans-serif; margin: 2em; }}
            h1 {{ color: #2c3e50; border-bottom: 2px solid #eee; padding-bottom: 10px; }}
            .summary {{ background: #f8f9fa; padding: 15px; border-radius: 5px; }}
            table {{ width: 100%; border-collapse: collapse; margin: 1em 0; }}
            th, td {{ padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }}
            th {{ background-color: #34495e; color: white; }}
            tr:hover {{ background-color: #f5f5f5; }}
            .chart-container {{ display: flex; margin: 20px 0; }}
            .chart {{ flex: 1; padding: 15px; }}
            .percentage-bar {{ 
                height: 20px; background: #ecf0f1; 
                border-radius: 3px; margin-top: 5px;
            }}
            .percentage-fill {{
                height: 100%; background: #3498db;
                border-radius: 3px; width: 0%;
            }}
        </style>
    </head>
    <body>
        <h1>代码统计报告</h1>
        
        <div class="summary">
            <p><strong>生成时间:</strong> {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
            <p><strong>仓库路径:</strong> {os.path.abspath(REPO_PATH)}</p>
            <p><strong>排除目录:</strong> {EXCLUDE_DIRS}</p>
        </div>
        
        <h2>总体统计</h2>
        <table>
            <tr>
                <th>总文件数</th>
                <th>空白行</th>
                <th>注释行</th>
                <th>代码行</th>
            </tr>
            <tr>
                <td>{cloc_data['SUM']['nFiles']}</td>
                <td>{cloc_data['SUM']['blank']}</td>
                <td>{cloc_data['SUM']['comment']}</td>
                <td>{cloc_data['SUM']['code']}</td>
            </tr>
        </table>
        
        <div class="chart-container">
            <div class="chart">
                <h3>代码分布条形图</h3>
                <img src="bar_chart.png" style="width:100%;">
            </div>
            <div class="chart">
                <h3>代码分布饼图</h3>
                <img src="pie_chart.png" style="width:100%;">
            </div>
        </div>
        
        <h2>详细统计</h2>
        <table>
            <tr>
                <th>语言</th>
                <th>文件数</th>
                <th>空白行</th>
                <th>注释行</th>
                <th>代码行</th>
                <th>占比</th>
            </tr>
            {''.join([
                f'''<tr>
                    <td>{lang['name']}</td>
                    <td>{lang['files']}</td>
                    <td>{lang['blank']}</td>
                    <td>{lang['comment']}</td>
                    <td>{lang['code']}</td>
                    <td>
                        {lang['percentage']}%
                        <div class="percentage-bar">
                            <div class="percentage-fill" style="width:{lang['percentage']}%"></div>
                        </div>
                    </td>
                </tr>'''
                for lang in languages
            ])}
        </table>
    </body>
    </html>
    """
    
    with open(os.path.join(REPORT_DIR, 'report.html'), 'w', encoding='utf-8') as f:
        f.write(html_content)

def main():
    print("=== 代码统计工具 ===")
    print(f"排除目录: {EXCLUDE_DIRS}")
    
    ensure_report_dir()
    
    print("\n[1/3] 正在运行cloc分析...")
    cloc_data = run_cloc()
    
    if "ERROR" in cloc_data:
        print(f"错误: {cloc_data['ERROR']}")
        return
    
    print("\n[2/3] 生成可视化图表...")
    generate_charts(cloc_data)
    
    print("\n[3/3] 生成HTML报告...")
    generate_html_report(cloc_data)
    
    print(f"\n✅ 完成！报告已生成到: {os.path.abspath(REPORT_DIR)}")
    print(f"HTML报告: file://{os.path.abspath(os.path.join(REPORT_DIR, 'report.html'))}")

if __name__ == "__main__":
    main()