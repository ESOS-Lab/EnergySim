SSD energy consumption analysis tool (EnergySim)
=================================================
Seokhei Cho <misost@hanyang.ac.kr>, Changhyun Park <pch1984@hanyang.ac.kr>

### Reference:
* Seokhei Cho et al. "Design Trade-Offs of SSDs: from Energy Consumption's Perspective"

### Acknowledgement:
* This work is sponsored by IT R&D program MKE/KEIT. [No.10035202, Large Scale hyper-MLC SSD Technology Development] and by the MSIP(Ministry of Science, ICT&Future Planning), Korea, under the ITRC(Information Technology Research Center) support program (NIPA-2014- H0301-14-1017) supervised by the NIPA(National IT Industry Promotion Agency).


EnergySim
-----------
This is not a simulator for any specific SSD, but rather a simulator for an idealized SSD that is parameterized by the properties of NAND flash chips such as read, write, and erase latency.  

There are various tuning parameters.  Please see the ssdmodel module specification for more information.  It is worth noting that the current SSD add-on does not simulate a read or write cache.

Apply this add-on by unpacking the enclosed into the ssdmodel subdirectory of the DiskSim source tree (so as to be parallel to memsmodel, etc.)  Then run the following patch script.

    patch -p1 < ssdmodel/ssd-patch

This patch script is valid for the DiskSim 4.0 release of 6/23/2008.
After successfully running the patch script, Unix/Linux/CygWin "make" on the top level of DiskSim creates an executable that supports SSD simulation.

In addition, the patch script creates a w32build directory that is suitable for building the simulator in Microsoft VisualStudio.  Note, however, that in order to build in this environment, you must first run the "relex" and "remod" scripts in the w32build directory.  These scripts must be run *once* under cygwin (or equivalent) with flex and bison support.

The VisualStudio build file is w32build/disksim.sln.  The build produces a ton of warnings, but should otherwise work.

The ssdmodel subdirectory contains a valid subdirectory that contains SSD-specific simulator tests.  To run these, execute the runvalid command in ssdmodel/valid.
