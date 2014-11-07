Getting started
===============
:Authors: Jesper Dangaard Brouer <netoptimizer@brouer.com>

Here is a quick starting guide, that ends showing the output from the
`time_bench_sample` kernel module.  I would like to know the
micro-benchmark performance of different CPUs, thus feel free to email
me the output after completing this exercise ;-)

Download source::

 $ git clone https://github.com/netoptimizer/prototype-kernel.git

Try to compile modules (you need RPM kernel-devel or a local kernel
source tree avail)::

 $ cd prototype-kernel/kernel
 $ make

After first make, run script to symlink kernel modules into your local
kernel module tree::

 $ dirs
 ~/git/prototype-kernel/kernel

 $ sudo ./scripts/setup_prototype_devel_env.sh
 [output shortened:]
 Symlink all kernel modules
 './time_bench_sample.ko' -> 'prototype-kernel/kernel/lib/time_bench_sample.ko'
 [...]
 Depmod - update kernel module symbols (on hostname)

Now you can load the kernel modules, and load dependencies also
work. Run the setup script again if you have added a new module.

Lets look at module `time_bench_sample`::

 $ modinfo time_bench_sample
 filename:       /lib/modules/3.16.4-200.fc20.x86_64/kernel/extra/time_bench_sample.ko
 license:        GPL
 author:         Jesper Dangaard Brouer <netoptimizer@brouer.com>
 description:    Sample: Benchmarking code execution time in kernel
 depends:        time_bench
 vermagic:       3.16.4-200.fc20.x86_64 SMP mod_unload

The `time_bench_sample` module simply prints its results to the kernel
log.  I usually "run-it" like this::

 $ make && \
   (sudo rmmod time_bench_sample; sudo modprobe time_bench_sample) \
   && sudo dmesg -c

On my laptop CPU i7-2620M the output looks like this.  I've cleaned it
up by adding line-breaks before "Type" and "-" ::

 [1523779.968385] time_bench_sample: Unloaded

 [1523779.975410] time_bench_sample: Loaded

 [1523780.298424] time_bench:
 Type:for_loop Per elem: 0 cycles(tsc) 0.322 ns (step:0)
 - (measurement period time:0.322574148 sec time_interval:322574148)
 - (invoke count:1000000000 tsc_interval:868128306)

 [1523781.558964] time_bench:
 Type:spin_lock_unlock Per elem: 33 cycles(tsc) 12.588 ns (step:0)
 - (measurement period time:1.258850730 sec time_interval:1258850730)
 - (invoke count:100000000 tsc_interval:3387886371)

 [1523782.240786] time_bench:
 Type:local_BH_disable_enable Per elem: 18 cycles(tsc) 6.809 ns (step:0)
 - (measurement period time:0.680904887 sec time_interval:680904887)
 - (invoke count:100000000 tsc_interval:1832487465)

 [1523782.523465] time_bench:
 Type:local_IRQ_disable_enable Per elem: 7 cycles(tsc) 2.822 ns (step:0)
 - (measurement period time:0.282294042 sec time_interval:282294042)
 - (invoke count:100000000 tsc_interval:759724455)

 [1523783.885408] time_bench:
 Type:local_irq_save_restore Per elem: 36 cycles(tsc) 13.601 ns (step:0)
 - (measurement period time:1.360118333 sec time_interval:1360118333)
 - (invoke count:100000000 tsc_interval:3660422754)

 [1523783.918188] time_bench:
 Type:preempt_disable_enable Per elem: 0 cycles(tsc) 0.327 ns (step:0)
 - (measurement period time:0.032728667 sec time_interval:32728667)
 - (invoke count:100000000 tsc_interval:88080549)

 [1523784.139055] time_bench:
 Type:funcion_call_cost Per elem: 5 cycles(tsc) 2.205 ns (step:0)
 - (measurement period time:0.220564571 sec time_interval:220564571)
 - (invoke count:100000000 tsc_interval:593594688)

 [1523784.325287] time_bench:
 Type:func_ptr_call_cost Per elem: 5 cycles(tsc) 1.859 ns (step:0)
 - (measurement period time:0.185976490 sec time_interval:185976490)
 - (invoke count:100000000 tsc_interval:500509440)

 [1523784.483675] time_bench:
 Type:page_alloc_put Per elem: 425 cycles(tsc) 158.170 ns (step:0)
 - (measurement period time:0.158170395 sec time_interval:158170395)
 - (invoke count:1000000 tsc_interval:425676012)

