Terrain heightmap for Tamriel

Each cell has 32x32 heightmap data points, so max resolution is 32x32 pixels per cell.

[worldspace editorID].HeigthMap.[East cell].[South cell].[West cell].[North cell].[z min].[z max].dds

The min/max cell coordinates are the actual cells that contain terrain height data.

For actual z min, z max from file name multiply by 8
For actual z value subtract 32767, then multiply by 8

Tamriel.HeightMap.-57.-43.61.50.-4629.4924.dds
native Skyrim.esm data

Tamriel.HeightMap.-96.-96.95.96.-4863.4924.dds
with SSE Terrain Tamriel Full Extend from https://www.nexusmods.com/skyrimspecialedition/mods/54680
