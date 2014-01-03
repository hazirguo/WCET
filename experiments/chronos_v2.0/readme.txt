This is the second release (v2.0) of Chronos, a prototype tool
performing Worst Case Execution Time (WCET) analysis for
embedded software. It is targeted for  x86/Linux platform and it
has been tested on Redhat Fedora Core 3.0 and 4.0. 

Chronos needs some third party software packages to work with
it, in particular, a free linear programming solver, lp_solve
(version 5.5 or above), the SimpleScalar toolset, and Java
Runtime Environment (JRE, version 1.5 or above). Here are some
additional notes about these third party tools:

1) The binary of lp_solve is provided along with the Chronos
package. So the user need not download it from its original
site, unless the binary does not work on his/her platform.

2) The SimpleScalar toolset consists of a few components, some
of which have been modified and are provided together with the
Chronos package. The provided components include: 

- simplesim-3.0.tgz: the SimpleScalar simulator, and

- gcc-2.7.2.3.ss.tar.gz: a SimpleScalar ported GCC compiler that
compiles C source code into PISA binaries. 

More details can be found from SimpleScalar website:
  http://www.simplescalar.com.
The user of Chronos needs to download from SimpleScalar website the remaining two components:

- simpleeutils-990811.tar.gz: the SimpleScalar ported GNU binutils

- simpletools-2v0.tgz: a SimpleScalar ported GCC and other
source files needed for compilation (but the GCC in this package
has been replaced with the newer version gcc-2.7.2.3 coming with
Chronos).

3) Jave Runtime Environment 1.5 can be obtained from
http://java.sun.com/j2se/1.5.0/download.jsp

Detailed installation and usage instruction of Chronos 2.0 can
be obtained from the user manual available at Chronos website:
http://www.comp.nus.edu.sg/~rpembed/chronos
The manual also contains information about internal workings of
Chronos 2.0, including its workflow and the structure of the
source code.
