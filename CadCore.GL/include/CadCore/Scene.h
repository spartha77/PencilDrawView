#pragma once
// =============================================================================
// CadCore.GL / Scene.h
//
// Owns the root of the scene graph and hands out node IDs.
// IDs let the managed (C#) layer refer to nodes without holding native
// pointers: ID 0 is always the root group.
// =============================================================================

#include "CadCore/SceneNode.h"
#include <unordered_map>

namespace CadCore {

   class Scene {
      public:
      static constexpr int kRootId = 0;

      Scene();

      Node* Root() const { return m_root.get(); }

      /// Returns the node with this ID, or nullptr.
      Node* Find(int id) const;

      /// Adds `node` under the node `parentId` (must exist) and returns its new ID.
      /// Throws std::invalid_argument if the parent does not exist.
      int Add(int parentId, std::unique_ptr<Node> node);

      /// Removes a node and its entire subtree. The root cannot be removed.
      /// The GL context must be current (GPU buffers are freed). Returns false if
      /// the ID is unknown.
      bool Remove(int id);

      /// Removes everything below the root.
      void Clear();

      /// World-space bounds of all VISIBLE geometry.
      AABB VisibleBounds() const;

      private:
      void Unregister(Node* node);

      std::unique_ptr<Node> m_root;
      std::unordered_map<int, Node*> m_byId;
      int m_nextId = 1;
   };

} // namespace CadCore
