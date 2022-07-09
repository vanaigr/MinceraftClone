#pragma once

#include"Chunk.h"
#include"Position.h"

void updateBlockDataWithoutNeighbours(chunk::Chunk chunk, pBlock const blockCoord);

void updateBlockDataNeighboursInfo(chunk::Chunk chunk, pBlock const blockCoord);

void updateBlocksDataInArea(chunk::Chunk startChunk, pBlock const firstRel, pBlock const lastRel);