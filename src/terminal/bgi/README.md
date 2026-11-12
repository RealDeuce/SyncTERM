# Shared BGI data

The immutable 64-colour EGA RGB16 table and sixteen default pen codes were
extracted verbatim from `cterm_rip_raster.c`. BIG reference chapters 26 and
83 identify the same data. `big_profile` independently checks every EGA entry
against its six colour-bit weights and checks the literal default map.

RIP and BIG keep separate protocol state and wire grammars. Only proved
matching data or used kernels belong here; this is not a second framebuffer
or a general BGI emulation library.
