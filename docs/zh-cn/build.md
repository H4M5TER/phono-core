# 构建指南

[项目首页](../../README.md) · [English](../en-us/build.md)

## 环境与构建入口

需要 Pixi、vcpkg 和 C++ 工具链。Windows 使用安装了 C++ 桌面工作负载及 Windows SDK 的 Visual Studio x64 开发者终端，并设置 `VCPKG_ROOT`。Linux 发布构建使用独立的 glibc 2.28 sysroot 环境。标准 SDK 构建、平台要求和发布验证见 [SDK、兼容性与发布](../sdk-release.md)。

在已准备的开发环境中，日常源码构建使用：

```sh
pixi install --locked
pixi run setup
pixi run config
pixi run build
pixi run ctest --test-dir build --output-on-failure
```

`setup` 按 `ci/core-dependencies.json` 准备 ExecuTorch 源码及所需子模块。`config` 配置 CMake，`build` 增量编译，程序默认输出到 `build/bin/`。uni-algo 和 nlohmann-json 由 `vcpkg.json` 管理；简繁转换规则由 `codegen/zh2hans_codegen.py` 在配置阶段生成 C++ 表。

## 构建选项

在 CMake 配置阶段可设置：

| 选项 | 用途 |
| --- | --- |
| `PHONO_USE_INSTALLED_EXECUTORCH` | 使用独立安装的 ExecuTorch，需同时设置 `CMAKE_PREFIX_PATH` |
| `EXECUTORCH_SOURCE_DIR` | ExecuTorch 源码路径，默认 `third_party/executorch` |
| `PHONO_XNNPACK_WEIGHT_CACHE` | 是否共享 XNNPACK packed weights，默认 ON |
| `PHONO_OPS_CONFIG_DIR` | 算子清单目录，默认 `ops_config/` |
| `PHONO_OPS_MANIFESTS` | 显式指定要合并的清单；默认从清单目录选择 |
| `PHONO_DTYPE_SELECTIVE_BUILD` | 是否按清单进一步裁剪 dtype 变体，默认 **OFF** |

PhonoP2C 的导出任务会生成逐模型和合并的算子清单。将所需清单放入 `ops_config/` 后重新配置、编译，运行库会按清单并集注册算子；没有清单时构建完整内核库。

dtype 裁剪独立于算子裁剪。启用时，构建流程从合并清单生成 `selected_op_variants.h`，保留对应的 dtype 变体。标准 SDK 和 `pixi run config` 均显式关闭 dtype 裁剪；自定义构建需要在运行该任务后单独覆盖：

```sh
pixi run cmake -S . -B build -DPHONO_DTYPE_SELECTIVE_BUILD=ON
pixi run build
```

## 模型测试与性能测量

将 `PHONO_TEST_MODEL_DIR` 设置为完整的 v2.2 模型包路径后运行 CTest；未设置时模型测试跳过。发布验证通过 `PHONO_REQUIRE_TEST_MODEL=1` 要求模型测试实际执行，详见 SDK 文档。

从仓库根目录运行以下命令，传入模型目录、采样次数和预热次数（Windows 使用 `.exe`）：

```sh
build/bin/performance_benchmark <model-dir> 20 5
```

程序以 CSV 报告模型加载耗时与 RSS、各模型方法耗时、不同历史和拼音窗口下的生成耗时、增量 fill，以及缓存清零、beam 重排和 session reset 的微基准。每次采样前的输入构造、上下文填充与缓存初始化不计入该样本耗时。
