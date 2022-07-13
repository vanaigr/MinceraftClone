#pragma once

#include"Chunk.h"
#include"Vector.h"

void writeChunk  (chunk::Chunk chunk);
bool tryReadChunk(chunk::Chunk chunk);

void genChunksColumnAt(chunk::Chunks &chunks, vec2i const columnPosition, bool const loadChunks);
