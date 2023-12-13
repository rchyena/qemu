This file serves as a brief introduction to QEMU tracing.

QEMU provides the ability to trace the backend functions and behavior of any device, useful in observing execution and debugging. You can think of these as print statements that QEMU manages for you. The steps to perform tracing are briefly outlined below.

# 1. Edit "trace-events" File
Many of the directories in qemu have local "trace-event" files (e.g. `hw/arm/trace-event`, `hw/core/trace-event`, etc) that contain a list of functions to trace. A function to trace can look like the following:

```
smmuv3_trigger_irq(int irq) "irq=%d"
```

Any of the functions (within the files in the same directory as this "trace-event" file) that you'd like to trace can be placed here. You tell the function what input you're expecting to trace and how you'd like to output the information. Here, we're expecting the trace function to receive an interrupt request (irq) of type int, and to print it out with its value.

When you build QEMU, all of the trace-events files will be concatenated together into a single, large trace-events-all file within `qemu/build/trace`.

# 2. Place Trace Function Calls in QEMU Source
In order to trigger your trace function, you must add a call to your tracing function within the QEMU source. For example, say you want to trace a device reset. QEMU resets are (in part) handled by `qemu/hw/core/resettable.c`. One of the functions here is `resettable_phase_enter`, which begins the process of resetting the device. Tracing this would mean adding - within this function - a call to a function of the same name prepended with "trace_" (so `trace\_resettable\_phase\_enter` in this case), and passing in whatever variables in this function you'd like to print out in your trace. For example:

```
trace_resettable_phase_enter_begin(obj, obj_typename, s->count, type);
```

Also, make sure to add `#include "trace.h"` to the file's includes.

# 3. Configure and Build QEMU
Configure QEMU with your target architecture and tracing enabled, then build. For example:

```
cd qemu/build
../configure --target-list=arm-softmmu --enable-trace-backends=simple
make
```

# 4. Run QEMU
Run QEMU. For example:

```
`./qemu-system-arm -M upsat-comm -kernel ../../upsat-comms-software/Debug/upsat-comms-software.elf -monitor stdio --trace resettable_phase_enter_begin`.
```

The `--trace` flag can be given a function to trace, as shown above, or a file within functions on each new line.

# 5. Decode Trace File
The trace will produce a "trace-X" file (where X is some number) within the same directory you ran QEMU from. This file is binary formatted, but can be passed into QEMU's `simpletrace.py` script for decoding. For example:

```
cd qemu/build
python3 ../scripts/simpletrace.py trace/trace-events-all <trace file>
```

# TLDR
- Add function definitions to trace in local `trace-events` file.
- Add tracing call within function in QEMU source.
- Configure and build QEMU.
- Run QEMU: `./qemu-system-arm -M upsat-comm -kernel ../../upsat-comms-software/Debug/upsat-comms-software.elf -monitor stdio --trace <function name | file with functions names to trace>`.
- Decode trace script: `python3 ../scripts/simpletrace.py trace/trace-events-all <trace file>`.


For more in-depth information, refer to the following:
- [QEMU Docs](https://qemu-project.gitlab.io/qemu/devel/tracing.html)
- [Helpful Blog Post](https://michael2012z.medium.com/tracing-in-qemu-8df4e4beaf1b)
