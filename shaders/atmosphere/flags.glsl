
//
// Flags which should really be uniforms (... or rather macros, to help the driver optimise):
//

// Optimise per voxel lighting with intermediate mini shadow maps computed per node group.
// Speeds up lighting pass roughly 4-8 times with seemingly no or few artefacts.
// (even hides some aliasing "grooves" which were present without the optimisation)
// (but might cause "spotty" artefacts instead. Really an... artefact redistribution)
const bool usePerGroupLighting = true;

// Interpolate between two voxel states when animating.
// - Big performance impact: roughly halves render speed in many common/heavier cases.
const bool interpolateAnimation = false;

// Double the step size in bricks flagged as empty.
// Causes banding, unfortunately (but only/mostly in space views? Maybe it could be applied selectively).
// Moderate performance win. Roughly 1/6 of render time, depending on the view.
const bool doubleStepsInEmptyBricks = false;//true;
