dassRBM - Usage Guide
~~~~~~~~~~~~~~~~~~~~~

These instructions are written for Microsoft Windows, but should work 
for anything else, too. Given that you're able to compile dassRBM for 
your system, of course. 


To extract a RBM file:
~~~~~~~~~~~~~~~~~~~~~~

[1] Put dassRBM.exe, extract_all.bat and the RBM file into one folder. 
[2] Run extract_all.exe.
-> You'll get one .img/.qmg file for each item in the RBM and one 
"build" CSV file containing the instructions for rebuild of the RBM 
file. The "build" CSV file can be opened using 
Excel/OpenOffice/LibreOffice or similar. See "editing build CSV files" 
for editing. 


To (re)build a RBM file:
~~~~~~~~~~~~~~~~~~~~~~~~

[1] Put dassRBM.exe, build_all.bat, all items to include (img/qmg/png) 
and the CSV file into one folder.
[2] Run build_all.exe.
-> All the items will be used as detailed in the CSV file to (re)build 
the RBM file. 


To analyse RBM files:
~~~~~~~~~~~~~~~~~~~~~

[1] Put dassRBM.exe, summarize_all.bat, and all RBMs to analyse into one 
folder.
[2] Run summarize_all.exe.
-> You'll get a comprehensive analysis of all RBM files in 
summary_items.csv (for items included in the RBM files) and 
sammary_rbms.csv (for RBM overview). Open in 
Excel/OpenOffice/LibreOffice or similar. 


Editing CSV build files
~~~~~~~~~~~~~~~~~~~~~~~

To completely remove one item:
Just delete the whole line/row corresponding to the item you no longer 
want. Rebuild the RBM file (see above). Keep in mind that doing this, 
and with it effectively changing the numbering for following items, is 
not recommended for RBMs used in Hardware. 

To safely remove one item:
Change the value in the "type" column to zero ("0"). Rebuild the RBM 
file (see above). The item ordering will be preserved this way, and (as 
long as the item is really not used) it is safe to do so. 

To add an IMG/QMG as new item or replace existing:
Add a row/line to the CSV file or replace an existing row. ID must be 
sequential (no double IDs, IDs in order, same as replaced row), 
resolution (width/height) and bpp must be correct. Type is 2 for IMG and 
3 or 5 for QMG (refer to the original source for choice between 3/5). 
Everything else may be zero (refer to the original source for alpha/mask 
anyways for the correct values). 

To add a PNG file as new item or replace existing:
Add a row/line to the CSV file or replace an existing row. ID must be 
sequential (no double IDs, IDs in order, same as replaced row), 
resolution (width/height) must be correct and bpp must be 16 or 32 (32 
for better quality and alpha channel). Type is 1. Alpha/mask is zero. 


Additional Info
~~~~~~~~~~~~~~~

dassRBM was originally written to edit .RBM files found in the Samsung 
Bada mobile OS. Find the original thread on XDA here: 
http://forum.xda-developers.com/showthread.php?t=1850829

This also contains lodepng source code, find the official repo here:
https://github.com/lvandeve/lodepng

You may freely use this package under the terms of the GPL v3.
