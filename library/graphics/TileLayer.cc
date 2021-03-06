/*
 * Gamedev Framework (gf)
 * Copyright (C) 2016-2019 Julien Bernard
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */
#include <gf/TileLayer.h>

#include <cassert>
#include <cstdio>
#include <algorithm>

#include <gf/Log.h>

#include <gf/RenderTarget.h>
#include <gf/Transform.h>
#include <gf/VectorOps.h>

namespace gf {
#ifndef DOXYGEN_SHOULD_SKIP_THIS
inline namespace v1 {
#endif

  constexpr int TileLayer::NoTile;

  TileLayer::TileLayer()
  : m_orientation(TileOrientation::Unknown)
  , m_mapCellIndex(MapCellIndex::Odd)
  , m_mapCellAxis(MapCellAxis::Y)
  , m_layerSize(0, 0)
  , m_tileSize(0, 0)
  , m_rect(RectI::empty())
  , m_vertices(PrimitiveType::Triangles)
  {
  }

  TileLayer::TileLayer(Vector2i layerSize, TileOrientation orientation)
  : m_orientation(orientation)
  , m_mapCellIndex(MapCellIndex::Odd)
  , m_mapCellAxis(MapCellAxis::Y)
  , m_layerSize(layerSize)
  , m_tileSize(0, 0)
  , m_tiles(layerSize)
  , m_rect(RectI::empty())
  , m_vertices(PrimitiveType::Triangles)
  {
    clear();
  }

  void TileLayer::setTileSize(Vector2i tileSize) {
    m_tileSize = tileSize;
  }

  void TileLayer::setTile(Vector2i position, int tile, Flags<Flip> flip) {
    assert(m_tiles.isValid(position));
    m_tiles(position) = { tile, flip };
  }

  int TileLayer::getTile(Vector2i position) const {
    assert(m_tiles.isValid(position));
    return m_tiles(position).tile;
  }


  Flags<Flip> TileLayer::getFlip(Vector2i position) const {
    assert(m_tiles.isValid(position));
    return m_tiles(position).flip;
  }

  void TileLayer::clear() {
    for (auto& cell : m_tiles) {
      cell.tile = NoTile;
      cell.flip = None;
    }
  }

  RectF TileLayer::getLocalBounds() const {
    return RectF::fromPositionSize({ 0.0f, 0.0f }, m_layerSize * m_tileSize);
  }

  void TileLayer::setAnchor(Anchor anchor) {
    setOriginFromAnchorAndBounds(anchor, getLocalBounds());
  }

  VertexBuffer TileLayer::commitGeometry() const {
    VertexArray vertices(PrimitiveType::Triangles);
    RectI rect = RectI::fromPositionSize({ 0, 0 }, m_layerSize);
    fillVertexArray(vertices, rect);

    return VertexBuffer(vertices.getVertexData(), vertices.getVertexCount(), vertices.getPrimitiveType());
  }

  void TileLayer::draw(RenderTarget& target, const RenderStates& states) {
    if (!m_tileset.hasTexture() || m_orientation == TileOrientation::Unknown) {
      return;
    }

    gf::Vector2i tileSize = m_tileSize;

    if (m_orientation == TileOrientation::Staggered) {
      tileSize.y /= 2;
    }

    // compute the viewable part of the layer

    const View& view = target.getView();
    Vector2f size = view.getSize();
    Vector2f center = view.getCenter();

    size.width = size.height = gf::Sqrt2 * std::max(size.width, size.height);

    RectF world = RectF::fromCenterSize(center, size);
    RectF local = gf::transform(getInverseTransform(), world).grow(std::max(tileSize.width, tileSize.height));

    RectF layer = RectF::fromPositionSize({ 0.0f, 0.0f }, m_layerSize * tileSize);

    RectI rect;
    RectF intersection;

    if (local.intersects(layer, intersection)) {
      rect = RectI::fromPositionSize(intersection.getPosition() / tileSize + 0.5f, intersection.getSize() / tileSize + 0.5f);
      rect.intersects(gf::RectI::fromPositionSize({ 0, 0 }, m_layerSize - 1), rect);
    }

    // build vertex array (if necessary)

    if (rect != m_rect) {
//       std::printf("rect | min: %i,%i | max: %i,%i\n", rect.min.x, rect.min.y, rect.max.x, rect.max.y);
      m_rect = rect;
      updateGeometry();
    }

    // call draw

    RenderStates localStates = states;

    localStates.transform *= getTransform();
    localStates.texture[0] = &m_tileset.getTexture();

    target.draw(m_vertices, localStates);
  }


  void TileLayer::fillVertexArray(VertexArray& array, RectI rect) const {
    array.reserve(static_cast<std::size_t>(rect.getWidth()) * static_cast<std::size_t>(rect.getHeight()) * 6);

    Vector2i cell;

    for (cell.y = rect.min.y; cell.y <= rect.max.y; ++cell.y) {
      for (cell.x = rect.min.x; cell.x <= rect.max.x; ++cell.x) {
        assert(m_tiles.isValid(cell));
        int tile = m_tiles(cell).tile;

        if (tile == NoTile) {
          continue;
        }

        assert(tile >= 0);

        // position

        Vector2f position, size;

        if (m_orientation == TileOrientation::Orthogonal) {
          size = m_tileSize;
          position = cell * m_tileSize + m_tileset.getOffset();
        } else {
          assert(m_orientation == TileOrientation::Staggered);
          size = m_tileset.getTileSize();

          if (cell.y % 2 == 0) {
            position = cell * m_tileSize;
            position.y /= 2;
          } else {
            position = cell * m_tileSize;
            position.y /= 2;
            position.x += m_tileSize.width / 2;
          }

          position += m_tileset.getOffset();
        }

        RectF box = RectF::fromPositionSize(position, size);

        // texture coords

        RectF textureCoords = m_tileset.computeTextureCoords(tile);

        // vertices

        Vertex vertices[4];

        vertices[0].position = box.getTopLeft();
        vertices[1].position = box.getTopRight();
        vertices[2].position = box.getBottomLeft();
        vertices[3].position = box.getBottomRight();

        vertices[0].texCoords = textureCoords.getTopLeft();
        vertices[1].texCoords = textureCoords.getTopRight();
        vertices[2].texCoords = textureCoords.getBottomLeft();
        vertices[3].texCoords = textureCoords.getBottomRight();

        auto flip = m_tiles(cell).flip;

        // order of flip matters:
        // http://docs.mapeditor.org/en/latest/reference/tmx-map-format/#tile-flipping

        if (flip.test(Flip::Diagonally)) {
          std::swap(vertices[1].texCoords, vertices[2].texCoords);
        }

        if (flip.test(Flip::Horizontally)) {
          std::swap(vertices[0].texCoords, vertices[1].texCoords);
          std::swap(vertices[2].texCoords, vertices[3].texCoords);
        }

        if (flip.test(Flip::Vertically)) {
          std::swap(vertices[0].texCoords, vertices[2].texCoords);
          std::swap(vertices[1].texCoords, vertices[3].texCoords);
        }

        // first triangle

        array.append(vertices[0]);
        array.append(vertices[1]);
        array.append(vertices[2]);

        // second triangle

        array.append(vertices[2]);
        array.append(vertices[1]);
        array.append(vertices[3]);
      }
    }

  }

  void TileLayer::updateGeometry() {
    m_vertices.clear();

    if (!m_tileset.hasTexture() || m_tileSize.width == 0 || m_tileSize.height == 0) {
      return;
    }

    fillVertexArray(m_vertices, m_rect);
  }

#ifndef DOXYGEN_SHOULD_SKIP_THIS
}
#endif
}
