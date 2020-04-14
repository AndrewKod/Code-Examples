#pragma once

#include "CoreMinimal.h"

#include "DensityValue.h"
//struct FDensityValue;

class FDensities
{
	//World position of structure's center
	FVector position;

	//Edge size in world units
	int     edgeSize;

	//Voxel size in world units
	int     voxelSize;

	//Chunk size in voxels
	int     chunkSize;

	//heights(Z) at [Y][X] position calculated with noise algorythm
	TArray<TArray<float>> localHeights;

public:

	/**
	Voxel Corners densities, each in range [-16383; 16383]
	Using for vertex positions calculation

	value > 0 means that surface is below corner
	value < 0 - is above corner
	0 - intersecting corner
	**/
	TMap<uint32, FDensityValue> densities;

	// Stores the density modifications because the densities can not be modified while a job that requires them is running.
	TMap<uint32, FDensityValue> modifiedDensities;

	//Indices of Chunks for edition
	TSet<uint32>                chunksToUpdate;

	//Constructor
	FDensities();

	FDensities(FVector position, int edgeSize, int voxelSize, int chunkSize);

	FVector GetPosition() { return this->position; }

	int GetEdgeSize()     { return this->edgeSize; }

	int GetVoxelSize()    { return this->voxelSize; }

	int GetChunkSize()    { return this->chunkSize; }
	
	const TMap<uint32, FDensityValue>& GetDensities() { return this->densities; }

	//Add density to densities Map
	//If Key is already present - update Value
	void AddDensity(uint32 Key, FDensityValue Value)  { this->densities.Add(Key, Value); }

	const TSet<uint32>& GetChunksToUpdate()           { return this->chunksToUpdate; }	

	void AddChunkToUpdate(uint32 chunkId)             { this->chunksToUpdate.Add(chunkId); }

	void ClearChunksToUpdate()                        { this->chunksToUpdate.Empty(); }	
	
	const TArray<TArray<float>>& GetHeights()		  { return this->localHeights; }

	int VoxelsPerEdge()                               { return this->edgeSize / this->voxelSize; }
	
	int ChunksPerEdge()                               { return this->edgeSize / this->voxelSize / this->chunkSize; }
	
	int DensitiesPerEdge()                            { return this->edgeSize / this->voxelSize + 1; }
	
	void GenerateDensities(float noiseFrequency = 1.f, int noiseOctaveCount = 16, float amplitude = 6000.f, float heightOffset = 5000.f);

	void GenerateDensitiesForOctree(float noiseFrequency = 1.f, int noiseOctaveCount = 16, float amplitude = 6000.f, float heightOffset = 5000.f);

	FVector ChunkLocalToWorldPosition(FVector localPosition);

	void ClearModifiedDensities() { this->modifiedDensities.Empty(); }

	void ModifyDensity(uint32 key, FDensityValue value) { this->modifiedDensities.Add(key, value); }

	//Get Ids of chunks containing voxel corner
	//cx,cy,cz - corner coordinates
	void GetAffectedChunkIds(int cx, int cy, int cz, TSet<uint32>& outIds);

private:
	
	

	float OctaveNoise(float x, float y, float frequency, int octaveCount);

	

	
};