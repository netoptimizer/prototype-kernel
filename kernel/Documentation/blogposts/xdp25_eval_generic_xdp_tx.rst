===============================
Eval Generic netstack XDP patch
===============================
:Authors: Jesper Dangaard Brouer
:Version: 1.0.0
:Date: 2017-04-24 Mon

Given XDP works at the driver level, developing and testing XDP
programs requires access to specific NIC hardware.  (XDP support have
been added to KVM via the virtio_net driver, but unfortunately it is a
hassle to configure (given it requires disabling specific options,
which are default enabled)).

To ease developing and testing XDP programs, a generic netstack-XDP
patch proposal (`PATCH V3`_ and `PATCH V4`_) have been posted.  This
allow for attaching XDP programs to any net_device.  If the driver
doesn't support native XDP, the XDP eBPF program gets attached further
inside the network stack.  This is obviously slower and loses the XDP
benefit of skipping the SKB allocation.

The generic netstack-XDP patchset is **NOT** targetted high
performance, but instead for making it easier to test and develop XDP
programs.

That said, this does provide an excellent opportunity for comparing
performance between NIC-level-XDP and netstack-XDP.  This provides the
ability to do what I call zoom-in-benchmarking of the network stack
facilities, that the NIC-XDP programs avoid. Thus, allowing us to
quantify the cost of these facilities.

.. _`PATCH V3`:
   http://lkml.kernel.org/r/20170413.120925.2082322246776478766.davem@davemloft.net

.. _`PATCH v4`:
   http://lkml.kernel.org/r/20170412.145415.1441440342830198148.davem@davemloft.net

Benchmark program
=================

The XDP program used is called: ``xdp_bench01_mem_access_cost`` and is
available in the prototype kernel `samples/bpf`_ directory as
`xdp_bench01_mem_access_cost_kern.c`_ and `_user.c`_.

.. _`samples/bpf`:
   https://github.com/netoptimizer/prototype-kernel/tree/master/kernel/samples/bpf

.. _`xdp_bench01_mem_access_cost_kern.c`:
   https://github.com/netoptimizer/prototype-kernel/blob/master/kernel/samples/bpf/xdp_bench01_mem_access_cost_kern.c

.. _`_user.c`:
   https://github.com/netoptimizer/prototype-kernel/blob/master/kernel/samples/bpf/xdp_bench01_mem_access_cost_user.c

Baseline testing with NIC-level XDP
===================================

First establish a baseline for the performance of NIC-level XDP.  This
will serve as baseline against the patch being evaluated.  The packet
generator machine is running pktgen_sample03_burst_single_flow.sh,
which implies these tests are single CPU RX performance, as the UDP
flow will hit a single hardware RX-queue, and thus only activate a
single CPU.

Baseline with mlx5 on a Skylake CPU:
Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz.

Network card (NIC) hardware: NIC: ConnectX-4 Dual 100Gbit/s, driver
mlx5.  Machines connected back-to-back with Ethernet-flow control
disabled.

Dropping packet without touching the packet data (thus avoiding
cache-miss) have a huge effect on my system.  HW indicate via PMU
counter LLC-load-misses that DDIO is working on my system, but the
L3-to-L1 cache-line miss is causing the CPU to stall::

 [jbrouer@skylake prototype-kernel]$
  sudo ./xdp_bench01_mem_access_cost --action XDP_DROP --dev mlx5p2
 XDP_action   pps        pps-human-readable mem      
 XDP_DROP     19851067   19,851,067         no_touch 
 XDP_DROP     19803663   19,803,663         no_touch  (**used in examples**)
 XDP_DROP     19795927   19,795,927         no_touch 
 XDP_DROP     19792161   19,792,161         no_touch 
 XDP_DROP     19792109   19,792,109         no_touch 

I have previously posted patches to the mlx5 and mlx4 driver, that
prefetch packet-data into L2, and avoid this cache stall, and I can
basically achieve same result as above, even when reading data.
Mellanox have taken over these patches, but they are stalling on that
on newer E5-26xx v4 CPUs this prefetch already happens in HW.

