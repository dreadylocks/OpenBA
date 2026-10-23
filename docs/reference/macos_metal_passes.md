# macOS Metal passes

Metal implements ASSAO after opaque geometry. It deinterleaves readable depth,
generates normals and occlusion, blurs the result and multiplies RGB while
preserving alpha. Selection uses `ext.ambient_occlusion`, `ext.ao_quality` and
`ext.ao_opacity` in settings.cfg. A backend must report unsupported when its
required shaders or depth path are unavailable.

MSAA uses multisampled main colour/depth targets, sample-count-specific pipelines
and colour resolve at presentation/readback. Available counts depend on the GPU;
changing the setting rebuilds the main targets. Shadow targets stay single-sample.

Better Trees rewrites runtime DXBC for cutout coverage, distance fill, billboard
shadow reception and edge dither. With MSAA, foliage coverage uses
alpha-to-coverage. Mode 1 preserves cutouts; mode 2 adds distance fill. The shared
mode-2 constants are 0.15 strength and 0.15 floor. Metal uses the ruled shared values 0.15 / 0.15; the earlier Metal-only 0.08 / 0.40
tuning is gone, and whether the haze it targeted returns has not been compared. Billboard
receiver changes affect shadow reception, not additional shadow casters.

Build through `tools/build.sh build perf --jobs 4`. Run with a legally owned
install supplied through `BANISHED_DIR`; select the features in settings.cfg or
Options. Faithful profile disables these extensions. For unattended runs use
`BANISHED_BACKGROUND=1`, `SDL_AUDIODRIVER=dummy` and a throwaway `USERPROFILE`;
terminate only the process started for that run. There is no shipped automated
Metal pass comparison harness. Historical screenshots do not establish retail
parity: [pass measurements](../history/macos/PASSES.md).
