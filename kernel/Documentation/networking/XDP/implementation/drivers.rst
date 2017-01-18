=======
Drivers
=======

XDP depends on drivers implementing the RX hook and set-up API.
Adding driver support is fairly easy, unless it requires changing the
driver's memory model (which is often the case).


Mellanox: mlx4
==============

The first driver implementing XDP were the Mellanox ``mlx4`` driver.
The corresponding NIC is called `ConnectX-3`_ and `ConnectX-3 pro`_.
These NICs run Ethernet at 10Gbit/s and 40Gbit/s.

.. _`ConnectX-3 pro`:
   http://www.mellanox.com/page/products_dyn?product_family=162&mtag=connectx_3_pro_en_card

.. _`ConnectX-3`:
  http://www.mellanox.com/page/products_dyn?product_family=127&mtag=connectx_3_en

Mellanox: mlx5
==============

The Mellanox driver ``mlx5`` support XDP since kernel v4.9, but kernel
v4.10 is recommended as some minor fixes got applied.

These NICs run Ethernet at 10G, 25G, 40G, 50G and 100Gbit/s. They are
called `ConnectX-4`_ and `ConnectX-4-Lx`_ (Lx is limited to max 50G or
2x 25G).

.. _`ConnectX-4`:
   http://www.mellanox.com/page/products_dyn?product_family=204&mtag=connectx_4_en_card

.. _`ConnectX-4-Lx`:
   http://www.mellanox.com/page/products_dyn?product_family=219&mtag=connectx_4_lx_en_card
