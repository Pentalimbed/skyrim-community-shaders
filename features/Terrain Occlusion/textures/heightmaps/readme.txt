Terrain heightmap for Tamriel

Each cell has 32x32 heightmap data points, so max resolution is 32x32 pixels per cell.

[worldspace editorID].HeigthMap.[West cell].[South cell].[East cell].[North cell].[z min].[z max].dds

The min/max cell coordinates are the actual cells that contain terrain height data.

For actual z min (pure black), z max (pure white) from file name multiply by 8

Skyrim.HeightMap.-57.-43.61.50.-32768.32768.dds
native Skyrim.esm data