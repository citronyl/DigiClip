# DigiClip
Electronics project to design wearable PCB hairclips

# LOADING AUDIO
Download audio files and store in seal-loader folder. In terminal run
```bash
ffmpeg -i {filename}.wav -ac 1 -ar 13021 -af highpass=f=400 -f u8 {filename}.raw
```

Set up loader script (only once)
```bash
chmod +x load_all.sh 
```
Plug in clip via UPDI. In terminal run:
```bash
python pack_barks.py {bark1}.raw {bark2}.raw
./load_all.sh
```



## License
This PCB design is open-source hardware licensed under the **CERN Open Hardware Licence Version 2 - Strongly Reciprocal (CERN-OHL-S v2)**. 

You are free to use, study, modify, and distribute these files and manufacture physical hardware based on them, provided that any derivative works or modifications are also shared under the exact same CERN-OHL-S v2 license terms.

For the full legal text, see the [LICENSE](LICENSE) file.
