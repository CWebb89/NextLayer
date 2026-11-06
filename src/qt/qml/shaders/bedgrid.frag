// OrcaSlicer's bed grid, drawn the way it looks rather than as geometry: 10 mm
// lines with every fifth heavier, anchored on the print origin, one pixel wide
// at any zoom. Orca draws them as wide GL lines; Qt Quick 3D has no line
// primitive with a width, and quads thicken as the camera closes in, so the
// lines come from a distance field here instead.
//
// The line width arrives as `pixelSize` — millimetres per screen pixel, measured
// by the viewport from the camera — rather than from fwidth(), which comes back
// as zero in this pipeline and makes every line vanish.
//
// A cell narrower than a few pixels would turn the field into a solid wash, so
// each spacing fades out once its cells shrink below that.
//
// The material is Shaded because an Unshaded custom material gets no varyings
// here: VAR_WORLD_POSITION comes back undefined and the plane draws nothing.

float gridLines(vec2 coord, float spacing, float width)
{
    float cell = spacing / max(width, 0.0001);
    float fade = smoothstep(2.0, 5.0, cell);
    float lineWidth = min(width, spacing * 0.25);
    vec2 gap = abs(fract(coord / spacing + 0.5) - 0.5) * spacing;
    vec2 line = 1.0 - smoothstep(vec2(lineWidth * 0.5), vec2(lineWidth * 1.5), gap);
    return max(line.x, line.y) * fade;
}

void MAIN()
{
    vec2 coord = vec2(VAR_WORLD_POSITION.x, -VAR_WORLD_POSITION.z);
    float width = max(pixelSize, 0.0001);
    float minor = gridLines(coord, 10.0, width) * minorAlpha;
    float major = gridLines(coord, 50.0, width) * majorAlpha;
    BASE_COLOR = vec4(lineColor.rgb, lineColor.a * max(minor, major));
}
