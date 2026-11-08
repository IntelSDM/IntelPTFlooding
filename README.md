# IntelPT Flooding Pass
Built for LLVM 19.1 - Windows x64 <br> <br>

[Intel Processor Trace](https://easyperf.net/blog/2019/08/23/Intel-Processor-Trace)(Intel PT) Is a hardware debugging tool that allows you to trace program execution, spitting out the instructions that execute. <br>
This is very common in hypervisor based analysis as it lets you trace what executes and what doesn't, allowing you to effectively remove opaque code and junkcode. <br>
The issue is it writes directly into a fixed size buffer, the behaviour of this buffer depends on the implementation, it will either fill up and stop collecting, fill up and overwrite old code, fill up and overwrite new code. <br>
Eitherway, if you can fill it up with junk, you break the tooling.

## The Attack
We insert a simple jump table before every return in the function. If a function doesn't return, then it wont work. <br>
This jumptable is a threaded table so it will need a new index and meet a new instruction each time. Allowing us to fill the buffer up. <br>
Each Intel PT implementation has a different buffer size limitation. Most start causing instabilities after 32mb per core, So we emit a 32mb jumptable (32mb of tracing, not 32mb of memory) <br>
This can be changed in /PTFloodPass.cpp - FloodIterations  <br>
This means after the function executes, all its data will be flooded out of the buffer. <br>
I have tested this to great success on HyperDBG's Intel PT. <br>
When active, this will make your function hang for a second on return. Only use it on sensitive/none time sensitive code.
<img width="799" height="498" alt="image" src="https://github.com/user-attachments/assets/4c52eae5-6856-49a6-96f8-3e77550232b4" />

## Annotations
```c
__attribute__((annotate("ptflood")))
``` 
* Applies flooding before function return

## How To Build
* Download [LLVM 19.1](https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.0)
* Download [VCPKG](https://github.com/microsoft/vcpkg)
* VCPKG install asmjit
* Open build.bat in a text editor.
* Find the LLVM 19.1 references in the command line, Change them to your file path.
* Find the VCPKG references in the command line, Change them to your file path.
* Run the build script
* Build your application with the ``` -load-pass-plugin ``` Param to the FunctionPass.dll
