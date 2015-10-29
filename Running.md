# Running Dementia #

This page describes basic command line options and arguments used by _Dementia_, as well as the supported hiding methods.

## Hiding methods ##

_Dementia_ currently supports three hiding methods.
#### User-mode hiding ####
Most memory acquisition applications (for example, Mandiant Memoryze and FTK Imager) use its driver as a "proxy" for kernel access. Driver maps physical memory and sends the contents back to the user-mode.
Because of the approach described above, potential attacker can perform DLL injection into a memory acquisition applications' process and hook `DeviceIoControl()` or other interesting functions.

This hiding method is currently supported only for Mandiant Memoryze and works by injecting a DLL into `Memoryze.exe` process, making an IAT hook of `DeviceIoControl()` function and clearing the contents of the dump on-the-fly.

#### Kernel-mode hiding based on hooks ####
This method works by installing a kernel module (driver) which performs inline hooking of `NtWriteFile` and `NtClose` functions. After detecting memory acquisition software based on various heuristics (see presentation), it builds a list of all addresses and ranges that need to be hidden. When a buffer containing "tagged" address is being written to a memory dump, driver sanitizes the buffer and removes the artifacts from the dump.

This method of hiding is supported only on 32-bit systems, because inline hooking of kernel functions is not supported on 64-bit systems, due to additional kernel level protections (Patchguard).

#### Kernel-mode hiding based on file system mini-filter driver ####
Since hooking is not allowed in 64-bit kernels, additional driver was created for 64-bit systems. This driver is a functional equivalent of the previous method, but instead of placing hooks, it works as a file system mini-filter driver, sanitizing the buffer as it is being written to disk.

## Basic usage ##
Running `Dementia.exe` without parameters will list all available options:
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

General command line is shown below:

```
Dementia.exe -m <method> -a "<arguments>"
```

where `<method>` is:
  * 1 - user-mode hiding
  * 2 - kernel mode hiding with hooks
  * 3 - kernel mode hiding with file system mini-filter

and `<arguments>` depend on the method being used.
For listing all available option for a given method, use `-i` option. For example, all available options for method 2 are listed below:

```
Dementia.exe -m 2 -a "-i"
Dementia - v1.0 -- Windows memory anti-forensics suite
Copyright (c) 2012, Luka Milkovic <milkovic.luka@gmail.com> or <luka.milkovic@infigo.hr>

Module: Generic kernel-mode evasion module

Arguments:      -i|--info - this information
                -P|--process-name - name of the process whose artifacts (EPROCESS block etc.) should be hidden
                -p|--process-id - ID of the process whose artifacts (EPROCESS block etc.) should be hidden
                -D|--driver-name - name of the driver that should be hidden, along with the additional artifacts
                --no-unload - by default, driver will be unloaded after hiding. This flag indicates that the driver will not be unloaded and will remain active after this program exits.
                --no-threads-hide - by default, all threads of the target process are hidden. This flag indicates no thread hiding
                --no-handles-hide - by default, all handles/objects uniquely opened within the target process are hidden. This flag indicates no handle hiding
                --no-image-hide - by default, file object representing the process image is hidden. This flag indicates no image file hiding
                --no-vad-hide - by default, private memory ranges of the target process are deleted. This flag indicates no hiding of process private memory ranges
                --no-job-hide - by default, if target process belongs to a job, it is removed from the job. Job is removed if no processes are left. This flag indicates no job hiding
                -d|--driver - name of the kernel-mode driver (default: DementiaKM.sys)


Description: This evasion method uses kernel driver in order to hide valuable artifacts and evade analysis. The method works by hooking NtWriteFile() function inside the kernel and modify obtained buffers (i.e. conceal presence of arbitrary objects)
```

Arguments above are self-explanatory and define behaviour of the hiding algorithm, i.e. whether certain artifacts will be hidden or left unmodified in the memory dump.

For example, if the process with the name `calc.exe` must be hidden using second method (kernel module with hooks), this command line must be used:
```
  Dementia.exe -m 2 -a "-P calc.exe"
```
Multiple "objects" (i.e. processes or drivers) can be specified on the command line. For example, command line below shows how to hide process with pid 1234 and driver `HTTP.sys` in addition to `calc.exe` process:
```
  Dementia.exe -m 2 -a "-P calc.exe -p 1234 -D HTTP.sys"
```

**Note for file system mini-filter driver (method 3)** - **before running _Dementia_**, the driver must first be installed by using the `inf` file (_right mouse click->Install_) and then issuing the following command:
```
  net start dementiafs
```
This step is not necessary for 32-bit driver (method 2) because the driver is automatically installed by the application.

Also, since the file system mini-filter is not signed, you must [enable test-signing](http://msdn.microsoft.com/en-us/library/windows/hardware/ff553484(v=vs.85).aspx) or choose [Disable Driver Signature Enforcement](http://www.raymond.cc/blog/loading-unsigned-drivers-in-windows-7-and-vista-64-bit-x64/) option when booting Windows.

After running _Dementia_, memory acquisition software can be started and _Dementia_ will try to sanitize the dump as it is being written to disk. While acquisition software is running, you can see _Dementia_ debug output in [DebugView](http://technet.microsoft.com/en-us/sysinternals/bb896647.aspx) or in the attached kernel debugger (if methods 2 or 3 are used), or inside the command window where _Dementia_ was launched (if method 1 is used).

When acquisition software finishes with the acquisition process, _Dementia_ will automatically unload the driver and terminate if method 2 is used. Otherwise, _Dementia_ must be manually killed (`Ctrl+C`), and, if the method 3 is used, the driver must be stopped using the following command before analyzing the image:
```
  net stop dementiafs
```
This method ensures that the memory analysis software is not being interpreted as an acquisition tool - _Dementia_ would try to perform hiding again, which can interfere with the analysis process.