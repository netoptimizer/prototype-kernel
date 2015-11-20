#!/bin/bash
#
# This script is almost same as: fail01_kmem_cache_alloc_bulk.sh
#  - See desc about double page alloc fault and "space" trick in that script
#
# Difference is testing normal (non-bulk) call: kmem_cache_alloc()
#
# This test exercise calling slab_post_alloc_hook() with object==NULL.
# The normal slab fault-injector "failslab", does not test this,
# because it will exit after the call to slab_pre_alloc_hook().
#
# Remember to compile kernel with config options for either kmemleak
# or kasan, else the compiler will have removed the code we want to
# exercise.
#

# Reuse fail01 script with a module parameter
CMD=./fail01_kmem_cache_alloc_bulk.sh

if [[ ! -x $CMD ]]; then
    echo "ERR - Need $CMD to run this test"
    exit 1
fi

exec $CMD nobulk
