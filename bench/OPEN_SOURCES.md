# Reproducible external test-source manifest

The benchmark harness accepts any local source file. Recommended openly redistributable sources for the extended suite:

- **Big Buck Bunny** — approximately 10 minutes, Blender Foundation, CC BY 3.0. Use for mixed animation, camera motion, foliage and action.
- **Sintel** — Blender Foundation open movie. Use action-heavy and cinematic material.
- **Tears of Steel** — Blender Foundation open movie. Use live action, effects, faces and cuts.
- **FourPeople / Johnny / KristenAndSara / Vidyo** from Xiph/Derf — public-domain talking-head sequences.
- **crowd_run**, **ducks_take_off**, **park_joy**, **sunflower** from the Xiph/VQEG collections — high motion and natural fine texture; inspect the accompanying copyright/readme before redistribution.

The project intentionally does not automate downloading arbitrary YouTube videos. Download only material whose licence permits codec testing and redistribution, then pass the local files to `scripts/benchmark.py`.

Example after obtaining the files:

```sh
python3 scripts/benchmark.py \
  --sources media/big_buck_bunny.mp4 media/sintel.mkv media/tears_of_steel.mkv \
  --duration 300 --fps 10 --hlv-qualities 40,55,70 --include-av1 \
  --prefix open_movies_5min
```
