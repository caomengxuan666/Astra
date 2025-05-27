t# 贡献指南

欢迎为 Astra 项目贡献代码！请遵循以下规范以确保代码质量和风格一致性。

## 代码风格规范
### C++ 编码标准
- 使用 C++17 标准
- 遵循 clang-format 风格指南
- 所有命令发送方法必须使用 `std::move` 返回值
- 响应构建必须通过 `RespBuilder` 工具类
- 多线程安全：使用分片锁机制时需保持粒度最小化

### 命名规范
| 类型 | 命名规则 | 示例 |
|-------|---------|-------|
| 类名 | PascalCase | LRUCache |
| 方法名 | camelCase | getArgs() |
| 变量名 | snake_case | cacheInstance_ |
| 常量 | 全大写+下划线 | MAX_TTL |

## 开发流程
1. **环境准备**
   ```bash
   # 安装依赖
   sudo apt-get install clang-format libfmt-dev
   
   # 配置 Git 钩子
   cp .githooks/pre-commit .git/hooks/
   ```

2. **代码格式化**
   ```bash
   # 使用项目格式化工具
   ./format.sh  # 自动格式化所有代码
   
   # 检查格式问题
   ./format.sh --dry-run
   ```

3. **构建与测试**
   ```bash
   # 构建项目
   mkdir build && cd build
   cmake .. && make -j$(nproc)
   
   # 运行测试
   ctest --output-on-failure
   ```

## 提交规范
### Commit 消息格式
```
<type>(<scope>): <subject>

<body>
- 使用 imperative mood（命令式）描述修改内容
- 包含解决的具体问题
- 如有破坏性变更需特殊标注
```

**类型枚举**：feat, fix, docs, style, refactor, perf, test, build
**示例**：
```feat(cache): add TTL expiration support

- Implement background cleanup task for expired keys
- Add std::chrono support in command SDK
- Update example client for TTL testing```

## 分支管理
- 主分支：`main`（只接受发布版本）
- 开发分支：`develop`（默认开发分支）
- 功能分支：`feature/<name>`（需关联 issue）
- 修复分支：`hotfix/<issue-number>`（紧急修复）

## 代码审查要求
1. 必须包含单元测试
2. 通过 clang-tidy 静态检查
3. 格式化脚本验证通过
4. 文档同步更新（如 API 变更）
5. 至少一个项目维护者批准

## 常见问题
**Q: 格式化脚本报错？**
A: 确保安装了 clang-format

**Q: 测试覆盖率要求？**
A: 新功能需达到 85%+ 行覆盖率，修改需保持原有覆盖率

