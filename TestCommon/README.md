# blk_buf / linked_list 单元测试

采用随项目保存的 [Unity v2.6.1](https://github.com/ThrowTheSwitch/Unity/tree/v2.6.1)
和 CTest。共 52 个用例：`blk_buf` 40 个，`linked_list` 12 个。构建无需联网。

## 构建与运行

需要 CMake 3.20+ 和支持 C11 的主机 C 编译器。在项目根目录执行；下例适用于
MinGW 的 `gcc` 和 `mingw32-make` 均已加入 PATH 的 Windows 环境：

```powershell
cmake -S . -B build-tests -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

如果使用 Ninja、Visual Studio 或 Linux，可更换/省略 `-G`；更换生成器时使用新的
构建目录。多配置生成器还需要在构建时加 `--config Debug`、运行 CTest 时加 `-C Debug`。

```powershell
# 只运行某个模块
ctest --test-dir build-tests -L blk_buf --output-on-failure
ctest --test-dir build-tests -L linked_list --output-on-failure

# 单独复现第二次分配失败
ctest --test-dir build-tests -R '^blk_buf.test_allocate_second_block_is_contiguous$' --output-on-failure

# 或直接启动一个用例，适合设置断点
.\build-tests\tests\test_blk_buf.exe test_allocate_second_block_is_contiguous

# 导出机器可读报告，写入 build-tests/test-results.xml
ctest --test-dir build-tests --output-on-failure --output-junit test-results.xml
```

CLion 重新加载 CMake 项目后可以运行 CTest 用例，也可以为 `test_blk_buf` 或
`test_linked_list` 的运行配置填写单个用例名作为程序参数。不带参数执行测试程序会
列出可用用例并返回 2；全部运行请使用 CTest。

每个用例由 CTest 启动独立进程，超时为 5 秒。访问异常或链表死循环不会阻止后续
用例运行。没有将已知失败设置成跳过或“预期失败”；修复实现后应自然变绿。

## 覆盖内容与约定

| 模块 | 覆盖内容 |
| --- | --- |
| Init | 字段及描述符初始化、空指针、零描述符、零容量、重新初始化 |
| Allocate | 零长度、首块、连续分配、恰好填满、超过容量、`SIZE_MAX`、描述符耗尽、线性尾部、回绕、回绕后的空隙、碎片空间不足、失败不改变内部状态 |
| Write / Read | 提交状态、二进制载荷、空缓冲区、跳过未提交/已读块、按链表顺序读取、重复读取、遍历到末尾 |
| Free | 空指针、已空闲块、头/中/尾/唯一节点、重复释放、描述符回收 |
| 组合流程 | 分配→写入→提交→读取→释放→再次使用、回绕后保护仍存活的载荷，以及 512 步混合尺寸 FIFO 压力序列 |
| 链表 | 初始化、长度、判空、插入、弹出、删除各位置/不存在节点、空参数、容器宏 |

缓冲区测试还会检查前后保护字节、块范围、块间重叠、描述符是否在两个链表中恰好
出现一次，以及 `tail` 是否指向真正的尾节点。完整性检查采用有界遍历，避免测试
辅助函数自身在损坏链表上无限循环。

当前头文件没有详细接口契约，因此测试采用以下约定：

- 正长度请求超过可用连续空间返回 `BUFFER_FULL`；不占用描述符、不改变现有载荷。
- **零容量初始化返回 `BUFFER_INVALID_ARGS` 是建议约定**，对应
  `test_init_rejects_zero_capacity`。如果你希望允许零容量缓冲区，应调整这一用例，
  并保证它不能成功分配任何正长度块。
- 按现有 ReadBlock 的意图，跳过 `BLOCK_ALLOCATED` / `BLOCK_READ`，返回最早的
  `BLOCK_WRITTEN`；全部找不到时返回 `BUFFER_EMPTY`。
- 失败时输出指针的值未定义，测试不要求它必须清空或保持原值。
- 不规定空闲描述符的分配顺序，不测试容器宏接收 NULL 的行为；应在转换前判断 NULL。
- 尚未规定向空闲块提交、跨缓冲区释放、重复提交和并发访问的契约，未为这些行为
  添加武断的状态码要求。

部分分配、读取和释放用例使用 `seed_layout` 直接构造合法状态。这是白盒测试夹具，
用于独立进入特定分支，避免“第二次分配失败”遮蔽后续问题；没有替换任何被测函数。
另保留纯公开接口的完整生命周期和回绕用例；当前这些组合用例会在首个缺陷处停止。

新增用例时定义测试函数，并在文件末尾的 `cases` 表新增一行
`TEST_CASE(test_name),`。CMake 从该表自动注册 CTest 用例；重新构建时会重新配置。

## 当前实现的实测结果

2026-09-17，Windows x64、MinGW GCC 14.2.0、CMake/CTest 3.28.1、Debug C11：

| 测试集 | 通过 | 断言失败 | 访问异常 | 总数 |
| --- | ---: | ---: | ---: | ---: |
| blk_buf | 40 | 0 | 0 | 40 |
| linked_list | 12 | 0 | 0 | 12 |
| 合计 | 52 | 0 | 0 | 52 |

构建成功，CTest 的 52 个用例全部通过。新增的混合尺寸压力用例使用公开 API
执行 512 步生产/消费操作，确认 FIFO 顺序、存活载荷、描述符归属和 tail 指针在
多次回绕后始终一致。

`linked_list.h/.c` 的 6 个函数使用普通外部函数定义，避免 C11 跨翻译单元的
`inline` 链接问题。测试入口可通过 `-DBUILD_TESTING=OFF` 关闭。
