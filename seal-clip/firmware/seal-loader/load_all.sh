#!/bin/bash
# Uploads the loader once per chunk and checks each result.
for f in $(ls chunks/chunk_*.h | sort -V); do
  cp "$f" include/chunk.h
  echo "== $f"
  pio run -t upload -s || { echo "UPLOAD FAILED"; exit 1; }
  sleep 6                                       # erase + write + verify
  out=$(pymcuprog read -t uart -u /dev/ttyUSB0 -d attiny3224 -m eeprom -o 0 -b 4 | grep 0x001400)
  echo "$out"
  [[ "$out" == *"AA 00 5A"* ]] || { echo "CHUNK FAILED, stopping"; exit 1; }
done
echo "ALL CHUNKS LOADED"
