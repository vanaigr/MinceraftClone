#pragma once

#include"Chunk.h"
#include"Vector.h"

#include<string_view>

void writeChunk(chunk::Chunk chunk, std::string_view const worldName);
void genChunksColumnAt(chunk::Chunks &chunks, vec2i const columnPosition, std::string_view const worldName, bool const loadChunks);
