# SDK、兼容性与发布 / SDK, compatibility and releases

phono-core 独立维护版本，唯一来源是根目录 `VERSION.txt`。Release 标签必须为对应的 `vX.Y.Z`，前端通过普通提交更新 SDK 版本，不要求同步前端版本，也不触发跨仓库发布。

phono-core owns its version, with the root `VERSION.txt` as the single source. Release tags must match `vX.Y.Z`. Frontends update their SDK dependency in ordinary commits; their versions are independent and no cross-repository release is triggered.

## 使用 SDK / Consume the SDK

从 [GitHub Releases](https://github.com/phono-project/phono-core/releases) 下载对应版本的平台 SDK 和 `SHA256SUMS-<platform>.txt`，校验后解压到任意 SDK 前缀。Linux x86_64 使用 `.tar.gz`，Windows x64 使用 `.zip`；资产名为 `phono-core-sdk-X.Y.Z-linux-x86_64.tar.gz` 或 `phono-core-sdk-X.Y.Z-windows-x64.zip`。模型不随 SDK 打包。

Download the platform SDK and `SHA256SUMS-<platform>.txt` for the desired version from [GitHub Releases](https://github.com/phono-project/phono-core/releases), verify the checksum, and extract into an SDK prefix. Linux x86_64 uses `.tar.gz` and Windows x64 uses `.zip`; assets are named `phono-core-sdk-X.Y.Z-linux-x86_64.tar.gz` or `phono-core-sdk-X.Y.Z-windows-x64.zip`. Models are distributed separately.

SDK 包含 `include/phono-core/phono_api.h`、Linux 的 `lib/libphono_core.so` 或 Windows 的 `bin/phono_core.dll` 和 `lib/phono_core.lib`、`lib/cmake/phono-core`、`share/licenses/phono-core`、`share/phono-core/VERSION` 及 `build-info.json`。安装后的 C API 可以由纯 C 工程链接，不要求消费者安装 ExecuTorch、Python 或 core 源码。

The SDK contains `include/phono-core/phono_api.h`, Linux `lib/libphono_core.so` or Windows `bin/phono_core.dll` and `lib/phono_core.lib`, `lib/cmake/phono-core`, `share/licenses/phono-core`, `share/phono-core/VERSION`, and `build-info.json`. A C-only project can link the installed C API without installing ExecuTorch, Python, or core sources.

```cmake
find_package(phono-core 0.1.1 CONFIG REQUIRED)
target_link_libraries(frontend PRIVATE phono::core)
```

配置消费者时通过 `-DCMAKE_PREFIX_PATH=<SDK-prefix>` 指定 SDK。Windows 安装时将 DLL 放在应用目录，Linux 前端按自己的发行版和安装布局部署共享库。

Configure consumers with `-DCMAKE_PREFIX_PATH=<SDK-prefix>`. On Windows, deploy the DLL alongside the application. Linux frontends deploy the shared library according to their distribution and installation layout.

## 兼容范围 / Compatibility

公开接口是 `phono_api.h` 中的 C ABI，构建信息记录其 ABI 代数为 1。对象通过不透明句柄传递；字符串、数组和候选结果须用对应的 `phono_free` 或结果释放函数释放。配置 JSON 的版本与模型格式版本分别校验，C++ 内部类不属于 SDK 接口。修改现有导出函数、枚举值或公开结构布局属于 ABI 变更，应更新版本并重新验证前端。

The public interface is the C ABI in `phono_api.h`; build information records ABI generation 1. Objects use opaque handles, and returned strings, arrays, and candidate results must be released with the matching `phono_free` or result-release function. Configuration JSON versions and model format versions are checked separately. Internal C++ classes are outside the SDK interface. Changing existing exports, enum values, or public structure layouts changes the ABI and requires a version update and frontend verification.

当前模型格式为 `2.2`，正式交付验证两个模型仓库：[W4A8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w4a8_model) 的 `c11bc50c13a5a6ff627ab60a3f5571b8fdbacc73` 和 [W8A8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w8a8_model) 的 `cf11a953a62fbba10f0eb585bc65802e01b70f7d`。配置、前后段 `.pte`、词表及配置启用的分词模型必须完整。下载脚本写入本仓库 `cache/models`，测试显式接收模型目录。

The current model format is `2.2`. Release verification covers two snapshots: [W4A8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w4a8_model) at `c11bc50c13a5a6ff627ab60a3f5571b8fdbacc73` and [W8A8](https://huggingface.co/afirelily/phonop2c_v2_2_base_w8a8_model) at `cf11a953a62fbba10f0eb585bc65802e01b70f7d`. Configuration, pre/post `.pte` files, vocabularies, and any enabled segmenter assets must be present. The downloader writes into this repository's `cache/models`; tests receive model directories explicitly.

一个通用运行库同时支持这两种量化模型，不发布按精度拆分的运行库。ExecuTorch 提交和子模块由 `ci/core-dependencies.json` 锁定，Python 与构建环境由已提交的 `pixi.lock` 和 `ci/pixi.lock` 锁定。运行库包含 XNNPACK、`ops_config` 清单的算子并集及 `phono::update_mhsa_kv.out`，关闭 dtype 裁剪。新模型若需要清单外算子，需在 core 中更新清单并重新构建验证。

One universal runtime supports both quantized models; no precision-specific runtimes are published. `ci/core-dependencies.json` pins the ExecuTorch commit and gitlinks, while the committed `pixi.lock` and `ci/pixi.lock` pin Python and build environments. The runtime contains XNNPACK, the operator union from `ops_config`, and `phono::update_mhsa_kv.out`, with dtype pruning disabled. Models requiring additional operators need an updated core manifest and a rebuilt, verified runtime.

Linux 构建使用 glibc 2.28 sysroot 和 GCC 13.3；发布前读取最终 ELF 实际导入的 GLIBC、GLIBCXX、CXXABI 版本，分别限制为 2.28、3.4.31、1.3.14，并检查导出仅含 `phono_*`、没有 RPATH 及额外运行库依赖。系统须提供满足实际报告的 glibc、libstdc++ 和 libgcc；报告保存在 `build-info.json`。Windows x64 使用静态 MSVC CRT。macOS arm64 尚未纳入正式交付，须完成独立构建和模型验证后增加平台产物。

Linux builds use the glibc 2.28 sysroot and GCC 13.3. Release checks read the final ELF's actual imported GLIBC, GLIBCXX, and CXXABI versions and cap them at 2.28, 3.4.31, and 1.3.14, while checking that exports are limited to `phono_*`, no RPATH is present, and no extra runtime dependencies appear. The host must supply glibc, libstdc++, and libgcc meeting the recorded requirements in `build-info.json`. Windows x64 uses the static MSVC CRT. macOS arm64 is not a release platform yet; independent build and model verification is required before adding its artifacts.

## 构建与验证 / Build and verify

Windows 在 x64 Visual Studio 开发者终端中设置 `VCPKG_ROOT`，然后使用已锁定环境。Linux 使用独立的已锁定 sysroot 构建环境；两个入口都是 CI 和本地复现共用的仓库脚本。

On Windows, set `VCPKG_ROOT` in an x64 Visual Studio developer shell and use the locked environment. Linux uses its separate locked sysroot toolchain. Both entry points are repository scripts shared by CI and local reproduction.

```powershell
pixi run --locked python tools/build_sdk.py --prefix build/sdk --build-dir build/windows-sdk
```

```sh
pixi run --locked --manifest-path ci/pixi.toml python tools/ci_build_core.py --source . --prefix build/sdk --build-dir build/linux-sdk
```

PR 构建两个平台并运行 `unit` CTest 和安装后纯 C 消费者检查。Release 另下载并逐个运行 W4A8、W8A8 `model` 测试；缺失目录、配置或模型文件会失败，`PHONO_REQUIRE_TEST_MODEL=1` 使测试不能按成功跳过。手动入口的 `model_tests` 可复现完整模型验证。

PR builds cover both platforms, the `unit` CTests, and an installed C-only consumer. Releases additionally download and run the W4A8 and W8A8 `model` tests separately. Missing directories, configuration, or model files fail verification, and `PHONO_REQUIRE_TEST_MODEL=1` prevents successful skips. The manual workflow's `model_tests` input reproduces the complete model verification.

## 维护流程 / Maintainer workflow

完成代码、依赖及版本更新并合并到默认分支后，创建 `vX.Y.Z` 标签，再在 GitHub 发布对应 Release（正式版或预发布均可）。工作流检出该标签，校验版本与默认分支归属，完成构建、测试、SDK 与演示程序打包，然后上传两个平台的附件及校验和。只推送标签不会触发交付；只有上传任务拥有写权限。

After merging code, dependency, and version updates into the default branch, create a `vX.Y.Z` tag and publish its GitHub Release, either stable or prerelease. The workflow checks out the tag, validates the version and default-branch ancestry, builds and tests, packages the SDK and demos, and uploads both platforms' assets and checksums. Pushing a tag alone does not deliver artifacts; only the upload job has write permission.

演示程序另打包为 `phono-core-demos-X.Y.Z-<platform>`。模型单独下载，Windows 调用演示程序时传入模型目录和包内 `share/phono-core/core_configs` 下的两份配置；Linux 的 `run-demo.sh` 自动传入配置，运行方式为 `./run-demo.sh cli_demo_capi <model-dir>`。

Demos are packaged separately as `phono-core-demos-X.Y.Z-<platform>`. Download models separately. On Windows, pass the model directory and both configs from the archive's `share/phono-core/core_configs` directory. Linux `run-demo.sh` supplies the configs automatically: `./run-demo.sh cli_demo_capi <model-dir>`.
