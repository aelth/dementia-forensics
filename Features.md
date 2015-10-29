# Features #

## Architectures ##
  * 32-bit
  * 64-bit - **note:** 64-bit support has not been tested as widely as 32-bit, so bugs should be expected

## Operating systems ##
_Dementia_ has been tested and known to work on the following operating systems:

  * Microsoft Windows XP
    * without SP
    * SP1
    * SP2
    * SP3
  * Microsoft Windows Vista
    * without SP
    * SP1
  * Microsoft Windows 7
    * without SP

## Memory aquisition applications ##
_Dementia_ is able to hide artifacts inside the memory image produced by the following memory acquisition applications:

  * [Mandiant Memoryze](http://www.mandiant.com/resources/download/memoryze)
  * [Winpmem](http://code.google.com/p/volatility/)
  * [Mantech MDD](http://sourceforge.net/projects/mdd/)
  * [Moonsols Windows Memory Toolkit](http://www.moonsols.com/products/)
  * [FTK Imager](http://www.accessdata.com/support/product-downloads)

## Dump formats ##
  * Raw dump format
  * Crash dump format (as created by Moonsols Win32dd)

## Hiding methods and supported artifacts ##
  * User-mode hiding method (supports Mandiant Memoryze only)
    * Process hiding
      * `Pro` allocation deletion
      * `_EPROCESS` unlinking from the list of active processes
    * Thread hiding
      * `Thr` allocation deletion
      * `_ETHREAD` unlinking from the list of active threads
    * Connection hiding
      * `_TCP_LISTENER` allocation deletion
      * `_TCP_ENDPOINT` allocation deletion
      * `_UDP_ENDPOINT` allocation deletion
  * Kernel-mode hiding based on hooks
  * Kernel-mode hiding based on file system mini-filter driver
    * Process hiding
      * `Pro` allocation deletion
      * `_EPROCESS` unlinking from the list of active processes
      * `_EPROCESS` unlinking from the appropriate session list
    * Thread hiding
      * `Thr` allocation deletion
    * Memory allocations hiding
      * `Vad`/`VadS`/`VadM` allocation deletion
      * Deletion of the entire memory region if it is private for the target process or if it represents process EXE image
      * Deletion of the entire memory region if it is a shared section which is opened exclusively by the target process
        * Deletion of mapped files, if used (`Fil` allocation deletion)
    * Handle hiding
      * `Obtb` allocation deletion (process handle table)
      * `_HANDLE_TABLE` unlinking from the list of handle tables
      * Deletion of handles/objects opened exclusively by the target object
        * `_HANDLE_TABLE_ENTRY` deletion
        * Object allocation deletion
      * Decrementing handle counters for objects not opened exclusively by the target process
        * `_HANDLE_TABLE_ENTRY` deletion
      * Removing thread handle from the `PspCidTable`
      * Removing process handle from the `PspCidTable`
      * Removing process handle from the csrss.exe handle table
    * File objects hiding
      * `Fil` allocation deletion
    * Driver hiding
      * `MmLd` allocation deletion (`_LDR_DATA_TABLE_ENTRY`)
      * `LDR_DATA_TABLE_ENTRY` unlinking from the loaded modules list
      * Deletion of driver image in memory