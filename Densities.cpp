#include "Densities.h"

#include "PerlinSimplexNoise.h"
#include "TransVoxel.h"
//#include "DensityValue.h"


FDensities::FDensities()
{
	this->position = FVector::ZeroVector;
	this->edgeSize = 0;
	this->voxelSize = 0;
	this->chunkSize = 0;
}

FDensities::FDensities(FVector position, int edgeSize, int voxelSize, int chunkSize)
{
	this->position = position;
	this->edgeSize = edgeSize;
	this->voxelSize = voxelSize;
	this->chunkSize = chunkSize;
}

void FDensities::GenerateDensities(float noiseFrequency, int noiseOctaveCount, float amplitude, float heightOffset)
{
	int edgeHalfSize = this->edgeSize / 2;
	int densitiesPerEdge = this->DensitiesPerEdge();
	int chunksPerEdge = this->ChunksPerEdge();

	//Convert coordinates for applying in Simplex Noise algorithm further 
	int StartX = (this->position.X - edgeHalfSize) / this->voxelSize;
	int StartY = (this->position.Y - edgeHalfSize) / this->voxelSize;
	int EndX = StartX + densitiesPerEdge;
	int EndY = StartY + densitiesPerEdge;

	float MinZ = this->position.Z - edgeHalfSize;
	float MaxZ = this->position.Z + edgeHalfSize;

	this->localHeights.Empty();

	//Fill Heights
	for (int Y = StartY, LY = 0; Y < EndY; Y++, LY++)
	{
		TArray<float> HeightsRow;
		for (int X = StartX, LX = 0; X < EndX; X++, LX++)
		{
			//Get global noise value and convert to local space
			HeightsRow.Add(OctaveNoise(X, Y, noiseFrequency * 0.001f, noiseOctaveCount) * amplitude - heightOffset - MinZ);
		}
		localHeights.Add(HeightsRow);
	}

	//Generate Densities for voxel corners near given height
	for (int Y = StartY, LY = 0; Y < EndY; Y++, LY++)
	{
		for (int X = StartX, LX = 0; X < EndX; X++, LX++)
		{
			//Local Height
			float LHeight = localHeights[LY][LX];

			//float LHeight = Height - MinZ;

			//Find local Z-coordinate for lower voxel corner
			int LZ = LHeight / this->voxelSize;

			//Calculate Densities for near voxel corners of lower and higher voxel corners
			//i.e. in area of 3x3x4 in x,y,z respectively
			for (int z = LZ - 1; z <= LZ + 2; z++)
			{
				if (!(z >= 0 && z < densitiesPerEdge))
					continue;

				int zOffset = z * densitiesPerEdge * densitiesPerEdge;
				for (int y = LY - 1; y <= LY + 1; y++)
				{
					if (!(y >= 0 && y < densitiesPerEdge))
						continue;

					int yOffset = y * densitiesPerEdge;
					for (int x = LX - 1; x <= LX + 1; x++)
					{
						if (!(x >= 0 && x < densitiesPerEdge))
							continue;

						uint32 DensityKey = zOffset + yOffset + x;
						if (!this->densities.Contains(DensityKey))
						{
							float LHeight = this->localHeights[y][x];						

							float Density = z - LHeight / this->voxelSize;

							FDensityValue densityValue(Density);
							this->densities.Add(DensityKey, densityValue);

							TSet<uint32> ChunksToUpdateIds;

							GetAffectedChunkIds(x, y, z, ChunksToUpdateIds);

							for (const uint32& chunkId : ChunksToUpdateIds)
							{
								chunksToUpdate.Add(chunkId);
							}

						}
					}
				}
			}
		}
	}
}

