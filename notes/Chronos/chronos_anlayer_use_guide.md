# Chronos 分析器的使用

本文介绍使用分析器的详细步骤，WCET 分析引擎使用图形化前端与用户进行交互。

## 加载 Chronos

进入包含 `gui.jar` 文件的 `$IDIR/chronos/gui` 目录，在终端下运行脚本 `gui.sh` 即可加载该工具。

主要窗口如图 1 所示，下面我们用 **insertsort** 程序为例来讨论。

## 性能分析

分析 insertsort 的 WCET 有以下几个步骤：

* 设置 SimpleScalar GCC bin, lp_solve, simplesim-3.0 路径；
* 打开测试用例目录
* 设置循环边界（如果需要的话）
* 设置其它的限制（如果需要的话）
* 设置间接跳转的目标（如果需要的话）
* 设置递归边界（如果需要的话）
* 配置处理器特性（可选）
* 性能评测
* 性能仿真（可选）


### 设置 SimpleScalar GCC bin, lp_solve 和 simplesim-3.0 的路径

正如前面提到的，Chronos 需要 SimpleScalar GCC (sslittle-na-sstrix-gcc)，前面已经描述过其安装的过程。在安装之后，我们需要告诉 Chronos 这些工具在哪里，按照如下的步骤：从“Option”菜单下单击“Simplescalar GCC bin directory" ，从对话框中定位到 GCC bin 的目录，如图所示。对于 lp_solve 和 simplesim-3.0 也是类似，从 “Option” 菜单分别找到 “ILP-solver directory” 和 “Simplesim-3.0 directory”。

### 打开测试用例目录

从 “File” 菜单选择 “Open File...” 命令，在屏幕上会弹出一个标准的对话框。在选择测试用例目录之后， Chronos 会加载源代码，然后做以下工作：

1. 调用 SimpleScalar GCC 来编译测试用例
2. 汇编之后生成二进制文件，重建程序流图（CFG），将在第三栏显示，在 CFG 中，每个正方形代表一个基本块。例如 **P:x B:y** 代表过程 x 的基本控制块 y。基本块的边缘代表的是该控制块可能的控制流向。
3. Chronos 同时会在第四栏显示汇编代码，并注有基本块的信息。

Chronos 不能对任何库函数直接调用，如果在测试用例中含有类似于 “sqrt” 这样的库函数，用户需要将这些库函数的源代码放到测试用例的目录下。



### 设置用户限制

Chronos 提供添加额外流限制的用户接口，



















