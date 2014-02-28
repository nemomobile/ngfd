#!/bin/sh

FLAVOUR="ngfd ngfd-hybris"

for fla in $FLAVOUR; do
  sed -e "s/@FLAVOUR@/$fla/g" ngfd.spec.in > $fla.spec
done

