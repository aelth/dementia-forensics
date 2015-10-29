# Caveat #
Unfortunately, current code structure is not very nice and it has not gone a single refactoring since the start of the project. All classes, source files and headers were added without any kind of organization or ease of automated builds in mind.

Code and code structure is completely Visual Studio-oriented and it could be hard to build it with other build system. I haven't tried it, so your mileage may vary.

# Prerequisites #
In order to build Dementia, following packages are needed:

  1. [WDK](http://msdn.microsoft.com/en-us/library/windows/hardware/gg487428.aspx)
> > To be honest, I'm not quite sure whether it will work with new WDK out of the box. It should work, but since I'm using rather ancient DDK version and didn't have the time to migrate, it might or might not work (let me know).
  1. [Boost libraries](http://www.boost.org/)
> > Any version will do, I have used 1.51. Both headers and libraries are needed.

Additionally, packages below are not necessary, but are highly recomended:

  1. Microsoft Visual Studio
  1. [DDKBuild](http://www.hollistech.com/Resources/ddkbuild/ddkbuild.htm)
> > DDKBuild is not needed on newer versions of Visual Studio (since they support driver development), but can be very useful if you are using older VS version like me.

# Environment variables #
Three environment variables must be defined:
  1. **WLHBASE** - base of your DDK/WDK directory
  1. **BOOST** - base of your Boost directory
  1. **DDKBUILD\_PATH** - path to DDKBuild. This is not necessary if DDKBuild is not used, but then make sure you properly import DementiaFS and DementiaKM projects.

# Code structure #
Code is organized into 4 projects:

  1. **Dementia** - user mode component and "controller" of all other components
  1. **DementiaFS** - file system mini-filter driver
  1. **DementiaKM** - hooking driver and main hiding driver
  1. **MemoryzeUM** - user-mode hiding DLL injected into Memoryze process
  1. **SymbolHelper** - DLL in charge for Microsoft debugging symbols download and parsing

If you satisfy all prerequisites and have version of Visual Studio newer than 2008, you should have little or no problems building the code.