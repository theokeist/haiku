#ifndef TILE_DAMAGE_TRACKER_H
#define TILE_DAMAGE_TRACKER_H

#include <Rect.h>
#include <SupportDefs.h>
#include <algorithm>
#include <new>

#define TILE_SIZE 32
#define TILE_SHIFT 5

struct Tile {
	bool dirty;
};

class TileDamageTracker {
public:
	TileDamageTracker(int32 screenWidth, int32 screenHeight)
		: fTiles(NULL)
	{
		fColumns = (screenWidth + TILE_SIZE - 1) >> TILE_SHIFT;
		fRows = (screenHeight + TILE_SIZE - 1) >> TILE_SHIFT;
		fTiles = new(std::nothrow) Tile[fColumns * fRows];
		ClearAll();
	}

	~TileDamageTracker()
	{
		delete[] fTiles;
	}

	void Resize(int32 screenWidth, int32 screenHeight)
	{
		int32 newColumns = (screenWidth + TILE_SIZE - 1) >> TILE_SHIFT;
		int32 newRows = (screenHeight + TILE_SIZE - 1) >> TILE_SHIFT;
		
		if (newColumns == fColumns && newRows == fRows && fTiles != NULL)
			return;

		Tile* newTiles = new(std::nothrow) Tile[newColumns * newRows];
		if (newTiles == NULL)
			return;

		delete[] fTiles;
		fTiles = newTiles;
		fColumns = newColumns;
		fRows = newRows;
		ClearAll();
	}

	void MarkDirty(BRect rect)
	{
		if (fTiles == NULL)
			return;

		// Clamp to screen bounds
		int32 tx0 = std::max(0, (int32)rect.left >> TILE_SHIFT);
		int32 ty0 = std::max(0, (int32)rect.top >> TILE_SHIFT);
		int32 tx1 = std::min(fColumns - 1, (int32)rect.right >> TILE_SHIFT);
		int32 ty1 = std::min(fRows - 1, (int32)rect.bottom >> TILE_SHIFT);

		for (int32 y = ty0; y <= ty1; y++) {
			for (int32 x = tx0; x <= tx1; x++) {
				fTiles[y * fColumns + x].dirty = true;
			}
		}
	}

	bool IsTileDirty(int32 x, int32 y) const
	{
		if (fTiles == NULL || x < 0 || x >= fColumns || y < 0 || y >= fRows)
			return false;
		return fTiles[y * fColumns + x].dirty;
	}

	void ClearTile(int32 x, int32 y)
	{
		if (fTiles != NULL && x >= 0 && x < fColumns && y >= 0 && y < fRows)
			fTiles[y * fColumns + x].dirty = false;
	}

	void Clear()
	{
		ClearAll();
	}

	void ClearAll()
	{
		if (fTiles == NULL)
			return;
		for (int32 i = 0; i < fColumns * fRows; i++)
			fTiles[i].dirty = false;
	}

	int32 Columns() const { return fColumns; }
	int32 Rows() const { return fRows; }

private:
	int32 fColumns;
	int32 fRows;
	Tile* fTiles;
};

#endif
