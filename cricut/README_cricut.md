# Cricut badge templates

These SVGs are sized for a 4 inch round acrylic disk.

## Files

- `front_vinyl_cut.svg` - front Cricut vinyl art with text, network traces, and LED node rings. Text has been converted to paths.
- `front_vinyl_cut_editable.svg` - same front art, but text remains editable SVG text.
- `led_placement_guide.svg` - print this on paper as a placement guide for the 8 LEDs. Do not use as the vinyl cut file.
- `back_label_print_then_cut.svg` - back instruction label with a local badge QR code for `http://192.168.4.1`.
- `badge_preview.png` - visual preview.

## Cricut notes

1. Upload `front_vinyl_cut.svg` to Design Space.
2. Confirm the imported size is 4.0 in x 4.0 in.
3. Use separate vinyl colors if desired:
   - White for the text.
   - Copper/gold/silver for traces and LED node rings.
4. The front SVG does not include the 4 inch circle outline because you already have acrylic circles.
5. Use `led_placement_guide.svg` as a printed paper guide behind the acrylic while placing LEDs.
6. Use `back_label_print_then_cut.svg` as a print-then-cut sticker, not as cut vinyl. QR codes are usually too detailed for vinyl cutting.

## LED order

The guide places LED1 at 12 o'clock, then numbers clockwise around the circle.

```text
        LED1
   LED8      LED2

LED7  TEXT   LED3

   LED6      LED4
        LED5
```

Wire LED DOUT to the next LED DIN in that order.
