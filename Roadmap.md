# Introduction #

Dementia was developed out of curiosity, not because of necessity.
Dementia does not have a specific direction or guidelines - when enhancing existing and developing new features I would like to gain new knowledge and understand particular concepts. This usually means that newly developed features will not be the most necessary ones:)

That being said, I do realize that Dementia has a lot of limitations and opportunities for improvement.

# Hiding methods roadmap #

Kernel level hiding methods will be improved in the future. I have received a lot of feedback and heard a lot of great ideas (P.S. a guy from Italy asked some very interesting questions and gave a lot of ideas during 29c3, but I forgot his name:( Please, if you are reading this, contact me!).
As a general rule, I would like to "dig deeper" with 32-bit module and leave "shallow" hooks like `NtWriteFile()`. I'm interested in MmCrashPte, crashdump callbacks and other low level details...
64-bit module will probably stay as is in terms of hiding methodology (file system mini-filter) because there is not much to be done in 64-bit kernels.

User mode module is not so interesting to me. It has served well as a proof of concept, but I'm not sure whether I'll invest some time in new features.

# Artifact hiding roadmap #

There are still a lot of objects that need to be hidden:

  1. registry keys and values
  1. connections
  1. DLLs
  1. better hiding of drivers
  1. API and other hooks hiding
  1. kernel callback hiding
  1. self-hiding capabilities (not sure if I want to develop this...)
  1. ...