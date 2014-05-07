# Chronos 工作流

## 预分析

在 Chronos 分析器工作之前，需要准备的工作有：

* 前端加载 **benchmark** 程序，调用 SimpleScalar GCC 进行编译成二进制文件。
* 用户利用前端可以进行配置分析器的行为和处理器特性。

在此之后，调用分析器 **est**。

## 分析

现在，分析器引擎接管工作：

### 1. 路径分析
分析器读入二进制代码，为 benchmark 中每个过程重建控制流图（CFG），CFG 被保存到 benchmark 相同路径下的一 benchmark.cfg 命名的文件。在每个独立的 CFG 构建之后，调用内部分析程序构建全局的控制流图，称之为 转化的CFG（tcfg.h 中的 	`struct tcfg_t`）。接下来所有的分析都建立在转化的CFG之上，而不是每个独立的过程CFG上。

这一步对应的是流图中的 **path analysis**，实现如下：

```
path_analysis() {
	read_code();		//从目标文件中读入程序代码
	build_cfgs();		//为每个过程构建CFG（数据结构为 cfg_node_t）
	prog_tran();		//转化的CFG（tcfg_node_t）
	loop_process();		//
}
```

路径分析还会对数据流进行分析，以发现流事实，例如循环边界、不可达路径等。分析的结果作为函数限制的集合。


### 2. 分支预测分析

分支预测捕获分支的错误预测。这是微处理建模的一部分，实现如下：

```
bpred_analysis() {
	collect_mp_insts();		//
	build_bfg();
	build_btg();
}
```

### 3. 指令 cache 分析


### 4. 流水线分析


### 5. 建立 IPL 问题

为 benchmark 程序的 WCET 分析建立 ILP 问题，主要基于：（1）路径分析（2）微处理器建模（3）用户提供函数限制。ILP 问题最终是以 ILOG/CPLEX 格式写入文件 benchmark.lp 中。主要的函数包括：

* cost_func()  产生 ILP 问题的目标函数
* tcfg_cons()  从之前生成的转化的CFG 中产生流限制
* bfg_cons()、tcfg_bfg_cons()、btg_cons() 产生分支预测相关的限制。
* cache_cons()、mp_cache_cons() 
* tcfg_estunit_cons()
* user_cons()



## 分析后

分析引擎的主要作用是，路径分析和微处理器建模，然后将 WCET 问题的分析转化为 ILP 问题求解。对 ILP 问题的求解依赖于第三方 IP/ILP 求解器。因为 ILP 文件的格式是以 ILOG/CPLEX 形式的，这种格式商用的 CPLEX_ILP 求解器和开源的 lp_solve LP 求解器都使用的。

 
 
