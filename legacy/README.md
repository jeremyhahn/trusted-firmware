# legacy/

The CropDroid irrigation firmware this project replaces, kept for reference and
excluded from every build.

`main.cpp` is the authority on what the original hardware did: DHT22 on pin 9,
six analog moisture inputs on A1–A6, one flow input on A0, seven relay channels
on digital pins `{8,2,3,4,5,6,7}`, and the REST routes `setip.sh` drives. When a
question comes up about the pin assignments or the wire semantics the deployed
controllers expect, the answer is here.

It is not a style reference. Its known defects are catalogued in
`../docs/migration/from-cropdroid-irrigation.md`, and the replacement exists
because of them.

`Makefile.avrdude` is the original flash target set. Its `/home/jhahn/.arduino15`
paths do not resolve on any current machine; it is kept for the avrdude
invocations, not to be run.