This is a more realistic XDP_DROP senario where we touch packet data
before dropping it (causes cache miss from L3)::

 [jbrouer@skylake prototype-kernel]$
   sudo ./xdp_bench01_mem_access_cost --action XDP_DROP --dev mlx5p2 --read
 XDP_action   pps        pps-human-readable mem      
 XDP_DROP     11972515   11,972,515         read     
 XDP_DROP     12006685   12,006,685         read     (**used in examples**)
 XDP_DROP     12004640   12,004,640         read     
 XDP_DROP     11997837   11,997,837         read     
 XDP_DROP     11998538   11,998,538         read     
 ^CInterrupted: Removing XDP program on ifindex:5 device:mlx5p2

An interesting observation and take-ways from these two measurements
is that this cache-miss cost approx 32ns ((1/12006685-1/19803663)*10^9).

For the XDP_TX test to be correct, it is important to swap MAC-addrs
else the NIC HW will not transmit this to the wire (I verified this
was actually TX'ed to the wire)::

 [jbrouer@skylake prototype-kernel]$
  sudo ./xdp_bench01_mem_access_cost --action XDP_TX --dev mlx5p2 --read
 XDP_action   pps        pps-human-readable mem      
 XDP_TX       10078899   10,078,899         read     
 XDP_TX       10109107   10,109,107         read     
 XDP_TX       10107393   10,107,393         read     
 XDP_TX       10107946   10,107,946         read     
 XDP_TX       10109020   10,109,020         read     


Testing with network stack generic XDP
======================================

This test is based on `PATCH V4`_ after adjusting the patch according
to the email thread, and and validated XDP_TX can send packets on wire.

Netstack XDP_DROP
-----------------

As expected there is no difference in letting the XDP prog touch/read
packet-data vs "no_touch", because we cannot avoid touching given the
XDP/eBPF hook happens much later in the network stack. As can be seen
by these benchmarks::

 [jbrouer@skylake prototype-kernel]$
  sudo ./xdp_bench01_mem_access_cost --action XDP_DROP --dev mlx5p2
 XDP_action   pps        pps-human-readable mem      
 XDP_DROP     8438488    8,438,488          no_touch 
 XDP_DROP     8423788    8,423,788          no_touch 
 XDP_DROP     8425617    8,425,617          no_touch 
 XDP_DROP     8421396    8,421,396          no_touch 
 XDP_DROP     8432846    8,432,846          no_touch 
 ^CInterrupted: Removing XDP program on ifindex:7 device:mlx5p2

The drop numbers are good, for the netstack but some distance to the
12,006,685 pps of XDP running on in-the-NIC.  Percentage-wise it looks
big a reduction of approx 30%.  But nanosec difference is it "only"
(1/12006685*10^9)-(1/8413417*10^9) = -35.57 ns ::

 [jbrouer@skylake prototype-kernel]$
  sudo ./xdp_bench01_mem_access_cost --action XDP_DROP --dev mlx5p2 --read
 XDP_action   pps        pps-human-readable mem      
 XDP_DROP     8415835    8,415,835          read     
 XDP_DROP     8413417    8,413,417          read     
 XDP_DROP     8236525    8,236,525          read     
 XDP_DROP     8410996    8,410,996          read     
 XDP_DROP     8412015    8,412,015          read     
 ^CInterrupted: Removing XDP program on ifindex:7 device:mlx5p2

Do notice, that reaching around 8Mpps is a **very** good result for
the normal networks stack, because 100Gbit/s with large MTU size
frames (1536 bytes due to overheads) corresponds to 8,138,020 pps
((100*10^9)/(1536*8)).  The above test is with small 64bytes packets,
and the generator sending 40Mpps (can be tuned to 65Mpps).

Below perf-stat for this generic netstack-XDP_DROP test, show a high
(2.01) insn per cycle indicate that it is functioning fairly optimal,
and we likely cannot find any "magic" trick as the CPU does not seem
to be stalling on something::

 $ sudo ~/perf stat -C7 -e L1-icache-load-misses -e cycles:k \
   -e  instructions:k -e cache-misses:k -e   cache-references:k \
   -e LLC-store-misses:k -e LLC-store -e LLC-load-misses:k \
   -e  LLC-load -r 4 sleep 1

 Performance counter stats for 'CPU(s) 7' (4 runs):

       349,830  L1-icache-load-misses                  ( +-  0.53% )  (33.31%)
 3,989,134,732  cycles:k                               ( +-  0.06% )  (44.50%)
 8,016,054,916  instructions:k # 2.01  insn per cycle       (+- 0.02%) (55.62%)
    31,843,544  cache-misses:k # 17.337 % of all cache refs (+- 0.04%) (66.71%)
   183,671,576  cache-references:k                     ( +-  0.03% )  (66.71%)
     1,190,204  LLC-store-misses                       ( +-  0.29% )  (66.71%)
    17,376,723  LLC-store                              ( +-  0.04% )  (66.69%)
        55,058  LLC-load-misses                        ( +-  0.07% )  (22.19%)
     3,056,972  LLC-load                               ( +-  0.13% )  (22.19%)

Netstack XDP_TX
---------------

When testing XDP_TX it is important to verify that packets are
actually transmitted.  This is because the NIC HW can choose to drop
invalid packets, which changes the performance profile and your
results.

Generic netstack-XDP_TX verified actually hitting wire.  The slowdown
is higher than expected.  Maybe we are stalling on the
tairptr/doorbell update on TX??? ::

 [jbrouer@skylake prototype-kernel]$
  sudo ./xdp_bench01_mem_access_cost --action XDP_TX --dev mlx5p2 --read
 XDP_action   pps        pps-human-readable mem      
 XDP_TX       4577542    4,577,542          read     
 XDP_TX       4484903    4,484,903          read     
 XDP_TX       4571821    4,571,821          read     
 XDP_TX       4574512    4,574,512          read     
 XDP_TX       4574424    4,574,424          read     (**use in examples**)
 XDP_TX       4575712    4,575,712          read     
 XDP_TX       4505569    4,505,569          read     
 ^CInterrupted: Removing XDP program on ifindex:7 device:mlx5p2

Below perf-stat for generic netstack-XDP_TX, show a lower (1.51) insn
per cycle, indicate that the system is stalling on something ::

 $ sudo ~/perf stat -C7 -e L1-icache-load-misses -e cycles:k \
  -e  instructions:k -e cache-misses:k -e   cache-references:k \
  -e LLC-store-misses:k -e LLC-store -e LLC-load-misses:k \
  -e  LLC-load -r 4 sleep 1

 Performance counter stats for 'CPU(s) 7' (4 runs):

       518,261  L1-icache-load-misses        ( +-  0.58% )  (33.30%)
 3,989,223,247  cycles:k                     ( +-  0.01% )  (44.49%)
 6,017,445,820  instructions:k #  1.51  insn per cycle      (+- 0.31%) (55.62%)
    26,931,778  cache-misses:k # 10.930 % of all cache refs (+- 0.05%) (66.71%)
   246,406,110  cache-references:k           ( +-  0.19% )  (66.71%)
     1,317,850  LLC-store-misses             ( +-  2.93% )  (66.71%)
    30,028,771  LLC-store                    ( +-  0.88% )  (66.70%)
        72,146  LLC-load-misses              ( +-  0.22% )  (22.19%)
    12,426,426  LLC-load                     ( +-  2.12% )  (22.19%)

Perf details for netstack XDP_TX
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

My first though is that there is a high probability that this could be
the tairptr/doorbell update. Looking at perf report something else
lights up, which could still be the tailptr, as it stalls on the next
lock operation ::

 Samples: 25K of event 'cycles', Event count (approx.): 25790301710
  Overhead  Symbol
 +   24.75%  [k] mlx5e_handle_rx_cqe
 +   16.95%  [k] __build_skb
 +   10.72%  [k] mlx5e_xmit
 +    7.03%  [k] build_skb
 +    5.31%  [k] mlx5e_alloc_rx_wqe
 +    2.99%  [k] kmem_cache_alloc
 +    2.65%  [k] ___slab_alloc
 +    2.65%  [k] _raw_spin_lock
 +    2.52%  [k] bpf_prog_662b9cae761bf6ab
 +    2.37%  [k] netif_receive_skb_internal
 +    1.92%  [k] memcpy_erms
 +    1.73%  [k] generic_xdp_tx
 +    1.69%  [k] mlx5e_get_cqe
 +    1.40%  [k] __netdev_pick_tx
 +    1.28%  [k] __rcu_read_unlock
 +    1.19%  [k] netdev_pick_tx
 +    1.02%  [k] swiotlb_map_page
 +    1.00%  [k] __cmpxchg_double_slab.isra.56
 +    0.99%  [k] dev_gro_receive
 +    0.85%  [k] __rcu_read_lock
 +    0.80%  [k] napi_gro_receive
 +    0.79%  [k] mlx5e_poll_rx_cq
 +    0.73%  [k] mlx5e_post_rx_wqes
 +    0.71%  [k] get_partial_node.isra.76
 +    0.70%  [k] mlx5e_page_release
 +    0.62%  [k] eth_type_trans
 +    0.56%  [k] mlx5e_select_queue
      0.49%  [k] skb_gro_reset_offset
      0.42%  [k] skb_put

Packet rate 4574424 translates to ~219 nanosec (1/4574424*10^9).

The top contender is mlx5e_handle_rx_cqe(24.75%), which initially
didn't surprise me, given I know that this function (via inlining)
will be the first to touch the packet (via is_first_ethertype_ip()),
thus causing a cache-line miss.  **BUT something is wrong**.  Looking
at perf-annotate, the cache-line miss is NOT occurring, instead 67.24%
CPU time spend on a refcnt increment (due to page_ref_inc(di->page)
used for page-recycle cache).  Something is wrong as 24.75% of 219 is
54ns, which is too high even for an atomic refcnt inc. (Note: the
cache-miss is actually avoided due to the prefetch have time to work,
due to this stall on the lock. Thus, removing the stall will
bring-back the cache-line stall).

Inside __build_skb(16.95%) there is 83.47% CPU spend on "rep stos",
which is clearing/memset-zero the SKB itself.  Again something is
wrong as ((1/4574424*10^9)*(16.95/100)) = 37ns is too high for
clearing the SKB (time_bench_memset show this optimally takes 10 ns).

Inside mlx5e_xmit(10.72%) there is 17.96% spend on a sfence asm
instruction.  The cost (1/4574424*10^9)*(10.72/100) = 23.43 ns of
calling mlx5e_xmit() might not be too off-target.

My guess is that this is caused the the tailptr/doorbell stall.  And
doing bulk/xmit_more we can likely reduce mlx5e_handle_rx_cqe(-12ns as
cache-miss returns) and __build_skb(-27ns).  Thus, the performance
target should lay around 5.6Mpps ((1/(218-12-27)*10^9) = 5586592).

Also notice that __cmpxchg_double_slab() show that we are hitting the
SLUB slow(er)-path.

Zooming into perf with Generic-netstack-XDP
-------------------------------------------

Testing Generic-netstack-XDP_DROP again and looking closer at the perf
reports.  This will be intersting because we can deduct the cost of
the different parts of the network stack, assuming there is no-fake
stalls due to tailptr/doorbell (like the XDP_TX case) ::

 [jbrouer@skylake prototype-kernel]$
  sudo ./xdp_bench01_mem_access_cost --action XDP_DROP --dev mlx5p2 --read
 XDP_action   pps        pps-human-readable mem
 XDP_DROP     8148835    8,148,835          read     
 XDP_DROP     8148972    8,148,972          read     
 XDP_DROP     8148962    8,148,962          read     
 XDP_DROP     8146856    8,146,856          read     
 XDP_DROP     8150026    8,150,026          read     
 XDP_DROP     8149734    8,149,734          read     
 XDP_DROP     8149646    8,149,646          read     

For some unknown reason the Generic-XDP_DROP number are a bit lower,
than above numbers.  Using 8148972 pps (8,148,972) as our new
baseline, show (averaged) cost per packet 122.47 nanosec (1/8165032*10^9)

The difference to NIC-level-XDP is:
(1/12006685*10^9)- (1/8148972*10^9) = -39.42 ns

Simply perf recorded 30 sec, and find the CPU this was running on by
added the --sort cpu to the output.  The CPU output/column showed that
NAPI was running on CPU 7 ::

 sudo ~/perf record -aR -g sleep 30
 sudo ~/perf report --no-children  --sort cpu,comm,dso,symbol

Now we will drill down on CPU 7 and see what it is doing.  We start
with removing the "children" column, to start viewing the overhead on
a per function basis.

I'm using this long perf report command to reduce the columns and
print to stdout and removing the call graph (I'll manually inspect the
call-graph with the standard terminal-user-interface (TUI)) ::

 sudo ~/perf report --no-children  --sort symbol \
    --kallsyms=/proc/kallsyms -C7 --stdio -g none

Reduced output::

 # Samples: 119K of event 'cycles'
 # Event count (approx.): 119499252009
 #
 # Overhead  Symbol
 # ........  ..........................................
 #
    34.33%  [k] mlx5e_handle_rx_cqe
    10.36%  [k] __build_skb
     5.49%  [k] build_skb
     5.10%  [k] page_frag_free
     4.06%  [k] bpf_prog_662b9cae761bf6ab
     4.02%  [k] kmem_cache_alloc
     3.85%  [k] netif_receive_skb_internal
     3.72%  [k] kmem_cache_free
     3.69%  [k] mlx5e_alloc_rx_wqe
     2.91%  [k] mlx5e_get_cqe
     1.83%  [k] napi_gro_receive
     1.80%  [k] __rcu_read_unlock
     1.65%  [k] skb_release_data
     1.49%  [k] dev_gro_receive
     1.43%  [k] skb_release_head_state
     1.26%  [k] mlx5e_post_rx_wqes
     1.22%  [k] mlx5e_page_release
     1.21%  [k] kfree_skb
     1.19%  [k] eth_type_trans
     1.00%  [k] __rcu_read_lock
     0.84%  [k] skb_release_all
     0.83%  [k] skb_free_head
     0.81%  [k] kfree_skbmem
     0.80%  [k] percpu_array_map_lookup_elem
     0.79%  [k] mlx5e_poll_rx_cq
     0.79%  [k] skb_put
     0.77%  [k] skb_gro_reset_offset
     0.63%  [k] swiotlb_sync_single
     0.61%  [k] swiotlb_sync_single_for_device
     0.42%  [k] swiotlb_sync_single_for_cpu
     0.28%  [k] net_rx_action
     0.21%  [k] bpf_map_lookup_elem
     0.20%  [k] mlx5e_napi_poll
     0.11%  [k] __do_softirq
     0.06%  [k] mlx5e_poll_tx_cq
     0.02%  [k] __raise_softirq_irqoff

Some memory observations are that we are hitting the fast path of the
SLUB allocator (indicated by no func names from the slower path).  The
mlx5 driver-page recycler also have 100% hit rate, verified by looking
at ethtool -S stats, and mlx5 stats "cache_reuse",
using my `ethtool_stats.pl`_ tool::

 Show adapter(s) (mlx5p2) statistics (ONLY that changed!)
 Ethtool(mlx5p2) stat:     8179636 (      8,179,636) <= rx3_cache_reuse /sec
 Ethtool(mlx5p2) stat:     8179632 (      8,179,632) <= rx3_packets /sec
 Ethtool(mlx5p2) stat:    40657800 (     40,657,800) <= rx_64_bytes_phy /sec
 Ethtool(mlx5p2) stat:   490777805 (    490,777,805) <= rx_bytes /sec
 Ethtool(mlx5p2) stat:  2602103605 (  2,602,103,605) <= rx_bytes_phy /sec
 Ethtool(mlx5p2) stat:     8179636 (      8,179,636) <= rx_cache_reuse /sec
 Ethtool(mlx5p2) stat:     8179630 (      8,179,630) <= rx_csum_complete /sec
 Ethtool(mlx5p2) stat:    18736623 (     18,736,623) <= rx_discards_phy /sec
 Ethtool(mlx5p2) stat:    13741170 (     13,741,170) <= rx_out_of_buffer /sec
 Ethtool(mlx5p2) stat:     8179630 (      8,179,630) <= rx_packets /sec
 Ethtool(mlx5p2) stat:    40657861 (     40,657,861) <= rx_packets_phy /sec
 Ethtool(mlx5p2) stat:  2602122863 (  2,602,122,863) <= rx_prio0_bytes /sec
 Ethtool(mlx5p2) stat:    21921459 (     21,921,459) <= rx_prio0_packets /sec
 [...]

.. _ethtool_stats.pl:
  https://github.com/netoptimizer/network-testing/blob/master/bin/ethtool_stats.pl

Knowing the cost per packet 122.47 nanosec (1/8165032*10^9), we can
extrapolate the ns used by each function call.  Let use oneline for
calculating that for us::

 sudo ~/perf report --no-children  --sort symbol \
   --kallsyms=/proc/kallsyms -C7 --stdio -g none | \
 awk -F% 'BEGIN {base=(1/8165032*10^9)} \
   /%/ {ns=base*($1/100); \
        printf("%6.2f\% => %5.1f ns func:%s\n",$1,ns,$2);}'

Output::

 34.33% =>  42.0 ns func:  [k] mlx5e_handle_rx_cqe
 10.36% =>  12.7 ns func:  [k] __build_skb
  5.49% =>   6.7 ns func:  [k] build_skb
  5.10% =>   6.2 ns func:  [k] page_frag_free
  4.06% =>   5.0 ns func:  [k] bpf_prog_662b9cae761bf6ab
  4.02% =>   4.9 ns func:  [k] kmem_cache_alloc
  3.85% =>   4.7 ns func:  [k] netif_receive_skb_internal
  3.72% =>   4.6 ns func:  [k] kmem_cache_free
  3.69% =>   4.5 ns func:  [k] mlx5e_alloc_rx_wqe
  2.91% =>   3.6 ns func:  [k] mlx5e_get_cqe
  1.83% =>   2.2 ns func:  [k] napi_gro_receive
  1.80% =>   2.2 ns func:  [k] __rcu_read_unlock
  1.65% =>   2.0 ns func:  [k] skb_release_data
  1.49% =>   1.8 ns func:  [k] dev_gro_receive
  1.43% =>   1.8 ns func:  [k] skb_release_head_state
  1.26% =>   1.5 ns func:  [k] mlx5e_post_rx_wqes
  1.22% =>   1.5 ns func:  [k] mlx5e_page_release
  1.21% =>   1.5 ns func:  [k] kfree_skb
  1.19% =>   1.5 ns func:  [k] eth_type_trans
  1.00% =>   1.2 ns func:  [k] __rcu_read_lock
  0.84% =>   1.0 ns func:  [k] skb_release_all
  0.83% =>   1.0 ns func:  [k] skb_free_head
  0.81% =>   1.0 ns func:  [k] kfree_skbmem
  0.80% =>   1.0 ns func:  [k] percpu_array_map_lookup_elem
  0.79% =>   1.0 ns func:  [k] mlx5e_poll_rx_cq
  0.79% =>   1.0 ns func:  [k] skb_put
  0.77% =>   0.9 ns func:  [k] skb_gro_reset_offset
  0.63% =>   0.8 ns func:  [k] swiotlb_sync_single
  0.61% =>   0.7 ns func:  [k] swiotlb_sync_single_for_device
  0.42% =>   0.5 ns func:  [k] swiotlb_sync_single_for_cpu
  0.28% =>   0.3 ns func:  [k] net_rx_action
  0.21% =>   0.3 ns func:  [k] bpf_map_lookup_elem
  0.20% =>   0.2 ns func:  [k] mlx5e_napi_poll
  0.11% =>   0.1 ns func:  [k] __do_softirq

top contender mlx5e_handle_rx_cqe
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The top contender mlx5e_handle_rx_cqe() in the driver code ::

 34.33% =>  42.0 ns func:  [k] mlx5e_handle_rx_cqe

When looking at the code/perf-annotate do notice that several function
calls have been inlined by the compiler.  The thing that light-up
(56.23% => 23.6 ns) in perf-annotate is touching/reading the
data-packet for the first time, which is reading the ethertype via
is_first_ethertype_ip(), called via:

 * which is called from mlx5e_handle_csum()
 * which is called by mlx5e_build_rx_skb()
 * which is called by mlx5e_complete_rx_cqe()
 * which is called by mlx5e_handle_rx_cqe() all inlined.

Notice this is_first_ethertype_ip() call is the reason why
eth_type_trans() is not so hot in this driver.

Analyzing __build_skb and memset
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The compiler choose not to inline __build_skb(), and what is primarily
going on here is memset clearing the SKB data, which gets optimized
into an "rep stos" asm-operation, which is actually not optimal for
this size of objects.  Looking at perf-annotate shows that 75.65% of
the time of __build_skb() is spend on "rep stos %rax,%es:(%rdi)".
Thus, extrapolating 12.7 ns (12.7*(75.65/100)) cost of 9.6 ns.

This is very CPU specific how fast or slow this is, but I've
benchmarked different alternative approaches with
`time_bench_memset.c`_.

.. _time_bench_memset.c:
   https://github.com/netoptimizer/prototype-kernel/blob/master/kernel/lib/time_bench_memset.c

Memset benchmarks on this Skylake CPU show that hand-optimizing
ASM-coded memset, can reach 8 bytes per cycles, but only saves approx
2.5 ns or 10 cycles. A more interesting approach would be if we could
memset clear a larger area.  E.g. when bulk-allocating SKBs and
detecting they belong to the same page and is contiguous in memory.
Benchmarks show that clearing larger areas is more efficient.

Table with memset "rep-stos" size vs bytes-per-cycle efficiency ::

 $ perl -ne 'while(/memset_(\d+) .* elem: (\d+) cycles/g)\
    {my $bpc=$1/$2; \
     printf("memset %5d bytes cost %4d cycles thus %4.1f bytes-per-cycle\n", \
            $1, $2, $bpc);}' memset_test_dmesg

 memset    32 bytes cost    4 cycles thus  8.0 bytes-per-cycle
 memset    64 bytes cost   29 cycles thus  2.2 bytes-per-cycle
 memset   128 bytes cost   29 cycles thus  4.4 bytes-per-cycle
 memset   192 bytes cost   35 cycles thus  5.5 bytes-per-cycle
 memset   199 bytes cost   35 cycles thus  5.7 bytes-per-cycle
 memset   201 bytes cost   39 cycles thus  5.2 bytes-per-cycle
 memset   204 bytes cost   40 cycles thus  5.1 bytes-per-cycle
 memset   200 bytes cost   39 cycles thus  5.1 bytes-per-cycle
 memset   208 bytes cost   39 cycles thus  5.3 bytes-per-cycle
 memset   256 bytes cost   36 cycles thus  7.1 bytes-per-cycle
 memset   512 bytes cost   40 cycles thus 12.8 bytes-per-cycle
 memset   768 bytes cost   47 cycles thus 16.3 bytes-per-cycle
 memset  1024 bytes cost   52 cycles thus 19.7 bytes-per-cycle
 memset  2048 bytes cost   84 cycles thus 24.4 bytes-per-cycle
 memset  4096 bytes cost  148 cycles thus 27.7 bytes-per-cycle
 memset  8192 bytes cost  276 cycles thus 29.7 bytes-per-cycle

I've already implemented the SLUB bulk-alloc API, and it could be
extended with detecting if objects are physically contiguous for
allowing clearing multiple object at the same time. (Notice the SLUB
alloc-side fast-path already delivers object from the same page).


Blaming the children
--------------------

The nanosec number are getting so small, that we might miss the effect
of deep call chains.  Thus, lets look at perf report with the
"children" enabled::

  Samples: 119K of event 'cycles', Event count (approx.): 119499252009
   Children      Self  Symbol
 +  100.00%     0.00%  [k] kthread
 +  100.00%     0.00%  [k] ret_from_fork
 +   99.99%     0.01%  [k] smpboot_thread_fn
 +   99.98%     0.01%  [k] run_ksoftirqd
 +   99.94%     0.11%  [k] __do_softirq
 +   99.78%     0.28%  [k] net_rx_action
 +   99.41%     0.20%  [k] mlx5e_napi_poll
 +   92.44%     0.79%  [k] mlx5e_poll_rx_cq
 +   86.37%    34.33%  [k] mlx5e_handle_rx_cqe
 +   29.40%     1.83%  [k] napi_gro_receive
 +   24.50%     3.85%  [k] netif_receive_skb_internal
 +   19.41%     5.49%  [k] build_skb
 +   14.98%     1.21%  [k] kfree_skb
 +   14.15%    10.36%  [k] __build_skb
 +    9.43%     0.84%  [k] skb_release_all
 +    6.97%     1.65%  [k] skb_release_data
 +    5.38%     1.26%  [k] mlx5e_post_rx_wqes
 +    5.10%     5.10%  [k] page_frag_free
 +    4.86%     4.06%  [k] bpf_prog_662b9cae761bf6ab
 +    4.30%     3.69%  [k] mlx5e_alloc_rx_wqe
 +    4.30%     0.81%  [k] kfree_skbmem
 +    4.02%     4.02%  [k] kmem_cache_alloc
 +    3.72%     3.72%  [k] kmem_cache_free
 +    2.91%     2.91%  [k] mlx5e_get_cqe

Lets calculate the ns cost::

  $ sudo ~/perf report --children  --sort symbol \
    --kallsyms=/proc/kallsyms -C7 --stdio -g none | \
    awk -F% 'BEGIN {base=(1/8165032*10^9); \
             print "Children => nanosec     Self    Symbol/fucntion\n";} \
      /%/ {ns=base*($1/100); \
          printf("%6.2f%s => %5.1f ns %s%s func:%s\n",$1,"%",ns,$2,"%",$3);}'

 Children => nanosec     Self    Symbol/fucntion
 100.00% => 122.5 ns      0.00% func:  [k] kthread
 100.00% => 122.5 ns      0.00% func:  [k] ret_from_fork
  99.99% => 122.5 ns      0.01% func:  [k] smpboot_thread_fn
  99.98% => 122.4 ns      0.01% func:  [k] run_ksoftirqd
  99.94% => 122.4 ns      0.11% func:  [k] __do_softirq
  99.78% => 122.2 ns      0.28% func:  [k] net_rx_action
  99.41% => 121.8 ns      0.20% func:  [k] mlx5e_napi_poll
  92.44% => 113.2 ns      0.79% func:  [k] mlx5e_poll_rx_cq
  86.37% => 105.8 ns     34.33% func:  [k] mlx5e_handle_rx_cqe
  29.40% =>  36.0 ns      1.83% func:  [k] napi_gro_receive
  24.50% =>  30.0 ns      3.85% func:  [k] netif_receive_skb_internal
  19.41% =>  23.8 ns      5.49% func:  [k] build_skb
  14.98% =>  18.3 ns      1.21% func:  [k] kfree_skb
  14.15% =>  17.3 ns     10.36% func:  [k] __build_skb
   9.43% =>  11.5 ns      0.84% func:  [k] skb_release_all
   6.97% =>   8.5 ns      1.65% func:  [k] skb_release_data
   5.38% =>   6.6 ns      1.26% func:  [k] mlx5e_post_rx_wqes
   5.10% =>   6.2 ns      5.10% func:  [k] page_frag_free
   4.86% =>   6.0 ns      4.06% func:  [k] bpf_prog_662b9cae761bf6ab
   4.30% =>   5.3 ns      3.69% func:  [k] mlx5e_alloc_rx_wqe
   4.30% =>   5.3 ns      0.81% func:  [k] kfree_skbmem
   4.02% =>   4.9 ns      4.02% func:  [k] kmem_cache_alloc
   3.72% =>   4.6 ns      3.72% func:  [k] kmem_cache_free
   2.91% =>   3.6 ns      2.91% func:  [k] mlx5e_get_cqe
   1.80% =>   2.2 ns      1.80% func:  [k] __rcu_read_unlock
   1.49% =>   1.8 ns      1.49% func:  [k] dev_gro_receive
   1.43% =>   1.8 ns      1.43% func:  [k] skb_release_head_state
   1.22% =>   1.5 ns      1.22% func:  [k] mlx5e_page_release
   1.19% =>   1.5 ns      1.19% func:  [k] eth_type_trans
   1.00% =>   1.2 ns      1.00% func:  [k] __rcu_read_lock
   0.84% =>   1.0 ns      0.83% func:  [k] skb_free_head
   0.80% =>   1.0 ns      0.80% func:  [k] percpu_array_map_lookup_elem
   0.79% =>   1.0 ns      0.79% func:  [k] skb_put
   0.77% =>   0.9 ns      0.77% func:  [k] skb_gro_reset_offset

Interesting here is napi_gro_receive() which is the base-call into the
network stack, everything "under" this call cost 29.40% of the time,
translated to 36.0 ns.  This 36 ns cost is interesting as we
calculated the difference to NIC-level-XDP to be 39 ns:

The difference to NIC-level-XDP is:
 (1/12006685*10^9)- (1/8148972*10^9) = -39.42 ns

Freeing the SKB is summed up under kfree_skb() with 14.98% => 18.3 ns.
In this case kfree_skb() should get attributed under napi_gro_receive(),
due to the direct kfree_skb(skb) call in netif_receive_generic_xdp().
In other situations kfree_skb() happens during the DMA TX completion,
but not here.

Creating, allocating and clearing the SKB is all "under" the
build_skb() call, which attributes to a collective 19.41% or 23.8 ns.
The build_skb() call happens, in-driver, before calling napi_gro_receive.

Thus, one might be lead to conclude that the overhead of the network
stack is (23.8 ns +36 ns) 59.8 ns, but something is not adding up as
this is higher the calculated approx 40ns difference to NIC-level-XDP.

