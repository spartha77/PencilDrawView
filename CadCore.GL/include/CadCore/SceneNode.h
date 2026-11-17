#pragma once
// =============================================================================
// CadCore.GL / SceneNode.h
//
// THE SCENE GRAPH
//   The scene is a TREE of nodes, not a flat display list:
//
//       Root (group)
//        +-- Assembly A (group)          <- hide this and everything below goes
//        |    +-- Bracket   (mesh, B-Rep)
//        |    +-- Cover     (mesh, STL)
//        +-- Scan     (point cloud)
//
//   Every node has
//     * a VISIBLE flag  - the renderer skips an invisible node AND its whole
//                         subtree during traversal,
//     * a LOCAL TRANSFORM relative to its parent - world transform is the
//                         product of all transforms from the root down,
//     * a stable integer ID used by the managed (C#) side to refer to it.
//
//   Each frame the renderer walks the tree from the root, accumulating
//   transforms and collecting the visible drawable nodes (see Renderer.cpp).
// =============================================================================

#include "CadCore/Math.h"
#include <memory>
#include <string>
#include <vector>

namespace CadCore {

enum class NodeType : int {
    Group      = 0,  ///< no geometry; groups children under one transform/visibility
    Mesh       = 1,  ///< triangles (+ edge lines): tessellated B-Rep or STL
    PointCloud = 2,  ///< points
    Curve      = 3   ///< a line / arc / spline, drawn as a line (see CurveNode.h)
};

class Node {
public:
    Node(NodeType type, std::string name) : m_type(type), m_name(std::move(name)) {}
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    NodeType Type() const { return m_type; }

    int  Id() const { return m_id; }
    void SetId(int id) { m_id = id; }           // assigned by Scene

    const std::string& Name() const { return m_name; }
    void SetName(std::string n) { m_name = std::move(n); }

    /// The "show" flag. false hides this node and all of its descendants.
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }

    const Mat4& LocalTransform() const { return m_local; }
    void SetLocalTransform(const Mat4& m) { m_local = m; }

    Node* Parent() const { return m_parent; }
    const std::vector<std::unique_ptr<Node>>& Children() const { return m_children; }

    /// Takes ownership of `child` and returns a plain pointer to it.
    Node* AddChild(std::unique_ptr<Node> child) {
        child->m_parent = this;
        m_children.push_back(std::move(child));
        return m_children.back().get();
    }

    /// Detaches `child` and returns ownership (destroying it frees its subtree).
    std::unique_ptr<Node> RemoveChild(Node* child) {
        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            if (it->get() == child) {
                std::unique_ptr<Node> owned = std::move(*it);
                m_children.erase(it);
                owned->m_parent = nullptr;
                return owned;
            }
        }
        return nullptr;
    }

    /// Bounds of this node's own geometry in its local coordinates.
    virtual AABB LocalBounds() const { return AABB(); }

private:
    NodeType    m_type;
    int         m_id = 0;
    std::string m_name;
    bool        m_visible = true;
    Mat4        m_local;
    Node*       m_parent = nullptr;
    std::vector<std::unique_ptr<Node>> m_children;
};

/// A node with no geometry of its own.
class GroupNode : public Node {
public:
    explicit GroupNode(std::string name) : Node(NodeType::Group, std::move(name)) {}
};

} // namespace CadCore