void FDensities::GenerateDensitiesForOctree(float noiseFrequency, int noiseOctaveCount, float amplitude, float heightOffset)
{
	int edgeHalfSize = this->edgeSize / 2;
	int densitiesPerEdge = this->DensitiesPerEdge();
	int chunksPerEdge = this->ChunksPerEdge();

	//Convert coordinates for applying in Simplex Noise algorithm further 
	int StartX = (this->position.X - edgeHalfSize) / this->voxelSize;
	int StartY = (this->position.Y - edgeHalfSize) / this->voxelSize;
	int EndX = StartX + densitiesPerEdge;
	int EndY = StartY + densitiesPerEdge;

	float MinZ = this->position.Z - edgeHalfSize;
	float MaxZ = this->position.Z + edgeHalfSize;

	this->localHeights.Empty();

	//Fill Heights
	for (int Y = StartY, LY = 0; Y < EndY; Y++, LY++)
	{
		TArray<float> HeightsRow;
		for (int X = StartX, LX = 0; X < EndX; X++, LX++)
		{
			//Get global noise value and convert to local space
			HeightsRow.Add(OctaveNoise(X, Y, noiseFrequency * 0.001f, noiseOctaveCount) * amplitude - heightOffset - MinZ);
		}
		localHeights.Add(HeightsRow);
	}

	//Generate Densities for voxel corners near given height
	for (int Y = StartY, LY = 0; Y < EndY; Y++, LY++)
	{
		for (int X = StartX, LX = 0; X < EndX; X++, LX++)
		{
			//Local Height
			float LHeight = localHeights[LY][LX];

			//float LHeight = Height - MinZ;

			//Find local Z-coordinate for lower voxel corner
			int LZ = LHeight / this->voxelSize;

			int yOffset = LY * densitiesPerEdge;

			for (int z = LZ ; z <= LZ + 1; z++)
			{
				if (!(z >= 0 && z < densitiesPerEdge))
					continue;

				int zOffset = z * densitiesPerEdge * densitiesPerEdge;				

				uint32 DensityKey = zOffset + yOffset + LX;			
				
				float Density = z - LHeight / this->voxelSize;

				FDensityValue densityValue(Density);
				this->densities.Add(DensityKey, densityValue);				
			}
		}
	}
}

//Convert Chunk's local position in voxels into world position in world units
FVector FDensities::ChunkLocalToWorldPosition(FVector localPosition)
{
	return this->position - this->edgeSize / 2 + localPosition * this->voxelSize;
}

//Get Ids of chunks containing voxel corner
//cx,cy,cz - local corner coordinates in voxels
void FDensities::GetAffectedChunkIds(int cx, int cy, int cz, TSet<uint32>& outIds)
{
	int chunksPerEdge = this->ChunksPerEdge();

	FIntVector cornerCoords(cx, cy, cz);
	//I'm using origin corner(cx,cy,cz coords) as right top near corner of Voxel Cell
	//If more than 1 chunks contains this corner - they will be detected by checking corners of voxel cell
	//Example: chunk size = 2, origin corner coincides with right top near corner of Chunk 
	//This means that 8 chunks are containing origin corner(#7 in picture below)
	//      *--*--*       Corners of voxel cell are numbered (corner #0 is 'central' corner of Chunk)
	//     /  /  /|       Corners marked with '*' are other voxel corners of Chunk
	//    *--4--5 *
	//   /  /  /|/|       All 8 Chunks containing origin corner #7 will be detected during further callculations
	//  *--6--7 1 *
	//  |  |  |/|/
	//  *--2--3 *
	//  |  |  |/
	//  *--*--*

	for (int32 i = 0; i < 8; i++)
	{
		FIntVector cellCornerCoords = cornerCoords - TransVoxel::SearchCubeCorners[i];

		int chunkX = cellCornerCoords.X / this->chunkSize;
		int chunkY = cellCornerCoords.Y / this->chunkSize;
		int chunkZ = cellCornerCoords.Z / this->chunkSize;

		if (chunkX < 0 || chunkY < 0 || chunkZ < 0 || chunkX >= chunksPerEdge || chunkY >= chunksPerEdge || chunkZ >= chunksPerEdge)
			continue;

		int chunkXOffset = chunkX;
		int chunkYOffset = chunkY * chunksPerEdge;
		int chunkZOffset = chunkZ * chunksPerEdge * chunksPerEdge;

		uint32 chunkId = chunkXOffset + chunkYOffset + chunkZOffset;

		outIds.Add(chunkId);
	}
}

float FDensities::OctaveNoise(float x, float y, float frequency, int octaveCount)
{
	float value = 0;

	for (int i = 0; i < octaveCount; i++)
	{
		int octaveModifier = (int)FMath::Pow(2, i);

		// (x+1)/2 because Simplex Noise returns a value from -1 to 1 so it needs to be scaled to go from 0 to 1.
		float pureNoise = (SimplexNoise::noise(octaveModifier * x * frequency, octaveModifier * y * frequency) + 1) / 2.f;
		value += pureNoise / octaveModifier;
	}

	return value;
}
