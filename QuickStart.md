# Quick Start #

If you don't have the time or patience to watch the presentation or read this manual, follow the steps below to have _Dementia_ up and running as quickly as possible.

  1. **Prerequisites** - _Dementia_ needs Internet access during its **first start** in order to download necessary kernel symbols (i.e. PDB files) from the Microsoft Symbol Server. Necessary DLLs (`dbghelp.dll` and `symsrv.dll`) are included in the download archive, but it is strongly recommended to obtain new versions of the mentioned DLLs (these DLLs are a part of  [Debugging Tools for Windows](http://msdn.microsoft.com/en-us/windows/hardware/gg463009.aspx) package). Please be patient while the symbols are being downloaded, since the symbols can be rather large (for example, about 7 MB for Windows 7).
  1. **Download appropriate release** - depending on the architecture you are using (32-bit or 64-bit), choose appropriate release for download from the [Downloads](http://code.google.com/p/dementia-forensics/downloads/list) page.
  1. **Extract the archive contents** - after downloading the appropriate release, contents of the downloaded archive must be extracted to a directory of your choosing.
  1. **Run _Dementia_** - this is the most involving step. _Dementia_ has three available hiding methods:
    * **User-mode hiding** (only _Memoryze_ is currently supported)
    * **Kernel-mode hiding based on hooks**
    * **Kernel-mode hiding based on file system mini-filter driver**
> > All three methods are supported on 32-bit systems, while on 64-bit architecture only the last method (file system mini-filter) is supported.
> > Running `Dementia.exe` without parameters will list all available options:
```
Dementia - v1.0 -- Windows memory anti-forensics suite
Copyright (c) 2012, Luka Milkovic <milkovic.luka at gmail.com> or <luka.milkovic at infigo.hr>

Usage: Dementia.exe [-h|--help -d|--debug -f|--file] -m <evasion_method> [-a|--args]
General options:
  -h [ --help ]         view program description
  -d [ --debug ]        print verbose/detailed program output - useful for
                        debugging
  -f [ --file ]         write all program output to "log.txt" file
  -m [ --method ] arg   Evasion method. Following methods are supported
                        (specify wanted method by number):

                        1 - Memoryze usermode evasion: Hides kernel level objects - process block, threads and network connections from the usermode
                        2 - Generic kernel-mode evasion module: Hides kernel level objects related to target process using kernel driver
                        3 - Generic kernel-mode evasion module: Hides kernel level objects related to target process using kernel driver

  -a [ --args ] arg     pass specific arguments to the evasion method (use info for method specific help) -- arguments are passed in quotes!
```
> > Additional help for a given method can be obtained by issuing the `-i` argument for the given method.
> > For example, if the process with the name `calc.exe` must be hidden using second method (kernel module with hooks), this command line must be used:
```
  Dementia.exe -m 2 -a "-P calc.exe"
```
> > Multiple "objects" (i.e. processes or drivers) can be specified on the command line. For example, command line below shows how to hide process with pid 1234 and driver `HTTP.sys` in addition to `calc.exe` process:
```
  Dementia.exe -m 2 -a "-P calc.exe -p 1234 -D HTTP.sys"
```
      * **Note for file system mini-filter driver (method 3)** - the driver must first be installed by using the `inf` file (_right mouse click->Install_) and then issuing the following command:
```
  net start dementiafs
```
> > > This step is not necessary for 32-bit driver (method 2) because the driver is automatically installed (and removed) by the application.
      * **Note for 64-bit** - since the file system mini-filter is not signed, you must [enable test-signing](http://msdn.microsoft.com/en-us/library/windows/hardware/ff553484(v=vs.85).aspx) or choose [Disable Driver Signature Enforcement](http://www.raymond.cc/blog/loading-unsigned-drivers-in-windows-7-and-vista-64-bit-x64/) option when booting Windows.
  1. **Run acquisition software** - run memory acquisition software of your choosing. List of memory acquisition software which is supported by _Dementia_ (i.e. whose memory dump can be modified) is available under [Features](Features.md). While acquisition software is running, you can see _Dementia_ debug output in [DebugView](http://technet.microsoft.com/en-us/sysinternals/bb896647.aspx) or in the attached kernel debugger (if methods 2 or 3 are used), or inside the command window where _Dementia_ was launched (if method 1 is used).
    * **Note for file system mini-filter driver (method 3)** - _Dementia_ must be manually killed (`Ctrl+C`) and the driver must be stopped using the following command before analyzing the image:
```
  net stop dementiafs
```
> > > This step is not necessary for 32-bit driver (method 2) because the driver is automatically removed by the application.
  1. **Analyze the dump** - after acquisition process has finished, the output image can be analyzed using any memory analysis application. All specified artifacts should be hidden from the dump.