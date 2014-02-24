## Chronos V2.0 下载安装指南

### 下载

Chronos 是架设在 Unix-like 平台上，当前在 GNU/Linux 平台测试通过。

下载的包包括：

* **chronos-2.0.tgz**：WCET 分析引擎的源代码，一个图形化前端，以及一组测试程序。分析测试程序和处理器模型，分析引擎得出一个整数线性规划问题（ILP），最终可以解出 WCET。
* **lp_solve_5.5.tgz**：一个免费的线性规划问题解决器，我们用它通过解分析引擎产生的 ILP 问题来获得最终的 WCET 的测评结果。
* **simplesim-3.0.tgz**：这是一个修改 SimpleScalar 源代码之后得到的一个模拟器。我们这里么提供它是因为当前 Chronos 是以简单的 SimpleScalar 体系结构建模的。
* **gcc-2.7.2.3.tgz**：这是经过修改之后的 SimpleScalar 发行版的 GCC 编译器，用来将测试程序编译成 SimpleScalar ISA 的二进制文件。
* **simpleutils-990811.tar.gz**：包含在 SimpleScalar 平台上的 GNU 工具包。
* **simpletools-2v0.tgz**：包含编译 SimpleScalar 二进制文件所需要的库。

### 安装

假设所有下载的包都放进 $IDIR 目录，进入该目录，按照下面步骤进行安装：

**分析器** 首先用下面的命令解压缩 chronos-2.0.tgz 包：

	tar -xvzf chronos-2.0.tgz

会得到三个子目录：**est** —— 分析引擎；**gui** —— 图形前端；**benchmarks** —— 测试程序集。下面你需要编译分析引擎，该分析引擎是用 ANSI C 编写的，键入下面命令进行编译：

	cd est && make

这会在 est 目录下生成可执行文件 **est**，这就是 WCET 的分析器。

图形化前端使用 Java 语言来编写的，你需要安装 JRE，你可以从下面的链接下载 JRE5.0:

	http://java.sun.com/j2se/1.5.0/download.jsp

在下载完 **jre-1_5_0_<version>-linux-586.bin** 文件之后，运行下面的命令：

	chmod +x jre-1_5_0_<version>-linux-586.bin
	./jre-1_5_0_<version>-linux-586.bin

在你安装 JRE 之后，将 JRE 的 bin 目录添加到系统路径中，确保你在之后可以使用 **java** 命令，而且是你刚下在的那个，你可以使用 java -version 来检查你的 java 版本。

前端也包括一些 C 的代码，为了汇编二进制文件进行数据流分析，以及一些汇编代码。使用下面命令进行编译：

	cd gui && make

将会在 gui 目录下生存 **dis** 可执行文件。


**LP 解决器**  用下面命令解压 **lp_solve_5.5.tgz** 文件：

	tar -xvzf lp_solve_5.5.tgz

生成三个包：

* lp_solve_5.5_exe.tar.gz：Linux 平台预编译文件。解压这个包，得到一个 lp_solve 目录，包含可执行文件 lp_solve 和 lib_xli_CPLEX.so 库。你需要通过 Chronos 的用户接口告诉它 lp_solve 目录的路径。如果预编译文件在我们的机器可以工作，你就无需用下面的两个包来编译 lp_solve 了。
* lp_solve_5.5_source.tar.gz
* lp_solve_5.5_xli_CPLEX_source.tar.gz


**SimpleScalar 模拟器**  为了安装 SimpleScalar 模拟器，首先解压 **simplesim-3.0.tgz**，生成一个子目录 **simplesim-3.0**，用下面的命令来编译：

	cd simplesim-3.0 && make

**SimpleScalar GCC 和相关的二进制工具**  首先我们需要安装二进制工具，用来编译 SimpleScalar 交叉编译器 GCC。之后，安装 SimpleScalar GCC。步骤如下：

1. 编译 SimpleScalar 二进制工具：

	tar -xvzf simpleutils-990811.tar.gz
	tar -xvzf simpletools-2v0.tar.gz

然后进入 simpleutils-990811 子目录，

	./configure --host=i386-*-linux --target=sslittle-na-sstrix
	--with-gnu-as --with-gnu-ld --prefix=$IDIR

注意，上面的命令仅在 Intel/x86 平台下运行 Linux 系统时有用。

	make install

会生成二进制工具，安装在 $IDIR/sslittle-na-sstrix/bin 目录下。

2. 编译 SimpleScalar GCC：

	export PATH=$PATH:$IDIR/sslittle-na-sstrix/bin

这步非常重要！它保证下面的交叉编译能找到以 SimpleScalar 为目标的二进制工具的正确的路径，而不是使用原生的 GNU 工具包。

	tar -xvzf gcc-2.7.2.3.tgz
	cd $IDIR/gcc-2.7.2.3

	./configure --host=i386-*-linux --target=sslittle-na-sstrix
	--with-gnu-as --with-gnu-ld --prefix=$IDIR --enable-languages=c

	make
	make install























