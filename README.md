# ESP32 Based Internet radio with a web-server remote

note: if using the PCM5102 DAC, make you sure you do the correct solder bridges:
1. The one on the front next to SCK.
2. H1L - L (bridge middle pad L side)
3. H2L - L (bridge middle pad L side)
4. H3L - H (bridge middle pad H side)
5. H4L - L (bridge middle pad L side)

The DIN/DOUT pin also needed to be shorted to the adjacent pin on the esp32 to potentially stop ringing on the data-line - this might be because of how i wired it (parasitic capacitance) or it might be necessary, unsure but it fixed audio glitches
