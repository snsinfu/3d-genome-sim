# Example data for CH12.LX simulation

This directory contains example files for simulating mouse CH12.LX cells:

- [chains.tsv](chains.tsv)
- [config.json](config.json)

The chain definition file (chains.tsv) is generated by the following commands:

```shell
$ ../../scripts/nci --binsize 100000 --output nci.tsv 4DNFIJLJIRKT.mcool
$ ../../scripts/annotate --activate-nor :a --extend-nor --cyto mm10-cytoBand-incorrect.txt --nci nci.tsv --smooth 5 --output chains.tsv
```

using these files:

- [4DNFIJLJIRKT.mcool][mcool]
- [mm10-cytoBand-incorrect.txt](mm10-cytoBand-incorrect.txt)

The latter is a manual, crude annotation of the mouse chromosomes to include
centromeres (`acen`) and nucleolus-organizing regions (`stalk`). It is based on
[cytoBand.txt.gz][cytoband] for mm10, and we re-annotated the leading 3MB of
each chromosome as `acen` excepting for chr12, 15, 16, 18, and 19 where the
leading 1.5MB is `acen` and the subsequent 1.5MB is `stalk`. **The annotation
is just for a technical demonstration and in no way biologically correct.**

[mcool]: https://data.4dnucleome.org/files-processed/4DNFIJLJIRKT/
[cytoband]: https://hgdownload.soe.ucsc.edu/goldenPath/mm10/database/cytoBand.txt.gz

